#define DECODE_NEC
#define IR_SEND_PIN 3
#include <IRremote.hpp>

const int NUM_CHILDREN = 4;

const int PIN_BTN_ORDER[NUM_CHILDREN] = {4, 5, 6, 7};  // 子機ごとの「参加/退場」トグルボタン
// 各子機ボタン横のLED (D4→A0, D5→A1, D6→A2, D7→A3)。
// ボタン押下毎に必ずトグル (参加予約/取消/退場予約/取消 のいずれでも反転)。
const int PIN_LED_CHILD[NUM_CHILDREN] = {A0, A1, A2, A3};
const int PIN_BTN_START = 8;
const int PIN_POT_TEMPO = A5;     // BPM可変抵抗器 (uxcell 10kΩ シングルターン)。回転で 60〜150 BPM
const int PIN_BTN_RESET = 10;
const int LED_INDICATOR = 13;
const int PIN_SYNC_OUT = 9;          // 遅延計測用同期パルス出力（子機のD3に直結）

const int QUANTIZE_TICKS = 8;     // 参加予約のアライメント(0,8,16,...)
// 子機の TOTAL_TICKS (=楽譜1周分のIR tick数) と一致させる必要がある。
// 退場予約はこの倍数(子機の譜面ループ末尾)にアライメントする。
const int SCORE_TICKS = 32;
const int TEMPO_MIN_BPM = 60;
const int TEMPO_MAX_BPM = 150;
const unsigned long POT_READ_INTERVAL_MS = 50;
const uint16_t IR_ADDRESS = 0x00;

bool isActive[NUM_CHILDREN]    = {false, false, false, false}; // 参加済みフラグ
long joinTick[NUM_CHILDREN]    = {-1, -1, -1, -1};              // 参加予定tick（未予約は-1）
long leaveTick[NUM_CHILDREN]   = {-1, -1, -1, -1};              // 退場予定tick（未予約は-1）
long joinedAtTick[NUM_CHILDREN] = {-1, -1, -1, -1};             // 実際に参加発火したtick(退場アライメント計算用)

bool isPlaying  = false;
int  tempoBpm   = 120;
long tickCount  = 0;
unsigned long lastTickMs = 0;

int prevBtnOrder[NUM_CHILDREN] = {HIGH, HIGH, HIGH, HIGH};
int prevBtnStart = HIGH;
int prevBtnReset = HIGH;

// ポテンショメータ読み取りの平滑化と更新間引き用
unsigned long lastPotReadMs = 0;
int smoothedPotRaw = -1;
int lastReportedBpm = -1;

void setup() {
  Serial.begin(115200);
  IrSender.begin(IR_SEND_PIN);

  for (int i = 0; i < NUM_CHILDREN; i++) {
    pinMode(PIN_BTN_ORDER[i], INPUT_PULLUP);
    pinMode(PIN_LED_CHILD[i], OUTPUT);
    digitalWrite(PIN_LED_CHILD[i], LOW);
  }
  pinMode(PIN_BTN_START, INPUT_PULLUP);
  pinMode(PIN_BTN_RESET, INPUT_PULLUP);
  // PIN_POT_TEMPO はアナログ入力なので pinMode 不要（pull-up は付けない）
  pinMode(LED_INDICATOR, OUTPUT);
  pinMode(PIN_SYNC_OUT, OUTPUT);
  digitalWrite(PIN_SYNC_OUT, LOW);

  printHelp();
}

void loop() {
  handleSerialCommand();
  handleOrderButtons();
  handleResetButton();
  handleTempoPot();
  handleStartButton();

  if (isPlaying) {
    unsigned long now = millis();
    unsigned long interval = 60000UL / (unsigned long)tempoBpm / 2UL;
    if (now - lastTickMs >= interval) {
      lastTickMs = now;
      tick();
      tickCount++;
    }
  }

  // 子機LEDを内部状態に同期。isActive XOR (joinTick≠-1 || leaveTick≠-1) で
  // 「参加意思あり=点灯」となり、ボタン押下毎に確実にトグルする。
  // 4状態の遷移は tick() で join/leave 反映後も継続する:
  //   未参加+予約なし(OFF) → 未参加+joinTick(ON) → tickでjoin → 参加+予約なし(ON)
  //   → 参加+leaveTick(OFF) → tickでleave → 未参加+予約なし(OFF)
  for (int i = 0; i < NUM_CHILDREN; i++) {
    bool on = isActive[i] != ((joinTick[i] != -1) || (leaveTick[i] != -1));
    digitalWrite(PIN_LED_CHILD[i], on ? HIGH : LOW);
  }
}

// 参加ボタン（子機ごと）のトグル処理（ボタン・シリアル共有）。
//   未参加     → 次の区切りから参加予約
//   参加予約中 → その予約を取消（参加しない）
//   参加中     → 次の区切りから退場予約
//   退場予約中 → その予約を取消（残留）
void toggleChild(int i) {
  if (i < 0 || i >= NUM_CHILDREN) return;
  if (!isPlaying) {
    Serial.println("[!] 再生中のみ操作できます");
    return;
  }

  long joinBoundary = ((tickCount / QUANTIZE_TICKS) + 1) * QUANTIZE_TICKS;

  if (isActive[i]) {
    if (leaveTick[i] != -1) {
      leaveTick[i] = -1;
      Serial.print("child ");
      Serial.print(i);
      Serial.println(" leave cancelled (stay)");
    } else {
      // 子機が現在のループを最後まで弾き切ってから抜けるよう、
      // joinedAtTick から数えて次の SCORE_TICKS 境界に揃える。
      long ticksSinceJoin = tickCount - joinedAtTick[i];
      long loopsCompleted = ticksSinceJoin / SCORE_TICKS;
      leaveTick[i] = joinedAtTick[i] + (loopsCompleted + 1) * SCORE_TICKS;
      Serial.print("child ");
      Serial.print(i);
      Serial.print(" will leave at tick ");
      Serial.print(leaveTick[i]);
      Serial.print(" (end of loop ");
      Serial.print(loopsCompleted + 1);
      Serial.println(")");
    }
  } else {
    if (joinTick[i] != -1) {
      joinTick[i] = -1;
      Serial.print("child ");
      Serial.print(i);
      Serial.println(" join cancelled");
    } else {
      joinTick[i] = joinBoundary;
      Serial.print("child ");
      Serial.print(i);
      Serial.print(" will join at tick ");
      Serial.println(joinTick[i]);
    }
  }
}

// 参加ボタン：押すごとに 参加予約⇄退場予約 をトグルする（リアルタイム、次の区切りに反映）
void handleOrderButtons() {
  for (int i = 0; i < NUM_CHILDREN; i++) {
    int s = digitalRead(PIN_BTN_ORDER[i]);
    if (prevBtnOrder[i] == HIGH && s == LOW) {
      toggleChild(i);
      delay(30);
    }
    prevBtnOrder[i] = s;
  }
}

void doReset() {
  isPlaying = false;
  tickCount = 0;
  for (int i = 0; i < NUM_CHILDREN; i++) {
    isActive[i] = false;
    joinTick[i] = -1;
    leaveTick[i] = -1;
    joinedAtTick[i] = -1;
  }
  digitalWrite(LED_INDICATOR, LOW);
  Serial.println("reset");
}

void handleResetButton() {
  int s = digitalRead(PIN_BTN_RESET);
  if (prevBtnReset == HIGH && s == LOW) {
    doReset();
    delay(30);
  }
  prevBtnReset = s;
}

// 可変抵抗（A5）の値を 60〜150 BPM に線形マップしてリアルタイム反映する。
// 50ms間隔で読み取り、指数移動平均で軽くノイズを抑える。
// 既存の tick 周期計算は毎ループ tempoBpm を参照しているため、即座に反映される。
void handleTempoPot() {
  unsigned long now = millis();
  if (now - lastPotReadMs < POT_READ_INTERVAL_MS) return;
  lastPotReadMs = now;

  int raw = analogRead(PIN_POT_TEMPO);
  if (smoothedPotRaw < 0) smoothedPotRaw = raw;
  else smoothedPotRaw = (smoothedPotRaw * 3 + raw) / 4;

  int newBpm = map(smoothedPotRaw, 0, 1023, TEMPO_MIN_BPM, TEMPO_MAX_BPM);
  newBpm = constrain(newBpm, TEMPO_MIN_BPM, TEMPO_MAX_BPM);
  tempoBpm = newBpm;

  // 微小変動でシリアルが埋まらないよう、2BPM以上変わったときだけログ
  if (lastReportedBpm < 0 || abs(newBpm - lastReportedBpm) >= 2) {
    Serial.print("tempo=");
    Serial.println(tempoBpm);
    lastReportedBpm = newBpm;
  }
}

// 再生開始：誰も参加していない状態から開始する（参加はボタンで都度行う）
void doStart() {
  if (isPlaying) {
    Serial.println("[!] already playing");
    return;
  }
  isPlaying  = true;
  tickCount  = 0;
  lastTickMs = millis() - (60000UL / (unsigned long)tempoBpm / 2UL);
  for (int i = 0; i < NUM_CHILDREN; i++) {
    isActive[i] = false;
    joinTick[i] = -1;
    leaveTick[i] = -1;
    joinedAtTick[i] = -1;
  }
  Serial.println("start");
}

void handleStartButton() {
  int s = digitalRead(PIN_BTN_START);
  if (prevBtnStart == HIGH && s == LOW) {
    doStart();
    delay(30);
  }
  prevBtnStart = s;
}

void handleSerialCommand() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\r' || c == '\n' || c == ' ') continue;

    if (c >= '0' && c <= ('0' + NUM_CHILDREN - 1)) {
      toggleChild(c - '0');
    } else if (c == 's' || c == 'S') {
      doStart();
    } else if (c == 'r' || c == 'R') {
      doReset();
    } else if (c == '?') {
      printHelp();
    }
  }
}

void printHelp() {
  Serial.println("=== hack_oya_kai help ===");
  Serial.println("0..3 : 子機の参加/退場トグル (D4..D7 ボタンと同じ動作)");
  Serial.println("       未参加→参加予約 / 参加中→退場予約 / 予約中に再度押すと取消");
  Serial.println("s    : start");
  Serial.println("r    : reset (即時の強制停止)");
  Serial.println("?    : help");
  Serial.println("(BPM は A5 のポテンショメータで 60-150 を連続調整)");
  Serial.print("Current BPM=");
  Serial.println(tempoBpm);
}

void tick() {
  digitalWrite(LED_INDICATOR, (tickCount % 2) ? HIGH : LOW);

  uint8_t mask = 0;
  for (int i = 0; i < NUM_CHILDREN; i++) {
    // 参加予約していたtickに到達したら有効化
    if (!isActive[i] && joinTick[i] != -1 && tickCount >= joinTick[i]) {
      isActive[i] = true;
      joinTick[i] = -1;
      joinedAtTick[i] = tickCount;   // 退場アライメント計算の起点
      Serial.print("child ");
      Serial.print(i);
      Serial.println(" joined");
    }
    // 退場予約していたtickに到達したら無効化 (このtickからは送らない)
    if (isActive[i] && leaveTick[i] != -1 && tickCount >= leaveTick[i]) {
      isActive[i] = false;
      leaveTick[i] = -1;
      joinedAtTick[i] = -1;
      Serial.print("child ");
      Serial.print(i);
      Serial.println(" left");
    }
    if (isActive[i]) {
      mask |= (uint8_t)(1 << i);
    }
  }

  if (mask != 0) {
    Serial.print("[SEND] millis=");
    Serial.print(millis());
    Serial.print(" tick=");
    Serial.print(tickCount);
    Serial.print(" mask=0b");
    Serial.println(mask, BIN);
    digitalWrite(PIN_SYNC_OUT, HIGH);
    IrSender.sendNEC(IR_ADDRESS, mask, 0);
    digitalWrite(PIN_SYNC_OUT, LOW);
  }
}

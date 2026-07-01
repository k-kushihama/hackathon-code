#define DECODE_NEC
#define IR_SEND_PIN 3
#include <IRremote.hpp>

const int NUM_CHILDREN = 4;

const int PIN_BTN_ORDER[NUM_CHILDREN] = {4, 5, 6, 7};  // 子機ごとの「参加/退場」トグルボタン
const int PIN_BTN_START = 8;
const int PIN_BTN_TEMPO = 9;
const int PIN_BTN_RESET = 10;
const int LED_INDICATOR = 13;

const int QUANTIZE_TICKS = 8;     // 区切り幅（0,5,10,15,...）
// NOTE: address フィールドは tickCount下位8bit用に転用するため、固定アドレスとしては未使用

bool isActive[NUM_CHILDREN]   = {false, false, false, false}; // 参加済みフラグ
long joinTick[NUM_CHILDREN]   = {-1, -1, -1, -1};              // 参加予定tick（未予約は-1）
long leaveTick[NUM_CHILDREN]  = {-1, -1, -1, -1};              // 退場予定tick（未予約は-1）

bool isPlaying  = false;
int  tempoBpm   = 120;
long tickCount  = 0;
unsigned long lastTickMs = 0;

int prevBtnOrder[NUM_CHILDREN] = {HIGH, HIGH, HIGH, HIGH};
int prevBtnStart = HIGH;
int prevBtnTempo = HIGH;
int prevBtnReset = HIGH;

void setup() {
  Serial.begin(115200);
  IrSender.begin(IR_SEND_PIN);

  for (int i = 0; i < NUM_CHILDREN; i++) {
    pinMode(PIN_BTN_ORDER[i], INPUT_PULLUP);
  }
  pinMode(PIN_BTN_START, INPUT_PULLUP);
  pinMode(PIN_BTN_TEMPO, INPUT_PULLUP);
  pinMode(PIN_BTN_RESET, INPUT_PULLUP);
  pinMode(LED_INDICATOR, OUTPUT);

  printHelp();
}

void loop() {
  handleSerialCommand();
  handleOrderButtons();
  handleResetButton();
  handleTempoButton();
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

  long boundary = ((tickCount / QUANTIZE_TICKS) + 1) * QUANTIZE_TICKS;

  if (isActive[i]) {
    if (leaveTick[i] != -1) {
      leaveTick[i] = -1;
      Serial.print("child ");
      Serial.print(i);
      Serial.println(" leave cancelled (stay)");
    } else {
      leaveTick[i] = boundary;
      Serial.print("child ");
      Serial.print(i);
      Serial.print(" will leave at tick ");
      Serial.println(leaveTick[i]);
    }
  } else {
    if (joinTick[i] != -1) {
      joinTick[i] = -1;
      Serial.print("child ");
      Serial.print(i);
      Serial.println(" join cancelled");
    } else {
      joinTick[i] = boundary;
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

void doTempoChange() {
  tempoBpm += 30;
  if (tempoBpm > 150) tempoBpm = 60;
  Serial.print("tempo=");
  Serial.println(tempoBpm);
}

void handleTempoButton() {
  int s = digitalRead(PIN_BTN_TEMPO);
  if (prevBtnTempo == HIGH && s == LOW) {
    doTempoChange();
    delay(30);
  }
  prevBtnTempo = s;
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
    } else if (c == 't' || c == 'T') {
      doTempoChange();
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
  Serial.println("t    : tempo cycle (60/90/120/150)");
  Serial.println("?    : help");
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
      Serial.print("child ");
      Serial.print(i);
      Serial.println(" joined");
    }
    // 退場予約していたtickに到達したら無効化 (このtickからは送らない)
    if (isActive[i] && leaveTick[i] != -1 && tickCount >= leaveTick[i]) {
      isActive[i] = false;
      leaveTick[i] = -1;
      Serial.print("child ");
      Serial.print(i);
      Serial.println(" left");
    }
    if (isActive[i]) {
      mask |= (uint8_t)(1 << i);
    }
  }

  if (mask != 0) {
    // address に tickCount の下位8bit を乗せて送る。
    // 子機側はこれを「正解の位置」として受け取り、自分のlocalPosを補正する
    // (IR受信失敗による同期ずれを毎tick自己修復できる)。
    uint8_t tickLow = (uint8_t)(tickCount & 0xFF);
    IrSender.sendNEC(tickLow, mask, 0);
  }
}

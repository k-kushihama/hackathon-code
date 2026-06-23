#define DECODE_NEC
#define IR_SEND_PIN 3
#include <IRremote.hpp>

// 単一ブロードキャスト方式 (NEC):
//   address[15:8] = cyclePos     (0..TICK_LENGTH-1)
//   address[7:0]  = enabledMask  (bit i = 子機 i が今このtickで鳴るか)
//   command[7:6]  = configIdx    (このフレームで設定を送る子機の番号 0..3)
//   command[5:0]  = canonOffset  (configIdx の子機の遅延値 0..63)

const int NUM_CHILDREN = 4;

// 1ループ = メロディ31tick + 末尾休符1tick = 32tick (LOOP_REST_TICKS で調整可)
const int MELODY_TICK_LENGTH = 31;
const int LOOP_REST_TICKS    = 1;
const int TICK_LENGTH        = MELODY_TICK_LENGTH + LOOP_REST_TICKS;

// 押下順N番目に割り当てるカノン参加tick。
// 1番目に押された子機 → 0 (リードとして最初から)、2番目 → 8、3番目 → 16、4番目 → 20。
// どのボタン(D4~D7=child0~3)を最初に押しても、その子機がpos0から流れ出すように動的割当する。
const int CANON_ENTRY_BY_ORDER[NUM_CHILDREN] = {0, 8, 16, 20};

const int DRUM_CHILD_ID = 3;
const int DRUM_INTERVAL_TICKS = 2;

const int PIN_BTN_CHILD[NUM_CHILDREN] = {4, 5, 6, 7};
// 各子機ボタン横のLED (D4→A0, D5→A1, D6→A2, D7→A3)。トグル状態を可視化する。
const int PIN_LED_CHILD[NUM_CHILDREN] = {A0, A1, A2, A3};
const int PIN_BTN_START = 8;
const int PIN_BTN_TEMPO = 9;
const int PIN_BTN_RESET = 10;
const int LED_INDICATOR = 13;

// canonOffset: 参加中の遅延値、-1 は不参加
// pendingOffset: 予約済み。cyclePos が一致した瞬間に canonOffset へ昇格する。
// pressOrder: 押下順 (0=1番目, 1=2番目, ...)。-1=未押下。Off時にリセット。
int  canonOffset[NUM_CHILDREN]   = {-1, -1, -1, -1};
int  pendingOffset[NUM_CHILDREN] = {-1, -1, -1, -1};
int  pressOrder[NUM_CHILDREN]    = {-1, -1, -1, -1};
int  globalPressCount = 0;

bool loopOn   = true;
bool isPlaying = false;
long parentTick = 0;
unsigned long lastTickMs = 0;
int  tempoBpm = 120;

// stable=確定済みの値、lastRaw=直近の生値、lastChangeMs=生値が変化した時刻。
// 長押し中の瞬間バウンス(LOW→HIGH→LOW)でエッジを再検出しないよう、
// 「同じ生値がBTN_DEBOUNCE_MS以上続いたら確定状態を更新する」方式にする。
int stableBtnChild[NUM_CHILDREN] = {HIGH, HIGH, HIGH, HIGH};
int lastRawBtnChild[NUM_CHILDREN] = {HIGH, HIGH, HIGH, HIGH};
unsigned long lastBtnChangeMs[NUM_CHILDREN] = {0, 0, 0, 0};
const unsigned long BTN_DEBOUNCE_MS = 50;
int prevBtnStart = HIGH;
int prevBtnTempo = HIGH;
int prevBtnReset = HIGH;

void setup() {
  Serial.begin(115200);
  IrSender.begin(IR_SEND_PIN);
  for (int i = 0; i < NUM_CHILDREN; i++) {
    pinMode(PIN_BTN_CHILD[i], INPUT_PULLUP);
    pinMode(PIN_LED_CHILD[i], OUTPUT);
    digitalWrite(PIN_LED_CHILD[i], LOW);
  }
  pinMode(PIN_BTN_START, INPUT_PULLUP);
  pinMode(PIN_BTN_TEMPO, INPUT_PULLUP);
  pinMode(PIN_BTN_RESET, INPUT_PULLUP);
  pinMode(LED_INDICATOR, OUTPUT);
  digitalWrite(LED_INDICATOR, LOW);
  delay(100);
  printHelp();
}

void loop() {
  handleSerialCommand();
  handleChildButtons();
  handleStartButton();
  handleTempoButton();
  handleResetButton();

  if (isPlaying) {
    unsigned long now = millis();
    unsigned long interval = 60000UL / (unsigned long)tempoBpm / 2UL;  // 8分音符1個ぶんの時間
    if (now - lastTickMs >= interval) {
      lastTickMs = now;
      sendTickBroadcast();
      parentTick++;
    }
  }
}

void sendTickBroadcast() {
  long cyclePos = parentTick % TICK_LENGTH;
  if (!loopOn && parentTick >= MELODY_TICK_LENGTH) return;

  digitalWrite(LED_INDICATOR, (parentTick % 2) ? HIGH : LOW);

  // 予約された子機を cyclePos に到達した瞬間に活性化する
  for (int i = 0; i < NUM_CHILDREN; i++) {
    if (pendingOffset[i] >= 0 && (int)cyclePos == pendingOffset[i]) {
      canonOffset[i] = pendingOffset[i];
      pendingOffset[i] = -1;
      Serial.print("[child ");
      Serial.print(i);
      Serial.print("] ACTIVATED at cyclePos=");
      Serial.println(cyclePos);
    }
  }

  // 各子機の鳴り状態を mask に集約 (per-voice REST)
  uint8_t enabledMask = 0;
  for (int i = 0; i < NUM_CHILDREN; i++) {
    if (canonOffset[i] < 0) continue;
    long localPos = (cyclePos - canonOffset[i] + TICK_LENGTH) % TICK_LENGTH;
    if (localPos >= MELODY_TICK_LENGTH) continue;
    if (i == DRUM_CHILD_ID) {
      if (localPos % DRUM_INTERVAL_TICKS == 0) enabledMask |= (1 << i);
    } else {
      enabledMask |= (1 << i);
    }
  }

  // 設定送信のラウンドロビン
  int configIdx = -1;
  for (int r = 0; r < NUM_CHILDREN; r++) {
    int idx = ((int)(parentTick % NUM_CHILDREN) + r) % NUM_CHILDREN;
    if (canonOffset[idx] >= 0) { configIdx = idx; break; }
  }
  if (configIdx < 0) return;

  uint8_t offsetEncoded = (uint8_t)(canonOffset[configIdx] & 0x3F);
  uint8_t cmd = (((uint8_t)configIdx & 0x03) << 6) | offsetEncoded;
  uint16_t addr = ((uint16_t)(cyclePos & 0xFF) << 8) | (uint16_t)enabledMask;
  IrSender.sendNEC(addr, cmd, 0);
}

void toggleChild(int i) {
  if (i < 0 || i >= NUM_CHILDREN) return;
  if (canonOffset[i] >= 0) {
    canonOffset[i] = -1;
    pressOrder[i] = -1;
    Serial.print("[child ");
    Serial.print(i);
    Serial.println("] OFF (active stopped)");
  } else if (pendingOffset[i] >= 0) {
    pendingOffset[i] = -1;
    pressOrder[i] = -1;
    Serial.print("[child ");
    Serial.print(i);
    Serial.println("] OFF (pending canceled)");
  } else {
    // 現在使われていない最小の押下順番号を割り当てる(0=リード, 1=2番目..)。
    int order = 0;
    while (order < NUM_CHILDREN) {
      bool used = false;
      for (int j = 0; j < NUM_CHILDREN; j++) {
        if (pressOrder[j] == order) { used = true; break; }
      }
      if (!used) break;
      order++;
    }
    if (order >= NUM_CHILDREN) order = NUM_CHILDREN - 1;
    pressOrder[i] = order;
    pendingOffset[i] = CANON_ENTRY_BY_ORDER[order];
    globalPressCount++;
    Serial.print("[child ");
    Serial.print(i);
    Serial.print("] RESERVED order=");
    Serial.print(order);
    Serial.print(" entry=");
    Serial.print(pendingOffset[i]);
    Serial.print(" total#");
    Serial.println(globalPressCount);
  }
  // active/pending いずれかが立っていれば点灯、両方解除なら消灯
  bool on = (canonOffset[i] >= 0) || (pendingOffset[i] >= 0);
  digitalWrite(PIN_LED_CHILD[i], on ? HIGH : LOW);
}

void startPlay() {
  if (isPlaying) {
    Serial.println("[!] already playing");
    return;
  }
  // 子機ボタン(D4~D7)が1つも押されていない=全員inactiveなら開始しない
  bool anySelected = false;
  for (int i = 0; i < NUM_CHILDREN; i++) {
    if (canonOffset[i] >= 0 || pendingOffset[i] >= 0) { anySelected = true; break; }
  }
  if (!anySelected) {
    Serial.println("[!] no child selected -- press D4-D7 first");
    return;
  }
  isPlaying = true;
  parentTick = 0;
  lastTickMs = millis() - (60000UL / (unsigned long)tempoBpm / 2UL);
  Serial.print("[start] BPM=");
  Serial.println(tempoBpm);
}

void resetAll() {
  isPlaying = false;
  parentTick = 0;
  for (int i = 0; i < NUM_CHILDREN; i++) {
    canonOffset[i] = -1;
    pendingOffset[i] = -1;
    pressOrder[i] = -1;
    digitalWrite(PIN_LED_CHILD[i], LOW);
  }
  globalPressCount = 0;
  digitalWrite(LED_INDICATOR, LOW);
  Serial.println("[reset]");
}

void handleChildButtons() {
  unsigned long now = millis();
  for (int i = 0; i < NUM_CHILDREN; i++) {
    int s = digitalRead(PIN_BTN_CHILD[i]);
    if (s != lastRawBtnChild[i]) {
      lastRawBtnChild[i] = s;
      lastBtnChangeMs[i] = now;
    }
    // 生値が安定して BTN_DEBOUNCE_MS 続いたら確定状態を更新。
    // これにより長押し中の瞬間バウンスでは stableBtnChild が HIGH に戻らず、再発火しない。
    if ((now - lastBtnChangeMs[i]) > BTN_DEBOUNCE_MS && s != stableBtnChild[i]) {
      stableBtnChild[i] = s;
      if (s == LOW) toggleChild(i);
    }
  }
}

void handleStartButton() {
  int s = digitalRead(PIN_BTN_START);
  if (prevBtnStart == HIGH && s == LOW) {
    startPlay();
    delay(30);
  }
  prevBtnStart = s;
}

void handleResetButton() {
  int s = digitalRead(PIN_BTN_RESET);
  if (prevBtnReset == HIGH && s == LOW) {
    resetAll();
    delay(30);
  }
  prevBtnReset = s;
}

void handleTempoButton() {
  int s = digitalRead(PIN_BTN_TEMPO);
  if (prevBtnTempo == HIGH && s == LOW) {
    tempoBpm += 30;
    if (tempoBpm > 150) tempoBpm = 60;
    Serial.print("[tempo] BPM=");
    Serial.println(tempoBpm);
    delay(30);
  }
  prevBtnTempo = s;
}

void handleSerialCommand() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\r' || c == '\n' || c == ' ') continue;
    if (c >= '0' && c <= ('0' + NUM_CHILDREN - 1)) {
      toggleChild(c - '0');
    } else if (c == 's' || c == 'S') {
      startPlay();
    } else if (c == 'r' || c == 'R') {
      resetAll();
    } else if (c == 't' || c == 'T') {
      tempoBpm += 30;
      if (tempoBpm > 150) tempoBpm = 60;
      Serial.print("[tempo] BPM=");
      Serial.println(tempoBpm);
    } else if (c == 'l' || c == 'L') {
      loopOn = !loopOn;
      Serial.print("[loop] ");
      Serial.println(loopOn ? "ON" : "OFF");
    } else if (c == '?') {
      printHelp();
    }
  }
}

void printHelp() {
  Serial.println("=== hack-oya help ===");
  Serial.println("0..3 : toggle child on/off (entryは押下順に動的割当: 1st=0, 2nd=8, 3rd=16, 4th=20)");
  Serial.println("s    : start");
  Serial.println("r    : reset");
  Serial.println("t    : tempo cycle (60/90/120/150)");
  Serial.println("l    : toggle loop on/off");
  Serial.println("?    : help");
  Serial.print("Current BPM=");
  Serial.print(tempoBpm);
  Serial.print(", loop=");
  Serial.println(loopOn ? "ON" : "OFF");
}

#define DECODE_NEC
#define IR_SEND_PIN 3
#include <IRremote.hpp>

const int NUM_CHILDREN = 4;

//const int PIN_BTN_ORDER[NUM_CHILDREN] = {4, 5, 6, 7};
const int PIN_BTN_START = 8;
const int PIN_BTN_TEMPO = 9;
const int PIN_BTN_RESET = 10;
const int LED_INDICATOR = 13;

const int OFFSET_TICKS  = 8;
const uint16_t IR_ADDRESS = 0x00;

int  playOrder[NUM_CHILDREN] = {-1, -1, -1, -1};
int  orderCount = 0;
bool isPlaying  = false;
int  tempoBpm   = 120;
long tickCount  = 0;
unsigned long lastTickMs = 0;

//int prevBtnOrder[NUM_CHILDREN] = {HIGH, HIGH, HIGH, HIGH};
int prevBtnStart = HIGH;
int prevBtnTempo = HIGH;
int prevBtnReset = HIGH;

void setup() {
  Serial.begin(115200);
  IrSender.begin(IR_SEND_PIN);

  //for (int i = 0; i < NUM_CHILDREN; i++) {
  //  pinMode(PIN_BTN_ORDER[i], INPUT_PULLUP);
  //}
  pinMode(PIN_BTN_START, INPUT_PULLUP);
  pinMode(PIN_BTN_TEMPO, INPUT_PULLUP);
  pinMode(PIN_BTN_RESET, INPUT_PULLUP);
  pinMode(LED_INDICATOR, OUTPUT);

  printHelp();
}

void loop() {
  handleSerial();
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

// シリアル入力処理
void handleSerial() {
  if (!Serial.available()) return;

  char c = Serial.read();

  // 0〜3: 順番登録
  if (c >= '0' && c <= '3') {
    if (isPlaying) {
      Serial.println("[!] 再生中は順番を変更できません");
      return;
    }
    int childIdx = c - '0';
    if (inOrder(childIdx)) {
      Serial.print("[!] 子機 ");
      Serial.print(childIdx);
      Serial.println(" はすでに登録されています");
      return;
    }
    if (orderCount >= NUM_CHILDREN) {
      Serial.println("[!] これ以上登録できません（最大4台）");
      return;
    }
    playOrder[orderCount++] = childIdx;
    Serial.print("順番登録: ");
    for (int k = 0; k < orderCount; k++) {
      Serial.print(playOrder[k]);
      if (k < orderCount - 1) Serial.print(" -> ");
    }
    Serial.println();
  }

  // s: 再生開始
  else if (c == 's' || c == 'S') {
    if (isPlaying) {
      Serial.println("[!] すでに再生中です");
    } else if (orderCount == 0) {
      Serial.println("[!] 子機が1台も登録されていません");
    } else {
      isPlaying  = true;
      tickCount  = 0;
      lastTickMs = millis() - (60000UL / (unsigned long)tempoBpm / 2UL);
      Serial.print("再生開始 (BPM=");
      Serial.print(tempoBpm);
      Serial.println(")");
    }
  }

  // r: リセット
  else if (c == 'r' || c == 'R') {
    isPlaying  = false;
    orderCount = 0;
    for (int i = 0; i < NUM_CHILDREN; i++) playOrder[i] = -1;
    tickCount = 0;
    digitalWrite(LED_INDICATOR, LOW);
    Serial.println("リセットしました");
  }

  // t: テンポ変更
  else if (c == 't' || c == 'T') {
    tempoBpm += 30;
    if (tempoBpm > 150) tempoBpm = 60;
    Serial.print("テンポ変更: BPM=");
    Serial.println(tempoBpm);
  }

  // ?: ヘルプ表示
  else if (c == '?') {
    printHelp();
  }
}

// ─── ヘルプ表示 ──────────────────────────────────────────────────
void printHelp() {
  Serial.println("============================");
  Serial.println("  hack-oya シリアルコマンド");
  Serial.println("============================");
  Serial.println("  0〜3 : 子機を順番に登録");
  Serial.println("  s    : 再生開始");
  Serial.println("  r    : リセット");
  Serial.println("  t    : テンポ変更 (60/90/120/150)");
  Serial.println("  ?    : このヘルプを表示");
  Serial.println("============================");
  Serial.print("現在のテンポ: BPM=");
  Serial.println(tempoBpm);
}

// ボタン
void handleResetButton() {
  int s = digitalRead(PIN_BTN_RESET);
  if (prevBtnReset == HIGH && s == LOW) {
    isPlaying  = false;
    orderCount = 0;
    for (int i = 0; i < NUM_CHILDREN; i++) playOrder[i] = -1;
    tickCount = 0;
    digitalWrite(LED_INDICATOR, LOW);
    Serial.println("リセットしました");
    delay(30);
  }
  prevBtnReset = s;
}

void handleTempoButton() {
  int s = digitalRead(PIN_BTN_TEMPO);
  if (prevBtnTempo == HIGH && s == LOW) {
    tempoBpm += 30;
    if (tempoBpm > 150) tempoBpm = 60;
    Serial.print("テンポ変更: BPM=");
    Serial.println(tempoBpm);
    delay(30);
  }
  prevBtnTempo = s;
}

void handleStartButton() {
  int s = digitalRead(PIN_BTN_START);
  if (prevBtnStart == HIGH && s == LOW) {
    if (!isPlaying && orderCount > 0) {
      isPlaying  = true;
      tickCount  = 0;
      lastTickMs = millis() - (60000UL / (unsigned long)tempoBpm / 2UL);
      Serial.print("再生開始 (BPM=");
      Serial.print(tempoBpm);
      Serial.println(")");
    }
    delay(30);
  }
  prevBtnStart = s;
}

bool inOrder(int childIdx) {
  for (int i = 0; i < orderCount; i++) {
    if (playOrder[i] == childIdx) return true;
  }
  return false;
}

void tick() {
  digitalWrite(LED_INDICATOR, (tickCount % 2) ? HIGH : LOW);

  uint8_t mask = 0;
  for (int i = 0; i < orderCount; i++) {
    int  childIdx       = playOrder[i];
    long childStartTick = (long)i * OFFSET_TICKS;
    if (tickCount >= childStartTick) {
      mask |= (uint8_t)(1 << childIdx);
    }
  }

  if (mask != 0) {
    IrSender.sendNEC(IR_ADDRESS, mask, 0);
  }
}

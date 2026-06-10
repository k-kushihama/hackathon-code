#define DECODE_NEC
#define IR_SEND_PIN 3
#include <IRremote.hpp>

const int NUM_CHILDREN = 4;

const int PIN_BTN_ORDER[NUM_CHILDREN] = {4, 5, 6, 7};
const int PIN_BTN_START = 8;
const int PIN_BTN_TEMPO = 9;
const int PIN_BTN_RESET = 10;
const int LED_INDICATOR       = 13;

const int OFFSET_TICKS  = 8;
const uint16_t IR_ADDRESS = 0x00;

int  playOrder[NUM_CHILDREN] = {-1, -1, -1, -1};
int  orderCount = 0;
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
  digitalWrite(LED_INDICATOR, LOW);

  Serial.println("hack-oya ready: 0-3=order, s=start, r=reset, t=tempo");
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

void handleOrderButtons() {
  if (isPlaying) return;
  for (int i = 0; i < NUM_CHILDREN; i++) {
    int s = digitalRead(PIN_BTN_ORDER[i]);
    if (prevBtnOrder[i] == HIGH && s == LOW) {
      if (orderCount < NUM_CHILDREN && !inOrder(i)) {
        playOrder[orderCount++] = i;
        Serial.print("order: ");
        for (int k = 0; k < orderCount; k++) {
          Serial.print(playOrder[k]);
          Serial.print(" ");
        }
        Serial.println();
      }
      delay(30);
    }
    prevBtnOrder[i] = s;
  }
}

bool inOrder(int childIdx) {
  for (int i = 0; i < orderCount; i++) {
    if (playOrder[i] == childIdx) return true;
  }
  return false;
}

void handleResetButton() {
  int s = digitalRead(PIN_BTN_RESET);
  if (prevBtnReset == HIGH && s == LOW) {
    isPlaying  = false;
    orderCount = 0;
    for (int i = 0; i < NUM_CHILDREN; i++) playOrder[i] = -1;
    tickCount = 0;
    digitalWrite(LED_INDICATOR, LOW);
    Serial.println("reset");
    delay(30);
  }
  prevBtnReset = s;
}

void handleTempoButton() {
  int s = digitalRead(PIN_BTN_TEMPO);
  if (prevBtnTempo == HIGH && s == LOW) {
    tempoBpm += 30;
    if (tempoBpm > 150) tempoBpm = 60;
    Serial.print("tempo=");
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
      Serial.println("start");
    }
    delay(30);
  }
  prevBtnStart = s;
}

void tick() {
  uint8_t mask = 0;
  for (int i = 0; i < orderCount; i++) {
    int  childIdx       = playOrder[i];
    long childStartTick = (long)i * OFFSET_TICKS;
    if (tickCount >= childStartTick) {
      mask |= (uint8_t)(1 << childIdx);
    }
  }

  // 赤外線を実際に送信したタイミングだけLEDを光らせて、IR出力を目視確認できるようにする。
  if (mask != 0) {
    digitalWrite(LED_INDICATOR, HIGH);
    IrSender.sendNEC(IR_ADDRESS, mask, 0);
    digitalWrite(LED_INDICATOR, LOW);
  }
}

// シリアル入力をボタン押下と同じ扱いで処理する。
// '0'..'3' = Order(子機追加), 's'/'S' = Start, 'r'/'R' = Reset, 't'/'T' = Tempo。
// 改行や空白文字は無視するので、シリアルモニタの改行設定はどれでもよい。
void handleSerialCommand() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\r' || c == '\n' || c == ' ') continue;

    if (c >= '0' && c <= ('0' + NUM_CHILDREN - 1)) {
      if (isPlaying) continue;
      int childIdx = c - '0';
      if (orderCount < NUM_CHILDREN && !inOrder(childIdx)) {
        playOrder[orderCount++] = childIdx;
        Serial.print("order(serial): ");
        for (int k = 0; k < orderCount; k++) {
          Serial.print(playOrder[k]);
          Serial.print(" ");
        }
        Serial.println();
      }
    } else if (c == 's' || c == 'S') {
      if (!isPlaying && orderCount > 0) {
        isPlaying  = true;
        tickCount  = 0;
        lastTickMs = millis() - (60000UL / (unsigned long)tempoBpm / 2UL);
        Serial.println("start(serial)");
      }
    } else if (c == 'r' || c == 'R') {
      isPlaying  = false;
      orderCount = 0;
      for (int i = 0; i < NUM_CHILDREN; i++) playOrder[i] = -1;
      tickCount = 0;
      digitalWrite(LED_INDICATOR, LOW);
      Serial.println("reset(serial)");
    } else if (c == 't' || c == 'T') {
      tempoBpm += 30;
      if (tempoBpm > 150) tempoBpm = 60;
      Serial.print("tempo(serial)=");
      Serial.println(tempoBpm);
    }
  }
}

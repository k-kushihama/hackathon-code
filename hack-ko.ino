#define DECODE_NEC
#include <IRremote.hpp>

#define CHILD_ID 0

const int PIN_IR_RECV = 2;
const int LED_INDICATOR = 13;

const int melodyLength = 37;
String myScore[] = {
  "C4", "D4", "E4", "F4", "E4", "D4", "C4", "R",
  "E4", "F4", "G4", "A4", "G4", "F4", "E4", "R",
  "C4", "R",  "C4", "R",  "C4", "R",  "C4", "R",
  "C4", "C4", "D4", "D4", "E4", "E4", "F4", "F4", "E4", "R", "D4", "R", "C4"
};

// idxがこの位置以降は「1音の間に2音」=半音価で発音させる範囲。
// 最後のフレーズ(C4 C4 D4 D4 E4 E4 F4 F4 E4 R D4 R C4)の開始位置。
const int DOUBLE_START = 24;
// 半音価のための連続2音目の遅延。親機tempoBpm=120 → 8分音符250msなので
// その半分125msより少し短い100msで「ほぼ16分音符」相当の連続発音にする。
const unsigned long DOUBLE_DELAY_MS = 100;

int idx = 0;

void setup() {
  Serial.begin(921600);
  IrReceiver.begin(PIN_IR_RECV);
  pinMode(LED_INDICATOR, OUTPUT);
}

void loop() {
  if (IrReceiver.decode()) {
    if (IrReceiver.decodedIRData.protocol == NEC) {
      uint8_t mask = (uint8_t)IrReceiver.decodedIRData.command;
      if (mask & (1 << CHILD_ID)) {
        Serial.println(myScore[idx]);
        digitalWrite(LED_INDICATOR, (idx % 2) ? HIGH : LOW);
        int prevIdx = idx;
        idx++;
        if (idx >= melodyLength) idx = 0;

        // 最後のフレーズ範囲では1IR受信あたり2音まとめて出力する。
        // ラップした場合(idx<=prevIdx)は次フレーズの先頭を巻き込まないようスキップ。
        if (prevIdx >= DOUBLE_START && idx > prevIdx) {
          delay(DOUBLE_DELAY_MS);
          Serial.println(myScore[idx]);
          idx++;
          if (idx >= melodyLength) idx = 0;
        }
      }
    }
    IrReceiver.resume();
  }
}

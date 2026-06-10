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
// 16分音符の正確な間隔。親機tempoBpm=120 → 8分音符250ms → 16分音符125ms。
// 1音目と2音目の間も、2音目と次の1音目(親機の次tickまで=125ms)も
// 均等125ms間隔になり「どど どど」と聴こえる(62.5だと「どどっ」になっていた)。
const unsigned long DOUBLE_DELAY_MS = 125;

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

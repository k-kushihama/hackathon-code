#define DECODE_NEC
#include <IRremote.hpp>

// この子機の役割ID。
// -1: 1台テストモード（メロディ原譜 address=0 のみ鳴らす。カノン/ドラムは捨てる）
//  0: メロディ原譜担当
//  1: メロディのカノン担当（親機が CANON_OFFSET_TICKS だけ遅らせて送る）
//  2: ドラム担当
// 複数台運用時は各機で 0/1/2 のいずれかに書き換えてアップロードする
const int CHILD_ID = -1;

const int PIN_IR_RECV = 2;
const int LED_INDICATOR = 13;

void setup() {
  Serial.begin(921600);
  IrReceiver.begin(PIN_IR_RECV);
  pinMode(LED_INDICATOR, OUTPUT);
}

void loop() {
  if (IrReceiver.decode()) {
    if (IrReceiver.decodedIRData.protocol == NEC) {
      int address = (int)IrReceiver.decodedIRData.address;
      bool accept = (CHILD_ID < 0) ? (address == 0) : (address == CHILD_ID);
      if (accept) {
        uint8_t note = (uint8_t)IrReceiver.decodedIRData.command;
        Serial.println(note);
        digitalWrite(LED_INDICATOR, (note > 0) ? HIGH : LOW);
      }
    }
    IrReceiver.resume();
  }
}

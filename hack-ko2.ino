#define DECODE_NEC
#include <IRremote.hpp>

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
      uint8_t note = (uint8_t)IrReceiver.decodedIRData.command;
      Serial.println(note);
      digitalWrite(LED_INDICATOR, (note > 0) ? HIGH : LOW);
    }
    IrReceiver.resume();
  }
}

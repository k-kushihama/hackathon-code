#define DECODE_NEC
#include <IRremote.hpp>

#define CHILD_ID 2

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
      uint16_t address = IrReceiver.decodedIRData.address;
      if (address == CHILD_ID) {
        uint8_t note = (uint8_t)IrReceiver.decodedIRData.command;
        Serial.println(note);
        digitalWrite(LED_INDICATOR, HIGH);
        delay(15);
        digitalWrite(LED_INDICATOR, LOW);
      }
    }
    IrReceiver.resume();
  }
}

#define DECODE_NEC
#include <IRremote.hpp>

#define CHILD_ID 0

const int PIN_IR_RECV = 2;
const int LED_INDICATOR     = 13;

const int melodyLength = 32;
String myScore[] = {
  "C4", "D4", "E4", "F4", "E4", "D4", "C4", "R",
  "E4", "F4", "G4", "A4", "G4", "F4", "E4", "R",
  "C4", "R",  "C4", "R",  "C4", "R",  "C4", "R",
  "C4", "C4", "D4", "D4", "E4", "D4", "C4", "R"
};

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
        idx++;
        if (idx >= melodyLength) idx = 0;
        digitalWrite(LED_INDICATOR, (idx % 2) ? HIGH : LOW);
      }
    }
    IrReceiver.resume();
  }
}

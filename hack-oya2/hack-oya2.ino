#define DECODE_NEC
#define IR_SEND_PIN 3
#include <IRremote.hpp>

const int LED_INDICATOR = 13;
const int tempoBpm = 90;

String melody[] = {
  "C4", "D4", "E4", "F4", "E4", "D4", "C4", "R",
  "E4", "F4", "G4", "A4", "G4", "F4", "E4", "R",
  "C4", "C4", "C4", "C4",
  "C4", "C4", "D4", "D4", "E4", "E4", "F4", "F4", "E4", "D4", "C4", "R"
};

const int durations[] = {
  2, 2, 2, 2, 2, 2, 2, 2,
  2, 2, 2, 2, 2, 2, 2, 2,
  4, 4, 4, 4,
  1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2
};

const int melodyLength = sizeof(melody) / sizeof(melody[0]);

int idx = 0;
unsigned long lastTickMs = 0;
unsigned long lastNoteDuration = 0;
uint16_t tickCount = 0;

uint8_t pitchToMidi(const String& pitch) {
  if (pitch.length() < 2) return 0;
  char noteChar = pitch.charAt(0);
  if (noteChar == 'R') return 0;
  int noteOffset;
  switch (noteChar) {
    case 'C': noteOffset = 0; break;
    case 'D': noteOffset = 2; break;
    case 'E': noteOffset = 4; break;
    case 'F': noteOffset = 5; break;
    case 'G': noteOffset = 7; break;
    case 'A': noteOffset = 9; break;
    case 'B': noteOffset = 11; break;
    default: return 0;
  }
  int octavePos = 1;
  if (pitch.charAt(1) == '#') {
    noteOffset++;
    octavePos = 2;
  } else if (pitch.charAt(1) == 'b') {
    noteOffset--;
    octavePos = 2;
  }
  if (octavePos >= (int)pitch.length()) return 0;
  int octave = pitch.charAt(octavePos) - '0';
  return (uint8_t)((octave + 1) * 12 + noteOffset);
}

unsigned long noteIntervalMs(int i) {
  unsigned long sixteenth = 60000UL / (unsigned long)tempoBpm / 4UL;
  return sixteenth * (unsigned long)durations[i];
}

void setup() {
  Serial.begin(115200);
  IrSender.begin(IR_SEND_PIN);
  pinMode(LED_INDICATOR, OUTPUT);
  delay(1000);
  lastTickMs = millis();
  Serial.println("hack-oya2 start");
}

void loop() {
  unsigned long now = millis();
  if (now - lastTickMs >= lastNoteDuration) {
    digitalWrite(LED_INDICATOR, (tickCount % 2) ? HIGH : LOW);
    uint8_t note = pitchToMidi(melody[idx]);
    IrSender.sendNEC(tickCount, note, 0);
    Serial.print("tx tick=");
    Serial.print(tickCount);
    Serial.print(" idx=");
    Serial.print(idx);
    Serial.print(" pitch=");
    Serial.print(melody[idx]);
    Serial.print(" dur=");
    Serial.print(durations[idx]);
    Serial.print(" midi=");
    Serial.println(note);
    lastNoteDuration = noteIntervalMs(idx);
    idx++;
    if (idx >= melodyLength) idx = 0;
    tickCount++;
    lastTickMs = now;
  }
}

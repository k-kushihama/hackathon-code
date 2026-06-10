#define DECODE_NEC
#define IR_SEND_PIN 3
#include <IRremote.hpp>

const int LED_INDICATOR = 13;
int tempoBpm = 90;

const int NUM_CHILDREN = 3;
const int DRUM_CHILD_ID = 2;
const int CANON_OFFSET_TICKS = 16;
const unsigned long IR_GAP_MS = 30;

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

const uint8_t DRUM_KICK = 36;
const uint8_t DRUM_CYMBAL = 49;
const uint8_t drumPattern[] = { DRUM_KICK, DRUM_KICK, DRUM_KICK, DRUM_KICK };
const int drumPatternLen = sizeof(drumPattern) / sizeof(drumPattern[0]);

int idx[NUM_CHILDREN] = {0, 0, 0};
unsigned long lastTickMs[NUM_CHILDREN] = {0, 0, 0};
unsigned long lastNoteDuration[NUM_CHILDREN] = {0, 0, 0};
unsigned long lastIrEndMs = 0;
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
  if (pitch.charAt(1) == '#') { noteOffset++; octavePos = 2; }
  else if (pitch.charAt(1) == 'b') { noteOffset--; octavePos = 2; }
  if (octavePos >= (int)pitch.length()) return 0;
  int octave = pitch.charAt(octavePos) - '0';
  return (uint8_t)((octave + 1) * 12 + noteOffset);
}

unsigned long sixteenthMs() {
  return 60000UL / (unsigned long)tempoBpm / 4UL;
}

unsigned long noteIntervalMs(int i) {
  return sixteenthMs() * (unsigned long)durations[i];
}

unsigned long quarterMs() {
  return sixteenthMs() * 4UL;
}

void waitGap() {
  while (millis() - lastIrEndMs < IR_GAP_MS) { }
}

void sendPiano(int c) {
  waitGap();
  uint8_t note = pitchToMidi(melody[idx[c]]);
  IrSender.sendNEC((uint16_t)c, note, 0);
  lastIrEndMs = millis();
  Serial.print("tx child=");
  Serial.print(c);
  Serial.print(" idx=");
  Serial.print(idx[c]);
  Serial.print(" pitch=");
  Serial.print(melody[idx[c]]);
  Serial.print(" midi=");
  Serial.println(note);
}

void sendDrum() {
  waitGap();
  uint8_t note = drumPattern[idx[DRUM_CHILD_ID] % drumPatternLen];
  IrSender.sendNEC((uint16_t)DRUM_CHILD_ID, note, 0);
  lastIrEndMs = millis();
  Serial.print("tx drum idx=");
  Serial.print(idx[DRUM_CHILD_ID]);
  Serial.print(" midi=");
  Serial.println(note);
}

void setup() {
  Serial.begin(115200);
  IrSender.begin(IR_SEND_PIN);
  pinMode(LED_INDICATOR, OUTPUT);
  delay(1000);
  unsigned long now = millis();
  for (int c = 0; c < NUM_CHILDREN; c++) {
    lastTickMs[c] = now;
  }
  lastNoteDuration[0] = 0;
  lastNoteDuration[1] = (unsigned long)CANON_OFFSET_TICKS * sixteenthMs();
  lastNoteDuration[DRUM_CHILD_ID] = 0;
  lastIrEndMs = now;
  Serial.println("hack-oya2 start");
}

void loop() {
  unsigned long now = millis();

  for (int c = 0; c < 2; c++) {
    if (now - lastTickMs[c] >= lastNoteDuration[c]) {
      sendPiano(c);
      lastNoteDuration[c] = noteIntervalMs(idx[c]);
      idx[c]++;
      if (idx[c] >= melodyLength) idx[c] = 0;
      lastTickMs[c] = now;
      tickCount++;
      digitalWrite(LED_INDICATOR, (tickCount % 2) ? HIGH : LOW);
    }
  }

  if (now - lastTickMs[DRUM_CHILD_ID] >= lastNoteDuration[DRUM_CHILD_ID]) {
    sendDrum();
    lastNoteDuration[DRUM_CHILD_ID] = quarterMs();
    idx[DRUM_CHILD_ID]++;
    lastTickMs[DRUM_CHILD_ID] = now;
    tickCount++;
    digitalWrite(LED_INDICATOR, (tickCount % 2) ? HIGH : LOW);
  }
}

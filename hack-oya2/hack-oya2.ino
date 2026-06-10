#define DECODE_NEC
#define IR_SEND_PIN 3
#include <IRremote.hpp>

// 親機: 1台で動作し、各パート（メロディ原譜 / カノン / ドラム）を
// NECアドレスを変えて多重送信する。子機側は CHILD_ID == address のフレームだけ受ける。
//
// NUM_CHILDREN で動作モードを切り替える:
//   1 -> 1台モード: メロディ原譜 (address=0) のみ送信。カノンもドラムも送らない
//   2 -> 2台モード: メロディ原譜 + カノン (address=1)
//   3 -> 3台モード: メロディ原譜 + カノン + ドラム (address=2)
const int NUM_CHILDREN = 3;

const int LED_INDICATOR = 13;

// 90BPMでは16分音符 ≈ 166ms / NECフレーム ≈ 67ms + 安全マージンで100ms。
// メロディとカノンを同tickに送るには 200ms 必要なので、16分音符連続部 (durations=1)
// では帯域がギリギリ。2台以上モードで途切れる場合は 75 程度まで落とすと安定する。
int tempoBpm = 90;

const int DRUM_CHILD_ID = 2;
const int CANON_OFFSET_TICKS = 16;

// NECフレーム+保護間隔。連続送信時の最低スペーシング。
const unsigned long IR_FRAME_GAP_MS = 30;
// カノンの送信時刻をメロディからずらす位相オフセット。
// NEC 1フレーム送信時間 + 余裕を確保し、同時発火による IR 干渉を構造的に防ぐ。
const unsigned long CANON_PHASE_MS = 100;
// ドラムをさらに後ろにずらして三重衝突を避ける。
const unsigned long DRUM_PHASE_MS = 200;

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

// パート別の進行状態。0=メロディ原譜, 1=カノン, 2=ドラム。
int idx[3] = {0, 0, 0};
unsigned long lastTickMs[3] = {0, 0, 0};
unsigned long lastNoteDuration[3] = {0, 0, 0};
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
  while (millis() - lastIrEndMs < IR_FRAME_GAP_MS) { }
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
  Serial.begin(500000);
  IrSender.begin(IR_SEND_PIN);
  pinMode(LED_INDICATOR, OUTPUT);
  delay(1000);

  unsigned long now = millis();
  // 子0(メロディ原譜)は即時開始。
  lastTickMs[0] = now;
  lastNoteDuration[0] = 0;

  // 子1(カノン)は CANON_OFFSET_TICKS だけ後ろに、さらに CANON_PHASE_MS だけ
  // 位相シフトして「子0と同時刻に発火しない」よう構造的に分離する。
  lastTickMs[1] = now;
  lastNoteDuration[1] =
      (unsigned long)CANON_OFFSET_TICKS * sixteenthMs() + CANON_PHASE_MS;

  // 子2(ドラム)はさらに後ろに位相シフト。
  lastTickMs[DRUM_CHILD_ID] = now;
  lastNoteDuration[DRUM_CHILD_ID] = DRUM_PHASE_MS;

  lastIrEndMs = now;

  Serial.print("hack-oya2 start NUM_CHILDREN=");
  Serial.println(NUM_CHILDREN);
}

void loop() {
  unsigned long now = millis();

  // メロディ原譜とカノンは独立のスケジューラで動かす。
  // lastTickMs は「理想時刻」で進めるため、waitGap や sendNEC のブロッキング分が
  // 後続tickに累積しない。これで「だんだん遅れる→途切れる」現象を抑える。
  int pianoChildren = (NUM_CHILDREN >= 2) ? 2 : 1;
  for (int c = 0; c < pianoChildren; c++) {
    if (now - lastTickMs[c] >= lastNoteDuration[c]) {
      sendPiano(c);
      lastTickMs[c] += lastNoteDuration[c];
      lastNoteDuration[c] = noteIntervalMs(idx[c]);
      idx[c]++;
      if (idx[c] >= melodyLength) idx[c] = 0;
      tickCount++;
      digitalWrite(LED_INDICATOR, (tickCount % 2) ? HIGH : LOW);
    }
  }

  if (NUM_CHILDREN >= 3) {
    if (now - lastTickMs[DRUM_CHILD_ID] >= lastNoteDuration[DRUM_CHILD_ID]) {
      sendDrum();
      lastTickMs[DRUM_CHILD_ID] += lastNoteDuration[DRUM_CHILD_ID];
      lastNoteDuration[DRUM_CHILD_ID] = quarterMs();
      idx[DRUM_CHILD_ID]++;
      tickCount++;
      digitalWrite(LED_INDICATOR, (tickCount % 2) ? HIGH : LOW);
    }
  }
}

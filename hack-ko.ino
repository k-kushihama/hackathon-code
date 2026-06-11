#define DECODE_NEC
#include <IRremote.hpp>

// 0,1,2 = メロディ機(myScoreを順に再生する輪唱パート)
// 3     = ドラム機(輪唱せず、受信ごとに固定のキック音を出す)
#define CHILD_ID 0

const int DRUM_CHILD_ID = 3;
// ドラム機が叩くキック相当の低音。Processing側のピアノ音色でも低音は
// 打楽器のように聴こえる。本格的なドラム音にするにはhack-ko.pde側で
// 専用Instrumentを追加する必要がある。
const String DRUM_NOTE = "C2";

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

int idx = 0;

// 直前のIR受信時刻と直近の受信間隔。親機テンポ(BPM)に依存せず
// 「次の受信予測時刻までの半分」で2音目を出すために動的計測する。
unsigned long lastReceiveMs = 0;
unsigned long lastIntervalMs = 250;  // 親機tempoBpm=120相当の初期推定

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
        if (CHILD_ID == DRUM_CHILD_ID) {
          // ドラム機: 親機が四分音符ごとに送ってくる前提なので、
          // 受信のたびに固定キック音を1発出す(idxは進めない)。
          Serial.println(DRUM_NOTE);
          digitalWrite(LED_INDICATOR, HIGH);
        } else {
          // メロディ機: 親機テンポを動的に追従するため受信間隔を更新する。
          // 妥当な範囲(50ms〜2s)のみ採用し、Reset/Startの長い空白で
          // 巨大値になるのを弾く。
          unsigned long now = millis();
          if (lastReceiveMs != 0) {
            unsigned long interval = now - lastReceiveMs;
            if (interval >= 50 && interval <= 2000) {
              lastIntervalMs = interval;
            }
          }
          lastReceiveMs = now;

          Serial.println(myScore[idx]);
          digitalWrite(LED_INDICATOR, (idx % 2) ? HIGH : LOW);
          int prevIdx = idx;
          idx++;
          if (idx >= melodyLength) idx = 0;

          // 最後のフレーズ範囲では1IR受信あたり2音まとめて出力する。
          // 遅延は親機の直近テンポの半分で、1音目→2音目と2音目→次の1音目を
          // 均等にし「どど どど」(16分音符相当)で聴こえるようにする。
          // ラップした場合(idx<=prevIdx)は次フレーズの先頭を巻き込まないようスキップ。
          if (prevIdx >= DOUBLE_START && idx > prevIdx) {
            delay(lastIntervalMs / 2);
            Serial.println(myScore[idx]);
            idx++;
            if (idx >= melodyLength) idx = 0;
          }
        }
      }
    }
    IrReceiver.resume();
  }
}

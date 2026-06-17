#define DECODE_NEC
#include <IRremote.hpp>

// 新方式: 親機は「自分(childid)が今いるべき楽譜のindex」をNECフレームで送ってくる。
// address(16bit)=childid, command(8bit)=localPos。自分宛のフレーム(address==CHILD_ID)
// だけを拾い、command番目の音を発音する。
//
// 利点: 通信ロスで1音抜けても、次に届くフレームには「正しい次のpos」が入っているため、
//       内部カウンタを進める旧方式と違い、子機は自然にカノン位置に復帰できる。

// 書き込む機体ごとに変える: 0,1,2 = メロディ機, 3 = ドラム機。
#define CHILD_ID 0

const int DRUM_CHILD_ID = 3;
const int PIN_IR_RECV = 2;
const int LED_INDICATOR = 13;

// 楽譜は子機が保持する。全メロディ機が同じ楽譜を持ち、
// 親機が割り当てる localPos の違い(=カノン遅延)だけで輪唱が成立する。
const int SCORE_LENGTH = 37;
const String myScore[SCORE_LENGTH] = {
  "C4", "D4", "E4", "F4", "E4", "D4", "C4", "R",
  "E4", "F4", "G4", "A4", "G4", "F4", "E4", "R",
  "C4", "R",  "C4", "R",  "C4", "R",  "C4", "R",
  "C4", "C4", "D4", "D4", "E4", "E4", "F4", "F4",
  "E4", "R",  "D4", "R",  "C4"
};

// index 24 以降は1IR=2音(16分音符相当)で発音する。親機の TICK_LENGTH=31 と整合。
const int DOUBLE_START = 24;

// 音価・音量は子機側で集中管理する (第8週末の変更を踏襲)。
// Processing への通信フォーマット: "ピッチ名,duration秒,amplitude,localPos\n"
// localPos は Processing 側のデバッグ表示用 (発音には使わない)。
const float NOTE_DURATION_DEFAULT = 0.40f;  // 8分音符相当
const float NOTE_DURATION_HALF    = 0.20f;  // 16分音符相当
const float NOTE_AMPLITUDE        = 0.60f;

// ドラム機が drum.pde に送るキック合図。drum.pde は "C2" の1行だけを期待する。
const String DRUM_NOTE = "C2";

// 同じフレームをNECが repeat 送出する/親機が連続送信する場合に二重発音しないための抑止キー。
int lastPlayedPos = -1;

// 親機tick間隔を動的に計測。双子音化区間で 1tick の半分を待つために使う。
unsigned long lastReceiveMs = 0;
unsigned long lastIntervalMs = 250;  // BPM=120 相当の初期推定

void sendNoteToHost(const String& pitch, float duration, float amplitude, int pos) {
  Serial.print(pitch);
  Serial.print(',');
  Serial.print(duration, 3);
  Serial.print(',');
  Serial.print(amplitude, 3);
  Serial.print(',');
  Serial.println(pos);
}

void setup() {
  // 子機 ↔ Processing(hackko.pde / drum.pde) のボーレートは必ず 115200 に揃える。
  Serial.begin(115200);
  IrReceiver.begin(PIN_IR_RECV);
  pinMode(LED_INDICATOR, OUTPUT);
  delay(100);
  Serial.print("hack-ko ready CHILD_ID=");
  Serial.print(CHILD_ID);
  if (CHILD_ID == DRUM_CHILD_ID) {
    Serial.println(" (DRUM)");
  } else {
    Serial.println(" (MELODY)");
  }
}

void loop() {
  if (!IrReceiver.decode()) return;

  if (IrReceiver.decodedIRData.protocol == NEC) {
    uint16_t address = IrReceiver.decodedIRData.address;
    uint8_t  command = IrReceiver.decodedIRData.command;

    // 自分宛のフレームだけを処理する。それ以外の子機宛IRは無視。
    if ((int)address == CHILD_ID) {
      int pos = (int)command;
      if (CHILD_ID == DRUM_CHILD_ID) {
        // ドラム機: pos の中身はキック番号。値が違うフレームが来たら1発鳴らす。
        // 同じ pos が NEC repeat で届いた場合は鳴らさない。
        if (pos != lastPlayedPos) {
          Serial.println(DRUM_NOTE);  // drum.pde 側は "C2" の単独行を期待
          digitalWrite(LED_INDICATOR, HIGH);
          delay(15);
          digitalWrite(LED_INDICATOR, LOW);
          lastPlayedPos = pos;
        }
      } else {
        // メロディ機: pos から楽譜index を決めて発音。
        // pos < DOUBLE_START(24): 1tick=1音(8分)
        // pos >= 24            : 1tick=2音(16分)。scoreIdx = 24 + 2*(pos-24)
        // 1音抜けても次の pos が届けば正しい位置に復帰できる。
        if (pos != lastPlayedPos) {
          // 受信間隔を計測(双子音化区間の半tick遅延に使う)
          unsigned long now = millis();
          if (lastReceiveMs != 0) {
            unsigned long interval = now - lastReceiveMs;
            if (interval >= 50 && interval <= 2000) {
              lastIntervalMs = interval;
            }
          }
          lastReceiveMs = now;

          if (pos >= 0 && pos < DOUBLE_START) {
            sendNoteToHost(myScore[pos], NOTE_DURATION_DEFAULT, NOTE_AMPLITUDE, pos);
            digitalWrite(LED_INDICATOR, (pos % 2) ? HIGH : LOW);
          } else if (pos >= DOUBLE_START) {
            int scoreIdx = DOUBLE_START + 2 * (pos - DOUBLE_START);
            if (scoreIdx < SCORE_LENGTH) {
              sendNoteToHost(myScore[scoreIdx], NOTE_DURATION_HALF, NOTE_AMPLITUDE, pos);
              digitalWrite(LED_INDICATOR, (pos % 2) ? HIGH : LOW);
            }
            if (scoreIdx + 1 < SCORE_LENGTH) {
              delay(lastIntervalMs / 2);
              sendNoteToHost(myScore[scoreIdx + 1], NOTE_DURATION_HALF, NOTE_AMPLITUDE, pos);
            }
          }
          lastPlayedPos = pos;
        }
      }
    }
  }
  IrReceiver.resume();
}

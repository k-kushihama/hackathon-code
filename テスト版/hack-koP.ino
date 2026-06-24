#define DECODE_NEC
#include <IRremote.hpp>

// hack_oya_kai.ino 用プロトコル対応版:
//   IrSender.sendNEC(addr, mask, 0) を受信。
//     addr bit0 = loopMode (1=ループON, 0=ループOFF)
//     command  = mask (子機ID bit)
//   mask の bit(childId) が立っているtickだけ「自分の出番」として
//   localPos をカウントアップし、その位置の音をホストへ送信する。
//   loopMode=ON のとき、localPos が LOOP_LENGTH に達したら 0 に戻して
//   楽譜の頭から繰り返す。loopMode=OFF のときは最後まで行ったら待機する。

// 書き込む機体ごとに値を変えて Upload する: 0,1,2 = メロディ機, 3 = ドラム機。
int childId = 1;
const int DRUM_CHILD_ID = 3;
const int PIN_IR_RECV = 2;
const int LED_INDICATOR = 13;

const int SCORE_LENGTH = 40;
const String myScore[SCORE_LENGTH] = {
  "C4", "D4", "E4", "F4", "E4", "D4", "C4", "R",
  "E4", "F4", "G4", "A4", "G4", "F4", "E4", "R",
  "C4", "R",  "C4", "R",  "C4", "R",  "C4", "R",
  "C4", "C4", "D4", "D4", "E4", "E4", "F4", "F4",
  "E4", "R",  "D4", "R",  "C4", "R" , "R" , "R"
};

// index 24 以降は1IR=2音(16分音符相当)で発音する。
const int DOUBLE_START = 24;

// localPos の有効範囲: 0..LOOP_LENGTH-1
//   0..23  : 1IR=1音 (scoreIdx = localPos)
//   24..31 : 1IR=2音 (scoreIdx = 24,26,28,30,32,34,36,38 → SCORE_LENGTH=40 を使い切る)
// LOOP_LENGTH=32 に達したら 0 に戻して楽譜の頭から繰り返す (loopMode=ON時)。
const int LOOP_LENGTH = 32;

// 親機から通知されるループモード (起動時はON想定。最初の受信で上書きされる)
bool loopMode = true;

// Processing への通信フォーマット: "ピッチ名,duration秒,amplitude,localPos\n"
const float NOTE_DURATION_DEFAULT = 0.40f;
const float NOTE_DURATION_HALF    = 0.20f;
const float NOTE_AMPLITUDE        = 0.60f;

// ドラム機が drum.pde に送るキック合図 (drum.pde は "C2" 単独行を期待)。
const String DRUM_NOTE = "C2";

// 自分の出番が来た回数 (= 親機のtickCountに相当)。受信したtickごとに進む。
long localPos = -1;  // -1 = まだ一度も自分の出番が来ていない

unsigned long lastReceiveMs = 0;
unsigned long lastIntervalMs = 250;  // 双子音化区間の半tick遅延に使う

void sendNoteToHost(const String& pitch, float duration, float amplitude, long pos) {
  Serial.print(pitch);
  Serial.print(',');
  Serial.print(duration, 3);
  Serial.print(',');
  Serial.print(amplitude, 3);
  Serial.print(',');
  Serial.println(pos);
}

void printReady() {
  Serial.print("hack-ko ready CHILD_ID=");
  Serial.print(childId);
  Serial.println(childId == DRUM_CHILD_ID ? " (DRUM)" : " (MELODY)");
}

void setup() {
  Serial.begin(115200);

  // UNO R4 等の native USB CDC は Serial.begin() 直後はホスト未接続のことがある。
  // 最大3秒だけCDC接続完了を待つ (タイムアウトしてもそのまま進む)。
  unsigned long boot_t = millis();
  while (!Serial && (millis() - boot_t) < 3000) { ; }

  IrReceiver.begin(PIN_IR_RECV);
  pinMode(LED_INDICATOR, OUTPUT);

  delay(100);
  printReady();
}

void loop() {
  if (!IrReceiver.decode()) return;

  if (IrReceiver.decodedIRData.protocol == NEC) {
    // hack_oya_kai.ino は command に mask、address bit0 に loopMode を載せて送る。
    uint8_t mask = IrReceiver.decodedIRData.command;
    loopMode = (IrReceiver.decodedIRData.address & 0x01) != 0;

    if (mask & (1 << childId)) {
      // 自分の出番が来た: localPosを1つ進める
      localPos++;
      // メロディ機はループON時に LOOP_LENGTH で wrap (ドラム機は楽譜を持たないので影響なし)
      if (loopMode && childId != DRUM_CHILD_ID && localPos >= LOOP_LENGTH) {
        localPos = 0;
      }

      if (childId == DRUM_CHILD_ID) {
        Serial.println(DRUM_NOTE);
        digitalWrite(LED_INDICATOR, HIGH);
        delay(15);
        digitalWrite(LED_INDICATOR, LOW);
      } else {
        unsigned long now = millis();
        if (lastReceiveMs != 0) {
          unsigned long interval = now - lastReceiveMs;
          if (interval >= 50 && interval <= 2000) lastIntervalMs = interval;
        }
        lastReceiveMs = now;

        if (localPos >= 0 && localPos < DOUBLE_START) {
          sendNoteToHost(myScore[localPos], NOTE_DURATION_DEFAULT, NOTE_AMPLITUDE, localPos);
          digitalWrite(LED_INDICATOR, (localPos % 2) ? HIGH : LOW);
        } else if (localPos >= DOUBLE_START) {
          int scoreIdx = DOUBLE_START + 2 * (int)(localPos - DOUBLE_START);
          if (scoreIdx < SCORE_LENGTH) {
            sendNoteToHost(myScore[scoreIdx], NOTE_DURATION_HALF, NOTE_AMPLITUDE, localPos);
            digitalWrite(LED_INDICATOR, (localPos % 2) ? HIGH : LOW);
          }
          if (scoreIdx + 1 < SCORE_LENGTH) {
            delay(lastIntervalMs / 2);
            sendNoteToHost(myScore[scoreIdx + 1], NOTE_DURATION_HALF, NOTE_AMPLITUDE, localPos);
          }
        }
        // loopMode=OFF のときは楽譜の最後まで行ったらそれ以降は何もしない (待機)
      }
    }
    // 自分のビットが立っていないtickは無視 (待機)
  }
  IrReceiver.resume();
}

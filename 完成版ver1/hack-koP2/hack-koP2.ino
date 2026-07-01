#define DECODE_NEC
#include <IRremote.hpp>

// hack_oya_kai.ino 用プロトコル対応版:
//   IrSender.sendNEC(IR_ADDRESS=0x00, mask, 0) を受信。
//   mask の bit(childId) が立っているtickだけ「自分の出番」として
//   localPos をカウントアップし、その位置の音をホストへ送信する。
//   親機側にループの概念がないが、メロディ機 (childId != DRUM_CHILD_ID)
//   は楽譜の最後まで行ったら localPos を 0 に戻し、自力でループする。
//   (ドラム機は元々楽譜を持たず鳴らし続けるだけなので、この対象外)

// 書き込む機体ごとに値を変えて Upload する: 0,1,2 = メロディ機, 3 = ドラム機。
int childId = 0;
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

// 1周(ループ)に必要なtick数。
//   通常区間: 0 .. DOUBLE_START-1 (1tick=1音)
//   倍速区間: DOUBLE_START .. (1tick=2音) なので tick数は半分になる
// 例) DOUBLE_START=24, SCORE_LENGTH=40 → 24 + (40-24)/2 = 32
const int TOTAL_TICKS = DOUBLE_START + (SCORE_LENGTH - DOUBLE_START) / 2;

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

  // decode直後にデータを退避し、即座にresumeする。
  // delay()中もIR受信を継続させ、tick取りこぼしを防ぐ。
  uint8_t protocol = IrReceiver.decodedIRData.protocol;
  uint8_t mask = (protocol == NEC) ? IrReceiver.decodedIRData.command : 0;
  IrReceiver.resume();

  if (protocol == NEC) {
    if (mask & (1 << childId)) {
      Serial.print("[RECV] millis=");
      Serial.print(millis());
      Serial.print(" child=");
      Serial.print(childId);
      Serial.print(" localPos=");
      Serial.println(localPos + 1);
      localPos++;

      if (childId == DRUM_CHILD_ID) {
        Serial.println(DRUM_NOTE);
        digitalWrite(LED_INDICATOR, HIGH);
        delay(15);
        digitalWrite(LED_INDICATOR, LOW);
      } else {
        if (localPos >= TOTAL_TICKS) {
          localPos = 0;
        }

        unsigned long now = millis();
        if (lastReceiveMs != 0) {
          unsigned long interval = now - lastReceiveMs;
          // 演奏停止→再開時の大きなギャップでdelayが膨張するのを防ぐ
          if (interval >= 50 && interval <= 2000 && interval <= lastIntervalMs * 3) {
            lastIntervalMs = interval;
          }
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
            unsigned long halfDelay = min(lastIntervalMs / 2, 200UL);
            delay(halfDelay);
            sendNoteToHost(myScore[scoreIdx + 1], NOTE_DURATION_HALF, NOTE_AMPLITUDE, localPos);
          }
        }
      }
    }
  }
}

#define DECODE_NEC
#include <IRremote.hpp>

// 単一ブロードキャスト方式 (NEC):
//   address[15:8]=cyclePos, address[7:0]=enabledMask
//   command[7:6]=configIdx, command[5:0]=canonOffset
// localPos = (cyclePos - myOffset) % TICK_LENGTH で発音位置を決める。
// configIdx==childId のとき myOffset を更新する。

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

// 親機の TICK_LENGTH と必ず一致させる。
const int MELODY_TICK_LENGTH = 31;
const int LOOP_REST_TICKS    = 1;
const int TICK_LENGTH        = MELODY_TICK_LENGTH + LOOP_REST_TICKS;

// Processing への通信フォーマット: "ピッチ名,duration秒,amplitude,localPos\n"
const float NOTE_DURATION_DEFAULT = 0.40f;
const float NOTE_DURATION_HALF    = 0.20f;
const float NOTE_AMPLITUDE        = 0.60f;

// ドラム機が drum.pde に送るキック合図 (drum.pde は "C2" 単独行を期待)。
const String DRUM_NOTE = "C2";

int lastPlayedPos = -1;
int myOffset = -1;  // -1 = まだconfig未受信(発音せず待機)

unsigned long lastReceiveMs = 0;
unsigned long lastIntervalMs = 250;  // 双子音化区間の半tick遅延に使う

void sendNoteToHost(const String& pitch, float duration, float amplitude, int pos) {
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
    uint16_t addr = IrReceiver.decodedIRData.address;
    uint8_t  cmd  = IrReceiver.decodedIRData.command;

    int cyclePos    = (int)((addr >> 8) & 0xFF);
    uint8_t mask    = (uint8_t)(addr & 0xFF);
    int configIdx   = (int)((cmd >> 6) & 0x03);
    int configOff   = (int)(cmd & 0x3F);
    bool configValid = (cmd != 0xFF);

    if (configValid && configIdx == childId) {
      myOffset = configOff;
    }

    if (!(mask & (1 << childId))) {
      IrReceiver.resume();
      return;
    }
    if (myOffset < 0) {
      IrReceiver.resume();
      return;
    }

    int localPos = (cyclePos - myOffset + TICK_LENGTH) % TICK_LENGTH;

    if (childId == DRUM_CHILD_ID) {
      if (localPos != lastPlayedPos) {
        Serial.println(DRUM_NOTE);
        digitalWrite(LED_INDICATOR, HIGH);
        delay(15);
        digitalWrite(LED_INDICATOR, LOW);
        lastPlayedPos = localPos;
      }
    } else {
      if (localPos != lastPlayedPos) {
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
          int scoreIdx = DOUBLE_START + 2 * (localPos - DOUBLE_START);
          if (scoreIdx < SCORE_LENGTH) {
            sendNoteToHost(myScore[scoreIdx], NOTE_DURATION_HALF, NOTE_AMPLITUDE, localPos);
            digitalWrite(LED_INDICATOR, (localPos % 2) ? HIGH : LOW);
          }
          if (scoreIdx + 1 < SCORE_LENGTH) {
            delay(lastIntervalMs / 2);
            sendNoteToHost(myScore[scoreIdx + 1], NOTE_DURATION_HALF, NOTE_AMPLITUDE, localPos);
          }
        }
        lastPlayedPos = localPos;
      }
    }
  }
  IrReceiver.resume();
}

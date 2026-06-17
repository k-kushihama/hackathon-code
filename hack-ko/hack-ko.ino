#define DECODE_NEC
#include <IRremote.hpp>
#include <EEPROM.h>

// 新方式: 親機は「自分(childid)が今いるべき楽譜のindex」をNECフレームで送ってくる。
// address(16bit)=childid, command(8bit)=localPos。自分宛のフレーム(address==childId)
// だけを拾い、command番目の音を発音する。
//
// 利点: 通信ロスで1音抜けても、次に届くフレームには「正しい次のpos」が入っているため、
//       内部カウンタを進める旧方式と違い、子機は自然にカノン位置に復帰できる。

// CHILD_ID は EEPROM[0] に保存して起動時にロードする。
// 同じ.inoを全Arduinoに焼き、シリアル経由で個体ごとに違うIDを付ける運用。
// 使い方: シリアルモニタで "id 0" / "id 1" / "id 2" / "id 3" を送信
//         (改行付き)。値はEEPROMに永続保存され、次回起動時もそのまま使われる。
// 未書き込み(0xFF)のEEPROMはデフォルト 0 として扱う。
const int EEPROM_ADDR_CHILD_ID = 0;
int childId = 1;

const int DRUM_CHILD_ID = 3;
const int PIN_IR_RECV = 2;
const int LED_INDICATOR = 13;

// 楽譜は子機が保持する。全メロディ機が同じ楽譜を持ち、
// 親機が割り当てる localPos の違い(=カノン遅延)だけで輪唱が成立する。
const int SCORE_LENGTH = 40;
const String myScore[SCORE_LENGTH] = {
  "C4", "D4", "E4", "F4", "E4", "D4", "C4", "R",
  "E4", "F4", "G4", "A4", "G4", "F4", "E4", "R",
  "C4", "R",  "C4", "R",  "C4", "R",  "C4", "R",
  "C4", "C4", "D4", "D4", "E4", "E4", "F4", "F4",
  "E4", "R",  "D4", "R",  "C4", "R" , "R" , "R"
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

void printReady() {
  Serial.print("hack-ko ready CHILD_ID=");
  Serial.print(childId);
  if (childId == DRUM_CHILD_ID) {
    Serial.println(" (DRUM)");
  } else {
    Serial.println(" (MELODY)");
  }
}

// シリアルから "id N\n" (N=0..3) を受け取ったらEEPROMに保存して反映する。
// 行バッファ方式で改行(\n または \r)が来るまで蓄積する。
void handleSerialIdCommand() {
  static char buf[16];
  static int  bufLen = 0;
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      buf[bufLen] = 0;
      if (bufLen >= 4 && buf[0] == 'i' && buf[1] == 'd' && buf[2] == ' ') {
        int newId = buf[3] - '0';
        if (newId >= 0 && newId <= 3) {
          if (newId != childId) {
            EEPROM.update(EEPROM_ADDR_CHILD_ID, (uint8_t)newId);
            childId = newId;
            lastPlayedPos = -1;  // ID変更後は再発音できるようにキャッシュをリセット
          }
          Serial.print("[id] CHILD_ID=");
          printReady();
        } else {
          Serial.println("[!] usage: id N  (N=0..3)");
        }
      }
      bufLen = 0;
    } else if (bufLen < (int)sizeof(buf) - 1) {
      buf[bufLen++] = c;
    } else {
      bufLen = 0;  // バッファ溢れはリセット
    }
  }
}

void setup() {
  // 子機 ↔ Processing(hackko.pde / drum.pde) のボーレートは必ず 115200 に揃える。
  Serial.begin(115200);
  IrReceiver.begin(PIN_IR_RECV);
  pinMode(LED_INDICATOR, OUTPUT);

  // EEPROMからCHILD_IDを復元。未書き込み(0xFF=255)はソースの初期値(上の
  // `int childId = N;`)をそのまま使う。これにより「ソース編集で初期IDを
  // 仮設定 → シリアル経由で個体ごとに上書き保存」の両方が自然に効く。
  uint8_t stored = EEPROM.read(EEPROM_ADDR_CHILD_ID);
  if (stored <= 3) {
    childId = (int)stored;
  }

  delay(100);
  printReady();
  Serial.println("send 'id N' (N=0..3) over serial to change & save CHILD_ID");
}

void loop() {
  handleSerialIdCommand();

  if (!IrReceiver.decode()) return;

  if (IrReceiver.decodedIRData.protocol == NEC) {
    uint16_t address = IrReceiver.decodedIRData.address;
    uint8_t  command = IrReceiver.decodedIRData.command;

    // 自分宛のフレームだけを処理する。それ以外の子機宛IRは無視。
    if ((int)address == childId) {
      int pos = (int)command;
      if (childId == DRUM_CHILD_ID) {
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

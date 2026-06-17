#define DECODE_NEC
#include <IRremote.hpp>

// 単一ブロードキャスト方式: 親機は1tickあたりIR1フレームだけ送る。
// フレーム内容 (NEC):
//   address[15:8] = cyclePos     (0..TICK_LENGTH-1, 全子機共通の進行位置)
//   address[7:0]  = enabledMask  (bit i = 子機 i が今このtickで鳴るか)
//   command[7:6]  = configIdx    (このフレームで設定を送る子機の番号 0..3)
//   command[5:0]  = canonOffset  (configIdx の子機の遅延値 0..63)
//
// 子機は cyclePos と自分の myOffset から localPos = (cyclePos - myOffset) % TICK_LENGTH
// を計算して発音。configIdx==childId のとき自分の myOffset を更新する。
// 1tick=1フレームなのでIR衝突しない (複数子機同時参加でも干渉しない)。

// 書き込む機体ごとに値を変えて Upload する: 0,1,2 = メロディ機, 3 = ドラム機。
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

// index 24 以降は1IR=2音(16分音符相当)で発音する。
const int DOUBLE_START = 24;

// 親機の TICK_LENGTH と必ず一致させる (= MELODY_TICK_LENGTH 31 + LOOP_REST_TICKS 2)
const int MELODY_TICK_LENGTH = 31;
const int LOOP_REST_TICKS    = 2;
const int TICK_LENGTH        = MELODY_TICK_LENGTH + LOOP_REST_TICKS;

// 音価・音量は子機側で集中管理する (第8週末の変更を踏襲)。
// Processing への通信フォーマット: "ピッチ名,duration秒,amplitude,localPos\n"
// localPos は Processing 側のデバッグ表示用 (発音には使わない)。
const float NOTE_DURATION_DEFAULT = 0.40f;  // 8分音符相当
const float NOTE_DURATION_HALF    = 0.20f;  // 16分音符相当
const float NOTE_AMPLITUDE        = 0.60f;

// ドラム機が drum.pde に送るキック合図。drum.pde は "C2" の1行だけを期待する。
const String DRUM_NOTE = "C2";

// 同じlocalPosの連続再生(NEC repeat等)を抑止するキー。
int lastPlayedPos = -1;

// 親機から受信した自分のcanonOffset値。-1 = 未受信(まだconfigを受け取ってない)。
// 受信前は鳴らさず(myOffset確定を待つ)、受信後はlocalPos計算に使う。
int myOffset = -1;

// 親機tick間隔を動的に計測。双子音化区間で 1tick の半分を待つために使う。
unsigned long lastReceiveMs = 0;
unsigned long lastIntervalMs = 250;  // BPM=120 相当の初期推定

void sendNoteToHost(const String& pitch, float duration, float amplitude, int pos) {
  // Processing が止まる/USBが詰まると Serial.print がブロックして main loop が
  // 凍り、IR受信もできなくなる(="急にできなくなる"症状の主因)。
  // 送信バッファに余裕がないときはこのフレームを捨てる(=1音欠ける程度の被害で済む)。
  // 1行あたり最大20文字程度なので 32 バイト余裕があれば OK。
  if (Serial.availableForWrite() < 32) return;
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

void setup() {
  // 子機 ↔ Processing(hackko.pde / drum.pde) のボーレートは必ず 115200 に揃える。
  Serial.begin(115200);
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

    // 自分宛 config が来たら myOffset を更新
    if (configValid && configIdx == childId) {
      myOffset = configOff;
    }

    // 自分bitが立ってない → このtickは鳴らさない (休符ではなく単に他の子機の番)
    if (!(mask & (1 << childId))) {
      IrReceiver.resume();
      return;
    }

    // offset未受信なら鳴らさない(次のconfigを待つ)
    if (myOffset < 0) {
      IrReceiver.resume();
      return;
    }

    // 自分のlocalPosを計算 (TICK_LENGTHを法に折り返し)
    int localPos = (cyclePos - myOffset + TICK_LENGTH) % TICK_LENGTH;

    if (childId == DRUM_CHILD_ID) {
      // ドラム機: posが変化したら1発鳴らす
      if (localPos != lastPlayedPos) {
        // 同じくバッファ詰まり時はスキップ
        if (Serial.availableForWrite() >= 8) {
          Serial.println(DRUM_NOTE);
        }
        digitalWrite(LED_INDICATOR, HIGH);
        delay(15);
        digitalWrite(LED_INDICATOR, LOW);
        lastPlayedPos = localPos;
      }
    } else {
      // メロディ機: localPos→scoreIdxマッピングして発音
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

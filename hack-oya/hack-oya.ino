#define DECODE_NEC
#define IR_SEND_PIN 3
#include <IRremote.hpp>

// 新方式: 親機は「再生中の場所(localPos)」を子機ごとに送り続ける。
// 各子機は自分の楽譜を持っており、address(=childid)が自分宛のフレームだけを拾って
// command(=localPos)番目の音を鳴らす。1音が通信ロスで抜けても、次のフレームには
// 正しいposが入っているため、子機は自然に「何事もなかったように」演奏に復帰できる。
//
// IR フォーマット (NEC):
//   address (16bit) = childid (0..NUM_CHILDREN-1)
//   command (8bit)  = その子機の楽譜index (localPos)

const int NUM_CHILDREN = 4;

// 子機側の楽譜は SCORE_LENGTH=37 音、うち index 24 以降は 1tick あたり 2 音(16分音符)で
// 発音される(子機が双子音化する)。よって 1 ループの演奏に必要な親機 tick は:
//   24 (通常区間) + ceil((37-24)/2) = 24 + 7 = 31 tick
// loopOn 時はこの後ろに「1拍ぶんの休符」を入れてから次のループへ折り返す。
// 1拍 = 四分音符 = 8分音符2個 = 2 tick (8分音符tick基準)
const int MELODY_TICK_LENGTH = 31;
const int LOOP_REST_TICKS    = 1;
const int TICK_LENGTH        = MELODY_TICK_LENGTH + LOOP_REST_TICKS;  // 33

// 各子機のカノン参加tick (固定割当)。
// child 0 -> entry 0  (リード)
// child 1 -> entry 8  (1-indexed 9配列目相当)
// child 2 -> entry 16 (1-indexed 17配列目)
// child 3 -> entry 20 (1-indexed 21配列目, ドラム機としても運用可)
// ボタン D4=child0, D5=child1, D6=child2, D7=child3 と固定対応するため、
// 「どの順番で押しても D4 は必ずリード、D5 は必ず 8 から、…」となる。
const int CHILD_ENTRY[NUM_CHILDREN] = {0, 8, 16, 20};

// ドラム機(CHILD_ID==3)はカノンには参加せず、四分音符ごと(=2tick)に1発キックする。
const int DRUM_CHILD_ID = 3;
const int DRUM_INTERVAL_TICKS = 2;

// NECフレーム間の最小ギャップ(ms)。これより短いと子機側の受信機が
// 前のフレームを処理しきる前に次のフレームが来て、取りこぼしや
// バッファ破損で子機が音を見失う。
// 35ms→50msに引き上げ(child1再生中にchild0追加でchild1が落ちる症状対策)。
// 受光モジュールの個体差・距離で必要量が変わるため、ノイズが残るなら
// さらに60〜80msまで上げて良い (その場合 BPM を下げる必要あり)。
const int IR_INTERFRAME_DELAY_MS = 50;

const int PIN_BTN_CHILD[NUM_CHILDREN] = {4, 5, 6, 7};
const int PIN_BTN_START = 8;
const int PIN_BTN_TEMPO = 9;
const int PIN_BTN_RESET = 10;
const int LED_INDICATOR = 13;

// canonOffset[i]: 参加中の遅延値(0/8/16/20)、-1 は不参加
// pendingOffset[i]: 「予約済み」の遅延値。cyclePos がこの値に達した瞬間に
//                   canonOffset[i] へ移行し localPos=0 から発音開始する。
//                   ボタン押下時は pending に入れ、即時参加はしない。
int  canonOffset[NUM_CHILDREN]   = {-1, -1, -1, -1};
int  pendingOffset[NUM_CHILDREN] = {-1, -1, -1, -1};
int  globalPressCount = 0;

bool loopOn   = true;        // ループ再生のON/OFF (シリアル 'l' でトグル)
bool isPlaying = false;
long parentTick = 0;            // start時に0、tickごとに+1。
unsigned long lastTickMs = 0;
int  tempoBpm = 120;

int prevBtnChild[NUM_CHILDREN] = {HIGH, HIGH, HIGH, HIGH};
int prevBtnStart = HIGH;
int prevBtnTempo = HIGH;
int prevBtnReset = HIGH;

void setup() {
  Serial.begin(115200);
  IrSender.begin(IR_SEND_PIN);
  for (int i = 0; i < NUM_CHILDREN; i++) {
    pinMode(PIN_BTN_CHILD[i], INPUT_PULLUP);
  }
  pinMode(PIN_BTN_START, INPUT_PULLUP);
  pinMode(PIN_BTN_TEMPO, INPUT_PULLUP);
  pinMode(PIN_BTN_RESET, INPUT_PULLUP);
  pinMode(LED_INDICATOR, OUTPUT);
  digitalWrite(LED_INDICATOR, LOW);
  delay(100);
  printHelp();
}

void loop() {
  handleSerialCommand();
  handleChildButtons();
  handleStartButton();
  handleTempoButton();
  handleResetButton();

  if (isPlaying) {
    unsigned long now = millis();
    unsigned long interval = 60000UL / (unsigned long)tempoBpm / 2UL;  // 8分音符1個ぶんの時間
    if (now - lastTickMs >= interval) {
      lastTickMs = now;
      sendTickBroadcast();   // 単一ブロードキャスト方式に切り替え (per-child は廃止)
      parentTick++;
    }
  }
}

// 1tickごとに、有効な全子機へ localPos を個別に送信する。
// 子機N台が同時に鳴ると N*67ms + (N-1)*50ms かかる。
// 2子機: 2*67 + 50  = 184ms  (BPM=120 tick=250ms OK)
// 3子機: 3*67 + 100 = 301ms  (BPM<=100 推奨)
// 4子機: 4*67 + 150 = 418ms  (BPM<=70 推奨)
void sendTick() {
  digitalWrite(LED_INDICATOR, (parentTick % 2) ? HIGH : LOW);
  bool needGap = false;  // 直前にIRを送ったか。送ったなら次のIR前にgapを入れる。
  for (int i = 0; i < NUM_CHILDREN; i++) {
    if (canonOffset[i] < 0) continue;        // 無効化中
    long relTick = parentTick - canonOffset[i];
    if (relTick < 0) continue;                // まだエントリ点に達していない

    // 1ループぶんの位置 (0..TICK_LENGTH-1)。
    // loopOn なら相対tickをTICK_LENGTHで折り返し、loopOff なら相対tickそのまま。
    // この位置が MELODY_TICK_LENGTH 以上の領域は「ループ後の休符(1拍)」期間で、
    // メロディもドラムも何も送らない (=子機側は無音)。
    long cyclePos;
    if (loopOn) {
      cyclePos = relTick % TICK_LENGTH;
    } else {
      if (relTick >= MELODY_TICK_LENGTH) continue;
      cyclePos = relTick;
    }
    if (cyclePos >= MELODY_TICK_LENGTH) continue;  // ループ間の休符期間

    uint8_t cmd;
    if (i == DRUM_CHILD_ID) {
      // ドラムは4つ打ち。cyclePos基準にすることで毎ループ頭から再アライン。
      if (cyclePos % DRUM_INTERVAL_TICKS != 0) continue;
      // command は重複抑止用キー。下位8bitだけで十分ユニーク。
      cmd = (uint8_t)((cyclePos / DRUM_INTERVAL_TICKS) & 0xFF);
    } else {
      cmd = (uint8_t)cyclePos;
    }

    // 連続送信時のフレーム取りこぼし対策。子機受信機の resume を待つギャップ。
    if (needGap) delay(IR_INTERFRAME_DELAY_MS);
    IrSender.sendNEC((uint16_t)i, cmd, 0);
    needGap = true;
  }
  // (旧 per-child 方式。新 sendTickBroadcast() に置き換え予定)
}

// 単一ブロードキャスト方式の sendTick。1tickあたり IR 1フレームのみ送る。
// フレーム内容 (NEC, 16bit address + 8bit command):
//   address[15:8] = cyclePos     (0..TICK_LENGTH-1, 共通の進行位置)
//   address[7:0]  = enabledMask  (bit i = 子機 i が今このtickで鳴るか)
//   command[7:6]  = configIdx    (このフレームで設定を送る子機の番号 0..3)
//   command[5:0]  = canonOffset  (configIdx の子機の遅延値 0..63)
//
// 各子機は cyclePos と myOffset から localPos を計算して発音する。
// 1tick=1フレームなのでIR衝突が起きず、複数子機同時参加でも干渉ゼロ。
//
// 重要な設計:
// - 予約活性化: cyclePos == pendingOffset[i] になった瞬間に活性化
//   (entry=8 を予約していれば cyclePos=8 で活性化、localPos=0 から発音開始)
// - per-voice REST: 全体ではなく各子機の localPos が
//   MELODY_TICK_LENGTH 以上のときその子機だけ休符。これで voice1 が
//   自分のlocalPos=23,24 を抜け落とす旧バグを解消。
void sendTickBroadcast() {
  long cyclePos = parentTick % TICK_LENGTH;  // 常に 0..TICK_LENGTH-1
  // 非loopモードでは MELODY_TICK_LENGTH を超えたら全停止
  if (!loopOn && parentTick >= MELODY_TICK_LENGTH) return;

  digitalWrite(LED_INDICATOR, (parentTick % 2) ? HIGH : LOW);

  // 予約された子機を、その cyclePos に到達した瞬間に活性化する
  for (int i = 0; i < NUM_CHILDREN; i++) {
    if (pendingOffset[i] >= 0 && (int)cyclePos == pendingOffset[i]) {
      canonOffset[i] = pendingOffset[i];
      pendingOffset[i] = -1;
      Serial.print("[child ");
      Serial.print(i);
      Serial.print("] ACTIVATED at cyclePos=");
      Serial.println(cyclePos);
    }
  }

  // 各子機の鳴り状態を mask に集約 (per-voice REST: 各子機の localPos で判定)
  uint8_t enabledMask = 0;
  for (int i = 0; i < NUM_CHILDREN; i++) {
    if (canonOffset[i] < 0) continue;
    long localPos = (cyclePos - canonOffset[i] + TICK_LENGTH) % TICK_LENGTH;
    if (localPos >= MELODY_TICK_LENGTH) continue;  // この子機は休符中
    if (i == DRUM_CHILD_ID) {
      if (localPos % DRUM_INTERVAL_TICKS == 0) enabledMask |= (1 << i);
    } else {
      enabledMask |= (1 << i);
    }
  }

  // 設定送信のラウンドロビン: 有効な子機を順番に巡回
  int configIdx = -1;
  for (int r = 0; r < NUM_CHILDREN; r++) {
    int idx = ((int)(parentTick % NUM_CHILDREN) + r) % NUM_CHILDREN;
    if (canonOffset[idx] >= 0) {
      configIdx = idx;
      break;
    }
  }

  // configIdx が無い(全子機無効)なら何も送らない
  if (configIdx < 0) return;

  uint8_t offsetEncoded = (uint8_t)(canonOffset[configIdx] & 0x3F);
  uint8_t cmd = (((uint8_t)configIdx & 0x03) << 6) | offsetEncoded;
  uint16_t addr = ((uint16_t)(cyclePos & 0xFF) << 8) | (uint16_t)enabledMask;
  IrSender.sendNEC(addr, cmd, 0);
}

// ボタンを押すたびに on/off をトグルする。固定割当:
//   D4(i=0) -> CHILD_ENTRY[0]=0  (リード)
//   D5(i=1) -> CHILD_ENTRY[1]=8
//   D6(i=2) -> CHILD_ENTRY[2]=16
//   D7(i=3) -> CHILD_ENTRY[3]=20
// OFF -> ON: 該当 entry を「予約」する。cyclePos がその entry に到達した瞬間に
//            活性化して localPos=0 から発音開始。
// ON -> OFF: 参加中なら止める。予約中なら予約を取り消す。
void toggleChild(int i) {
  if (i < 0 || i >= NUM_CHILDREN) return;
  if (canonOffset[i] >= 0) {
    canonOffset[i] = -1;
    Serial.print("[child ");
    Serial.print(i);
    Serial.println("] OFF (active stopped)");
  } else if (pendingOffset[i] >= 0) {
    pendingOffset[i] = -1;
    Serial.print("[child ");
    Serial.print(i);
    Serial.println("] OFF (pending canceled)");
  } else {
    pendingOffset[i] = CHILD_ENTRY[i];
    globalPressCount++;
    Serial.print("[child ");
    Serial.print(i);
    Serial.print("] RESERVED entry=");
    Serial.print(pendingOffset[i]);
    Serial.print(" (waiting for cyclePos=");
    Serial.print(pendingOffset[i]);
    Serial.print(") total#");
    Serial.println(globalPressCount);
  }
}

void startPlay() {
  if (isPlaying) {
    Serial.println("[!] already playing");
    return;
  }
  isPlaying = true;
  parentTick = 0;
  lastTickMs = millis() - (60000UL / (unsigned long)tempoBpm / 2UL);
  Serial.print("[start] BPM=");
  Serial.println(tempoBpm);
}

void resetAll() {
  isPlaying = false;
  parentTick = 0;
  for (int i = 0; i < NUM_CHILDREN; i++) { canonOffset[i] = -1; pendingOffset[i] = -1; }
  globalPressCount = 0;
  digitalWrite(LED_INDICATOR, LOW);
  Serial.println("[reset]");
}

void handleChildButtons() {
  for (int i = 0; i < NUM_CHILDREN; i++) {
    int s = digitalRead(PIN_BTN_CHILD[i]);
    if (prevBtnChild[i] == HIGH && s == LOW) {
      toggleChild(i);
      delay(30);
    }
    prevBtnChild[i] = s;
  }
}

void handleStartButton() {
  int s = digitalRead(PIN_BTN_START);
  if (prevBtnStart == HIGH && s == LOW) {
    startPlay();
    delay(30);
  }
  prevBtnStart = s;
}

void handleResetButton() {
  int s = digitalRead(PIN_BTN_RESET);
  if (prevBtnReset == HIGH && s == LOW) {
    resetAll();
    delay(30);
  }
  prevBtnReset = s;
}

void handleTempoButton() {
  int s = digitalRead(PIN_BTN_TEMPO);
  if (prevBtnTempo == HIGH && s == LOW) {
    tempoBpm += 30;
    if (tempoBpm > 150) tempoBpm = 60;
    Serial.print("[tempo] BPM=");
    Serial.println(tempoBpm);
    delay(30);
  }
  prevBtnTempo = s;
}

// シリアルコマンドは旧仕様(0..3=順番登録)から「0..3=トグル」に変更した。
// 's'/'r'/'t'/'?' は従来通り。
void handleSerialCommand() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\r' || c == '\n' || c == ' ') continue;
    if (c >= '0' && c <= ('0' + NUM_CHILDREN - 1)) {
      toggleChild(c - '0');
    } else if (c == 's' || c == 'S') {
      startPlay();
    } else if (c == 'r' || c == 'R') {
      resetAll();
    } else if (c == 't' || c == 'T') {
      tempoBpm += 30;
      if (tempoBpm > 150) tempoBpm = 60;
      Serial.print("[tempo] BPM=");
      Serial.println(tempoBpm);
    } else if (c == 'l' || c == 'L') {
      loopOn = !loopOn;
      Serial.print("[loop] ");
      Serial.println(loopOn ? "ON" : "OFF");
    } else if (c == '?') {
      printHelp();
    }
  }
}

void printHelp() {
  Serial.println("=== hack-oya help (canon-broadcast mode) ===");
  Serial.println("0..3 : toggle child on/off");
  Serial.println("       press order decides canon entry tick: 1st=0, 2nd=8, 3rd=16, 4th=20");
  Serial.println("s    : start");
  Serial.println("r    : reset (disable all children, parentTick=0)");
  Serial.println("t    : tempo cycle (60/90/120/150)");
  Serial.println("l    : toggle loop on/off");
  Serial.println("?    : help");
  Serial.print("Current BPM=");
  Serial.print(tempoBpm);
  Serial.print(", loop=");
  Serial.println(loopOn ? "ON" : "OFF");
}

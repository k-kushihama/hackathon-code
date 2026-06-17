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
const int LOOP_REST_TICKS    = 2;
const int TICK_LENGTH        = MELODY_TICK_LENGTH + LOOP_REST_TICKS;  // 33

// 各子機のカノン参加tick (0-indexed)。
// 1回目に有効化された子機は 0tick (楽譜の頭) から参加し、
// 2回目は 8tick (1-indexed の9配列目相当)、3回目は16、4回目は20で参加する。
// ボタンを押した順番でカノンの遅延量が決まる。
const int ENTRY_POINTS[] = {0, 8, 16, 20};
const int NUM_ENTRY_POINTS = sizeof(ENTRY_POINTS) / sizeof(ENTRY_POINTS[0]);

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

// canonOffset[i] = -1: 子機 i は無効化中。送信しない。
// canonOffset[i] >= 0: 子機 i は有効。parentTick >= canonOffset[i] になったら
//                     localPos = parentTick - canonOffset[i] として送信する。
int  canonOffset[NUM_CHILDREN] = {-1, -1, -1, -1};
int  globalPressCount = 0;  // 「ON にした延べ回数」。ENTRY_POINTS のインデックスに使う。

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
      sendTick();
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
}

// ボタンを押すたびに on/off をトグルする。
// OFF -> ON の遷移時は「他の子機で使われていない entry point の最小値」を割り当てる。
// これにより、既に動いている子機(例: child0 offset=0)と同じ offset が当たることがなく、
// 後から ON にした子機は必ずカノン位置({8,16,20} のどれか)で参加する。
// OFF にすると当該 entry が解放され、次に ON する子機が同じ枠を再利用できる。
void toggleChild(int i) {
  if (i < 0 || i >= NUM_CHILDREN) return;
  if (canonOffset[i] >= 0) {
    canonOffset[i] = -1;
    Serial.print("[child ");
    Serial.print(i);
    Serial.println("] OFF");
  } else {
    // 既に他の子機で使用中の entry を集計する
    bool used[NUM_ENTRY_POINTS] = {false, false, false, false};
    for (int k = 0; k < NUM_CHILDREN; k++) {
      if (k == i) continue;
      if (canonOffset[k] < 0) continue;
      for (int e = 0; e < NUM_ENTRY_POINTS; e++) {
        if (ENTRY_POINTS[e] == canonOffset[k]) {
          used[e] = true;
          break;
        }
      }
    }
    // 最小の未使用 entry を選ぶ
    int chosenIdx = 0;
    for (int e = 0; e < NUM_ENTRY_POINTS; e++) {
      if (!used[e]) { chosenIdx = e; break; }
    }
    canonOffset[i] = ENTRY_POINTS[chosenIdx];
    globalPressCount++;
    Serial.print("[child ");
    Serial.print(i);
    Serial.print("] ON canonOffset=");
    Serial.print(canonOffset[i]);
    Serial.print(" (entry#");
    Serial.print(chosenIdx);
    Serial.print(", total#");
    Serial.print(globalPressCount);
    Serial.println(")");
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
  for (int i = 0; i < NUM_CHILDREN; i++) canonOffset[i] = -1;
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

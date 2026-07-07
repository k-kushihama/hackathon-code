// flog_kao_kasi_ir_full
//   カエルの顔(LCD1) + 歌詞(LCD2) + IR同期(NEC) を1台に統合した表示子機スケッチ。
//   親機 hack_oya_kai2 が sendNEC(0x00, mask, 0) で流すtickを受信し、
//   自分のビット(childId)が立ったtickだけ 歌詞送り・口パク・移動 を1段ずつ進める。
//   音楽子機 hack-koP2 と同じ単純インクリメント方式で挙動を揃えている。

// 【jukou由来】NEC受信のためのIRremote定義とLCDライブラリ
#define DECODE_NEC
#include <IRremote.hpp>
#include <LiquidCrystal.h>

int childId = 0;                 // 【jukou由来】表示子機の声部。親機ボタンD4/シリアル'0'で参加
const int PIN_IR_RECV = 6;       // 【jukou由来】IR受信モジュールのOUTピン

LiquidCrystal lcd1(12, 11, 5, 4, 3, 2);  // 【idou由来】顔（E=pin11）
LiquidCrystal lcd2(12, 10, 5, 4, 3, 2);  // 【idou由来】歌詞（E=pin10、データ線は共有）


// ---- 【idou由来】カエルのカスタム文字 (lcd1用) ----

// 左上
byte frog0[8] = {
  B00000, B00011, B00100, B01101,
  B01100, B00111, B00111, B01111
};

// 左下（口開）
byte frog1_open[8] = {
  B11111, B11110, B11100, B11110,
  B01111, B00001, B00000, B00000
};
// 左下（口閉）
byte frog1_close[8] = {
  B11111, B11100, B11111, B11111,
  B01111, B00001, B00000, B00000
};

// 上中央
byte frog2[8] = {
  B00000, B10001, B01010, B01010,
  B01110, B11111, B11111, B10101
};

// 下中央（口開）
byte frog3_open[8] = {
  B11111, B00000, B00000, B00000,
  B00000, B11111, B00000, B00000
};
// 下中央（口閉）
byte frog3_close[8] = {
  B11111, B00000, B11111, B11111,
  B11111, B11111, B00000, B00000
};

// 右上
byte frog4[8] = {
  B00000, B11000, B00100, B10110,
  B00110, B11100, B11100, B11110
};

// 右下（口開）
byte frog5_open[8] = {
  B11111, B01111, B00111, B01111,
  B11110, B10000, B00000, B00000
};
// 右下（口閉）
byte frog5_close[8] = {
  B11111, B00111, B11111, B11111,
  B11110, B10000, B00000, B00000
};


// ---- 【jukou由来】歌詞テーブル (LCD2用・tick駆動) ----

#define TOTAL_TICKS  32

const char* lyrics[TOTAL_TICKS] = {
  "KA",   //  0
  "E",    //  1
  "RU",   //  2
  "NO",   //  3
  "U",    //  4
  "TA",   //  5
  "GA",   //  6
  " ",    //  7 (休)
  "KI",   //  8
  "KO",   //  9
  "E",    // 10
  "TE",   // 11
  "KU",   // 12
  "RU",   // 13
  "YO",   // 14
  " ",    // 15 (休)
  "GU",   // 16
  "WA",   // 17
  "GU",   // 18
  "WA",   // 19
  "GU",   // 20
  "WA",   // 21
  "GU",   // 22
  "WA",   // 23
  "GWA!", // 24 (倍速)
  "GWA!", // 25
  "KERO", // 26
  "KERO", // 27
  "GWA!", // 28
  "GWA!", // 29
  "GWA!", // 30
  " ",    // 31 (休)
};

// 【jukou由来】歌詞1文字表示: 行0に現在、行1に次をプレビュー
void showLyric(int pos) {
  // 行0: 現在の歌詞（16スペースで上書きしてからprint）
  lcd2.setCursor(0, 0);
  lcd2.print("                ");  // 16文字スペース
  lcd2.setCursor(0, 0);
  lcd2.print(lyrics[pos]);

  // 行1: 次の歌詞（プレビュー）
  int next = (pos + 1) % TOTAL_TICKS;
  lcd2.setCursor(0, 1);
  lcd2.print("                ");
  lcd2.setCursor(0, 1);
  lcd2.print(lyrics[next]);
}


// ---- 状態変数 ----
long localPos = -1;   // 【jukou由来】自分の出番が来た回数（tick位置）
int frogX = 0;        // 【idou由来】顔の横位置
bool frogOpen = false;// 【idou由来】口の開閉状態
int stepCount = 0;    // 【idou由来】口パク2回ごとに1マス進むためのカウンタ


// 【idou由来】顔の全再描画（口の開閉に応じてCGRAM3,4,5を切替→clear→上下段write）。
//   位置が変わるtickで使う（移動には再printが必要なため）。
void drawFrogFull(int x, bool open) {
  if (open) {
    lcd1.createChar(3, frog1_open);
    lcd1.createChar(4, frog3_open);
    lcd1.createChar(5, frog5_open);
  } else {
    lcd1.createChar(3, frog1_close);
    lcd1.createChar(4, frog3_close);
    lcd1.createChar(5, frog5_close);
  }

  lcd1.clear();

  lcd1.setCursor(x, 0);
  lcd1.write(byte(0));
  lcd1.write(byte(1));
  lcd1.write(byte(2));

  lcd1.setCursor(x, 1);
  lcd1.write(byte(3));
  lcd1.write(byte(4));
  lcd1.write(byte(5));
}

// 【新規】口パクのみの軽量更新: CGRAM(3,4,5)を書き換えるだけ。
//   HD44780はCGRAMを書き換えると表示中の文字も即座に変わるため、
//   位置が変わらないtickでは clear も re-print も不要（チラつき防止）。
void setMouth(bool open) {
  if (open) {
    lcd1.createChar(3, frog1_open);
    lcd1.createChar(4, frog3_open);
    lcd1.createChar(5, frog5_open);
  } else {
    lcd1.createChar(3, frog1_close);
    lcd1.createChar(4, frog3_close);
    lcd1.createChar(5, frog5_close);
  }
}


// 【新規】idouのsetupとjukouのsetupを結合
void setup() {
  Serial.begin(115200);
  lcd1.begin(16, 2);
  lcd2.begin(16, 2);

  // 【idou由来】lcd1のカスタム文字を登録（0,1,2=固定上段 / 3,4,5=口・初期は閉）
  lcd1.createChar(0, frog0);
  lcd1.createChar(1, frog2);
  lcd1.createChar(2, frog4);
  lcd1.createChar(3, frog1_close);
  lcd1.createChar(4, frog3_close);
  lcd1.createChar(5, frog5_close);

  // 【新規】待機中の顔（静止・口閉・左端）を表示
  drawFrogFull(0, false);

  // 【新規】LCD2に待機メッセージ
  lcd2.setCursor(0, 0);
  lcd2.print("MATTETA KAERU");
  lcd2.setCursor(0, 1);
  lcd2.print("OSHITE START!");

  // 【jukou由来】IR受信開始
  IrReceiver.begin(PIN_IR_RECV);
}

// 【jukou由来】ループ構造を踏襲し、顔の更新（口パク/移動）を追加
void loop() {
  if (!IrReceiver.decode()) return;
  IrReceiver.printIRResultShort(&Serial);

  // 【新規】NECリピートフレームは無視（押しっぱなしの誤カウント防止）
  if (IrReceiver.decodedIRData.flags & IRDATA_FLAGS_IS_REPEAT) {
    IrReceiver.resume();
    return;
  }

  if (IrReceiver.decodedIRData.protocol == NEC) {
    // 【jukou由来】親機はcommandにmaskをそのまま乗せて送る（address=0x00固定）
    uint8_t mask = IrReceiver.decodedIRData.command;

    if (mask & (1 << childId)) {
      // 【jukou由来】自分の出番: tick位置を1つ進める（末尾でループ）
      localPos++;
      if (localPos >= TOTAL_TICKS) localPos = 0;

      showLyric((int)localPos);   // 【jukou由来】LCD2に歌詞
      frogOpen = !frogOpen;       // 【idou由来】LCD1の口を反転
      stepCount++;

      // 【idou由来】口パク2回（開→閉）で1マス移動
      if (stepCount >= 2) {
        stepCount = 0;
        frogX++;
        if (frogX > 13) frogX = 0;
        drawFrogFull(frogX, frogOpen);  // 【idou由来】移動時は全再描画
      } else {
        setMouth(frogOpen);             // 【新規】口パクのみ（軽量）
      }
    }
    // 自分のビットが立っていないtickは無視（待機）
  }

  IrReceiver.resume();
}

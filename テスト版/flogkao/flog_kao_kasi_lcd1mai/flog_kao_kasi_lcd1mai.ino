// LCD1枚だけで顔+歌詞を確認できる自走デモ版。
//   左3列=カエルの顔（固定・口パクのみ）、4列目以降=歌詞（行0=現在、行1=次）。
//   配線は顔用LCD（E=D11）と同一なので、2枚構成に増やすときも配線変更不要。親機も不要。
//   本番では flog_kao_kasi_ir_full を書き込むこと。
#include <LiquidCrystal.h>

// ---- 自走tick設定（親機と同じ計算式）----
const int BPM = 120;                       // 親機のBPM相当。変えると速度が変わる
const unsigned long TICK_INTERVAL_MS = 60000UL / BPM / 2;  // =250ms
unsigned long lastTickMs = 0;

// LCD1枚（顔用配線: RS=12, E=11, D4-7=5,4,3,2）
LiquidCrystal lcd(12, 11, 5, 4, 3, 2);

// ---- カエルのカスタム文字（idou由来・無変更）----
byte frog0[8] = { B00000, B00011, B00100, B01101, B01100, B00111, B00111, B01111 };
byte frog1_open[8] = { B11111, B11110, B11100, B11110, B01111, B00001, B00000, B00000 };
byte frog1_close[8] = { B11111, B11100, B11111, B11111, B01111, B00001, B00000, B00000 };
byte frog2[8] = { B00000, B10001, B01010, B01010, B01110, B11111, B11111, B10101 };
byte frog3_open[8] = { B11111, B00000, B00000, B00000, B00000, B11111, B00000, B00000 };
byte frog3_close[8] = { B11111, B00000, B11111, B11111, B11111, B11111, B00000, B00000 };
byte frog4[8] = { B00000, B11000, B00100, B10110, B00110, B11100, B11100, B11110 };
byte frog5_open[8] = { B11111, B01111, B00111, B01111, B11110, B10000, B00000, B00000 };
byte frog5_close[8] = { B11111, B00111, B11111, B11111, B11110, B10000, B00000, B00000 };

// ---- 歌詞（32tick・親機の楽譜と1:1対応・無変更）----
#define TOTAL_TICKS 32
const char* lyrics[TOTAL_TICKS] = {
  "KA", "E", "RU", "NO", "U", "TA", "GA", " ",
  "KI", "KO", "E", "TE", "KU", "RU", "YO", " ",
  "GU", "WA", "GU", "WA", "GU", "WA", "GU", "WA",
  "GWA!", "GWA!", "KERO", "KERO", "GWA!", "GWA!", "GWA!", " ",
};

// ---- 状態 ----
long localPos = -1;
bool frogOpen = false;

// 口パク: CGRAMのスロット3,4,5を書き換えるだけ。表示中の文字も即座に変わるため
// re-print不要・clear禁止（顔と歌詞が消える）
void setMouth(bool open) {
  if (open) {
    lcd.createChar(3, frog1_open);
    lcd.createChar(4, frog3_open);
    lcd.createChar(5, frog5_open);
  } else {
    lcd.createChar(3, frog1_close);
    lcd.createChar(4, frog3_close);
    lcd.createChar(5, frog5_close);
  }
}

// 歌詞表示: 列4〜15の12桁だけを書き換える。列0〜3（顔+区切り）には絶対に触れない
void showLyric(int pos) {
  lcd.setCursor(4, 0);
  lcd.print("            ");  // 12桁スペース
  lcd.setCursor(4, 0);
  lcd.print(lyrics[pos]);
  int next = (pos + 1) % TOTAL_TICKS;
  lcd.setCursor(4, 1);
  lcd.print("            ");
  lcd.setCursor(4, 1);
  lcd.print(lyrics[next]);
}

// 顔の全体描画（setupで一度だけ。以降は setMouth のみ）
void drawFrogOnce() {
  lcd.setCursor(0, 0);
  lcd.write(byte(0));
  lcd.write(byte(1));
  lcd.write(byte(2));
  lcd.setCursor(0, 1);
  lcd.write(byte(3));
  lcd.write(byte(4));
  lcd.write(byte(5));
}

void setup() {
  lcd.begin(16, 2);
  // 上段（不変部分）
  lcd.createChar(0, frog0);
  lcd.createChar(1, frog2);
  lcd.createChar(2, frog4);
  // 下段（口。閉じた状態で初期化）
  lcd.createChar(3, frog1_close);
  lcd.createChar(4, frog3_close);
  lcd.createChar(5, frog5_close);
  lcd.clear();          // clearはここの1回だけ
  drawFrogOnce();
}

void loop() {
  unsigned long now = millis();
  if (now - lastTickMs < TICK_INTERVAL_MS) return;  // unsigned減算でオーバーフロー安全
  lastTickMs = now;

  // 1tick分の進行: 歌詞を進めて口パク
  localPos++;
  if (localPos >= TOTAL_TICKS) localPos = 0;
  showLyric((int)localPos);
  frogOpen = !frogOpen;
  setMouth(frogOpen);
}

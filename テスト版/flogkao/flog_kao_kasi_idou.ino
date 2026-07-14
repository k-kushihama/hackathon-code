#include <LiquidCrystal.h>

// LCD1: カエルの顔（移動・口パク）
LiquidCrystal lcd1(12, 11, 5, 4, 3, 2);

// LCD2: カエルの歌の歌詞（1文字ずつ横流し）
LiquidCrystal lcd2(12, 10, 5, 4, 3, 2);


// ---- カエルのカスタム文字 (lcd1用) ----

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


// ---- 歌詞 ----
const char* lyrics =
  "KAERU NO UTA GA   "
  "KIKOETE KURU YO   "
  "GUWA GUWA GUWA GUWA   "
  "KERO KERO KERO KERO   "
  "GUWA GUWA GUWA   ";

int lyricsLen;
int scrollPos = 0;

// ---- カエルの状態 ----
int frogX   = 0;
bool frogOpen = false;
int stepCount = 0;  // 口パク2回ごとに1マス進む


void drawFrog(int x, bool open) {
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

void scrollLyrics() {
  char buf[17];
  for (int i = 0; i < 16; i++) {
    buf[i] = lyrics[(scrollPos + i) % lyricsLen];
  }
  buf[16] = '\0';
  lcd2.setCursor(0, 0);
  lcd2.print(buf);
  scrollPos = (scrollPos + 1) % lyricsLen;
}


void setup() {
  lcd1.begin(16, 2);
  lcd2.begin(16, 2);

  // lcd1のカスタム文字を登録（変わらない部分）
  lcd1.createChar(0, frog0);
  lcd1.createChar(1, frog2);
  lcd1.createChar(2, frog4);
  lcd1.createChar(3, frog1_close);
  lcd1.createChar(4, frog3_close);
  lcd1.createChar(5, frog5_close);

  lcd1.clear();
  lcd2.clear();

  lyricsLen = strlen(lyrics);
}

void loop() {
  // LCD2: 歌詞を1文字スクロール
  scrollLyrics();

  // LCD1: カエルの顔を口パクしながら移動
  drawFrog(frogX, frogOpen);
  frogOpen = !frogOpen;

  // 口パク2回（開→閉）で1マス進む
  stepCount++;
  if (stepCount >= 2) {
    stepCount = 0;
    frogX++;
    if (frogX > 13) frogX = 0;
  }

  delay(300);
}

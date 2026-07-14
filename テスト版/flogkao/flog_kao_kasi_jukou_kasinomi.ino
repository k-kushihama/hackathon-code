#define DECODE_NEC
#include <IRremote.hpp>
#include <LiquidCrystal.h>

// hack-koP2 と同じ childId / PIN
int childId = 0;
const int PIN_IR_RECV = 6;



// LCD2: カエルの歌の歌詞（音のタイミングで表示）
LiquidCrystal lcd2(12, 10, 5, 4, 3, 2);



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


long localPos = -1;

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


void setup() {
  Serial.begin(115200);
  lcd2.begin(16, 2);
  lcd2.clear();
  IrReceiver.begin(PIN_IR_RECV);
}

void loop() {
  if (!IrReceiver.decode()) return;
  IrReceiver.printIRResultShort(&Serial);

  if (IrReceiver.decodedIRData.protocol == NEC) {
    uint8_t mask = IrReceiver.decodedIRData.command;

    if (mask & (1 << childId)) {
      localPos++;
      if (localPos >= TOTAL_TICKS) {
        localPos = 0;
      }
      showLyric((int)localPos);
    }
  }

  IrReceiver.resume();
}


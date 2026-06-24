#define BAUD 921600

const int PIN_IR_START   = 2;
const int PIN_IR_ADVANCE = 3;

const int melodyLength = 32;
String myScore[] = {
  "C4", "D4", "E4", "F4", "E4", "D4", "C4", "R",
  "E4", "F4", "G4", "A4", "G4", "F4", "E4", "R",
  "C4", "R",  "C4", "R",  "C4", "R",  "C4", "R",
  "C4", "C4", "D4", "D4", "E4", "D4", "C4", "R"
};

int  idx         = 0;
bool isPlaying   = false;
int  prevStart   = HIGH;
int  prevAdvance = HIGH;

void setup() {
  Serial.begin(BAUD);
  pinMode(PIN_IR_START,   INPUT_PULLUP);
  pinMode(PIN_IR_ADVANCE, INPUT_PULLUP);
}

void loop() {
  int s = digitalRead(PIN_IR_START);
  if (prevStart == HIGH && s == LOW) {
    isPlaying = true;
    idx = 0;
    sendNote();
    delay(20);
  }
  prevStart = s;

  int a = digitalRead(PIN_IR_ADVANCE);
  if (isPlaying && prevAdvance == HIGH && a == LOW) {
    idx++;
    if (idx >= melodyLength) idx = 0;
    sendNote();
    delay(20);
  }
  prevAdvance = a;
}

void sendNote() {
  Serial.println(myScore[idx]);
}

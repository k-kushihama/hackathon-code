#define BAUD 115200

const int PIN_IR_START   = 2;
const int PIN_IR_ADVANCE = 3;
const int PIN_LED        = 13;

const int SCORE_LEN = 32;
const int myScore[SCORE_LEN] = {
  60, 62, 64, 65,  64, 62, 60,  0,
  64, 65, 67, 69,  67, 65, 64,  0,
  60,  0, 60,  0,  60,  0, 60,  0,
  60, 60, 62, 62,  64, 62, 60,  0
};

int  idx         = 0;
bool isPlaying   = false;
int  prevStart   = HIGH;
int  prevAdvance = HIGH;

void setup() {
  Serial.begin(BAUD);
  pinMode(PIN_IR_START,   INPUT_PULLUP);
  pinMode(PIN_IR_ADVANCE, INPUT_PULLUP);
  pinMode(PIN_LED, OUTPUT);
}

void loop() {
  int s = digitalRead(PIN_IR_START);
  if (prevStart == HIGH && s == LOW) {
    isPlaying = true;
    idx = 0;
    sendNote(myScore[idx]);
    digitalWrite(PIN_LED, HIGH);
    delay(20);
  }
  prevStart = s;

  int a = digitalRead(PIN_IR_ADVANCE);
  if (isPlaying && prevAdvance == HIGH && a == LOW) {
    idx++;
    if (idx >= SCORE_LEN) idx = 0;
    sendNote(myScore[idx]);
    digitalWrite(PIN_LED, (idx % 2) ? HIGH : LOW);
    delay(20);
  }
  prevAdvance = a;
}

void sendNote(int note) {
  Serial.print("P:");
  Serial.println(note);
}

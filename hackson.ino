#define BAUD 921600 // ボーレート（速め）
#define PIN 0 // A0 アナログ出⼒
#define RESOLUTION 10 // 量⼦化10bit
const int melodyLength = 29;
String melody[] = {
"C4", "D4", "E4", "F4","E4","D4","C4",
"E4","F4","G4","A4","G4","F4","E4",
"C4","C4","C4","C4",
"C4","C4","D4","D4","E4","E4","F4","F4","E4","D4","C4"};
 float duration[] = {
0.4f, 0.4f,0.4f,0.4f,0.4f,0.4f,0.4f,
0.4f,0.4f,0.4f,0.4f,0.4f,0.4f,0.4f,
0.4f,0.4f,0.4f,0.4f,
0.2f,0.2f,0.2f,0.2f,0.2f,0.2f,0.2f,0.2f,0.4f,0.4f,0.4f,
};

float startTime[]  = {
0.0f, 0.5f, 1.0f, 1.5f, 2.0f, 2.5f, 3.0f,      
  4.0f, 4.5f, 5.0f, 5.5f, 6.0f, 6.5f, 7.0f,      
  8.0f, 9.0f, 10.0f, 11.0f,                      
  12.0f, 12.25f, 12.5f, 12.75f, 13.0f, 13.25f, 13.5f, 13.75f, 
  14.0f, 14.5f, 15.0f};
  
  float amplitude[]  = {
  0.8f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 
  0.8f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 
  0.7f, 0.7f, 0.7f, 0.7f,                  
  0.3f, 0.3f, 0.3f, 0.3f, 0.3f, 0.3f, 0.3f, 0.3f,
  0.6f, 0.8f, 1.0f
};

void setup() {
Serial.begin(BAUD);
while (Serial.available() <= 0) {
    delay(10); 
  }
  delay(500);
sendMelody();
}
void loop() {

}
void sendMelody(){
for(int i = 0;i<melodyLength;i++){
Serial.print(melody[i]);
Serial.print(":");
Serial.print(duration[i]);
Serial.print(":");
Serial.print(startTime[i]);
Serial.print(":");
Serial.print(amplitude[i]);

if(i<28){
  Serial.print(",");
}
}
  Serial.println();
}

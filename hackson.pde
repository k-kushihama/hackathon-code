
 import processing.serial.*;
Serial port; // シリアルポート
import ddf.minim.*;
import ddf.minim.ugens.*;
Waveform currentWaveform ;
 Minim minim ;
 AudioOutput out;
 int melodyLength = 29;
 String[] melody = new String[melodyLength];
 float[] duration = new float[melodyLength];
 float[] startTime = new float[melodyLength];
 float[] amplitude = new float[melodyLength];
void setup(){
size (512 , 200);
minim = new Minim ( this );
out = minim . getLineOut ();
out. setTempo ( 120 );
currentWaveform = WavetableGenerator.gen10( 4096, new float[] { 1.0f, 0.8f, 0.6f, 0.4f, 0.2f, 0.15f, 0.1f, 0.05f });
port = new Serial(this, "/dev/cu.usbmodem34B7DA64C6002",921600);
port.clear();
port.bufferUntil('\n');
delay(1000); // 接続が安定するまで少し待つ
  port.write('A'); // Arduinoに「送って！」と合図（Aでも何でもOK）
  println("Arduinoに送信リクエストを送りました...");


}

void draw() {
  background(0);
  stroke(255);
  // 波形を表示（おまけ）
  for(int i = 0; i < out.bufferSize() - 1; i++) {
    line(i, 50 + out.left.get(i)*50, i+1, 50 + out.left.get(i+1)*50);
  }
}

void serialEvent(Serial p) {
String inString =
p.readStringUntil('\n');
if(inString != null){
  println("受信した生データ: " + inString);
  String[] notes =  split(trim(inString),',');
  for(int i=0;i<notes.length; i++){
    String[] data = 
    split(notes[i],':');
    if(data.length ==4){
      melody[i] = data[0];
      duration[i] = float(data[1]);
      startTime[i] = float(data[2]);
      amplitude[i] = float(data[3]);
      
      
    }
    
}
}
}


 // 音色を変更するためにInstrument インタフェースを実装する
 class HackInstrument implements Instrument
 {
Oscil wave;
  Line ampEnv;
  Oscil vib;      // ビブラート
  Summer freqSum; // 周波数を合流させる部品
  Constant baseFreq; // 基本周波数を保持する部品
  float maxAmp;

  HackInstrument(float frequency, float maxAmp, Waveform wf) {
    this.maxAmp = maxAmp;
    
    // 1. 周波数の合流地点（Summer）を作成
    freqSum = new Summer();
    
    // 2. 基本の音の高さを「Constant」として作成
    baseFreq = new Constant(frequency);
    
    // 3. ビブラート（揺れ）の設定
    vib = new Oscil(5, 4, Waves.SINE); 
    
    // 4. 回路を繋ぐ： [基本周波数] + [ビブラート] → [Summer]
    baseFreq.patch(freqSum);
    vib.patch(freqSum);
    
    // 5. 音を出す本体（Oscil）を作り、合流した周波数を繋ぐ
    wave = new Oscil(frequency, 0, wf);
    freqSum.patch(wave.frequency);
    
    // 6. 音量制御を繋ぐ
    ampEnv = new Line();
    ampEnv.patch(wave.amplitude);
  }

  void noteOn(float duration) {
    // トランペットらしく、最後まで音を出し切る設定（第3引数を 0 に）
    ampEnv.activate(duration, this.maxAmp, 0);
    wave.patch(out);
 }

 
 void noteOff ()
{ // 再生の停止
wave . unpatch ( out );
}
 }

void playSong() {
  out.pauseNotes();
  boolean hasData = false;
  for (int i = 0; i < melody.length; i++) {
    if (melody[i] != null) { // データがある場合だけ
      out.playNote(startTime[i], duration[i],
        new HackInstrument(Frequency.ofPitch(melody[i]).asHz(),
        amplitude[i], currentWaveform));
      hasData = true;
    }
  }
  if (!hasData) {
    println("まだデータが届いていないようです。Arduinoを確認してください。");
  }
  out.resumeNotes();
}
 
 void keyPressed () {
switch (key)
{
case '1':
// 正弦波
currentWaveform = Waves . SINE ;
break ;
case '2':
// 三角波
currentWaveform = Waves . TRIANGLE ;
break ;
case '3':
// のこぎり波
currentWaveform = Waves .SAW;
break ;
case '4':
// 矩形波
currentWaveform = Waves . SQUARE ;
break ;
case '5':
// 倍音の振幅値を指定した自作の音色
currentWaveform = WavetableGenerator . gen10 (
4096 , // サンプルサイズ（2 の倍数で）
new float [] { 1.0f, 0.45f, 0.20f, 0.10f,0.05f } // 各倍音の振幅値
);
break ;
case 'p':
// 作成した信号を出力
playSong ();
break ;
default :
break ;
 }
 }
 
 

import processing.serial.*;
import ddf.minim.*;
import ddf.minim.ugens.*;

// 子機3(ドラム機)が送る "C2\n" を受信してキックドラム音を発音する。
// 既存のLEDマトリクス描画も維持し、ドラムが叩かれた瞬間に画面が反転するビジュアライザにする。

Serial port;
Minim minim;
AudioOutput out;
boolean displayLogo = false;

int[][] frame = {
  { 0, 0, 1, 1, 1, 0, 1, 1, 1, 0, 0, 0 },
  { 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0 },
  { 0, 0, 1, 1, 1, 0, 1, 1, 1, 0, 0, 0 },
  { 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0 },
  { 0, 0, 1, 1, 1, 0, 1, 1, 1, 0, 0, 0 },
  { 0, 0, 1, 0, 1, 0, 1, 0, 1, 0, 0, 0 },
  { 0, 0, 1, 0, 1, 0, 1, 0, 1, 0, 0, 0 },
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }
};

int cellSize = 20;

void settings() {
  size(12 * cellSize, 8 * cellSize);
}

void setup() {
  // 利用可能なシリアルポート一覧をコンソールに表示。実機では子機3に対応する
  // /dev/cu.wchusbserial-XXXX または /dev/cu.usbmodemXXXX を選んで下の行を差し替える。
  println(Serial.list());
  port = new Serial(this, "/dev/cu.usbmodem4827E2E019382", 115200);
  port.bufferUntil('\n');

  minim = new Minim(this);
  out = minim.getLineOut();

  displayLogo = false;
}

void draw() {
  background(255);
  if (displayLogo) {
    for (int i = 0; i < frame.length; i++) {
      for (int j = 0; j < frame[i].length; j++) {
        if (frame[i][j] == 1) {
          fill(0);
        } else {
          fill(200);
        }
        rect(j * cellSize, i * cellSize, cellSize, cellSize);
      }
    }
  }
}

// 子機3 hack-ko.ino の DRUM_NOTE と同じ "C2" を受信したらキックを発音する。
// 旧版の1バイト読み('0'/'1')はもう来ないので readStringUntil で行単位受信に変更。
void serialEvent(Serial p) {
  String inString = p.readStringUntil('\n');
  if (inString == null) return;
  inString = trim(inString);
  if (inString.length() == 0) return;

  // デバッグ用: 受信した文字列を毎回コンソール表示する。
  // "hack-ko ready CHILD_ID=3 (DRUM)" のような起動メッセージや
  // "C2" 連発が見えるはず。化けた文字列ならボーレート不一致を疑う。
  println("rx: " + inString);

  if (inString.equals("C2")) {
    playKick();
    displayLogo = !displayLogo;  // 叩いた瞬間に画面パターンを反転させてビジュアライザ化
  }
}

void playKick() {
  // 短い発音時間(0.15秒)でキックの「ドゥン」感を作る。
  out.playNote(0, 0.15f, new KickInstrument());
}

// キックドラム用Instrument。
// 80Hz→30Hzへ急速に下がるピッチエンベロープと、duration内で振幅0まで
// 落ちるアンプエンベロープで、サイン波1本から「ドゥン」というキック感を作る。
class KickInstrument implements Instrument {
  Oscil wave;
  Line ampEnv;
  Line freqEnv;

  KickInstrument() {
    wave = new Oscil(80, 0.0f, Waves.SINE);
    ampEnv = new Line();
    ampEnv.patch(wave.amplitude);
    freqEnv = new Line();
    freqEnv.patch(wave.frequency);
  }

  void noteOn(float duration) {
    // ピッチは duration の前半30%で急速に下降してアタック感を作る。
    freqEnv.activate(duration * 0.3f, 80, 30);
    // 振幅は duration 全体でフルから無音まで減衰。
    ampEnv.activate(duration, 0.9f, 0);
    wave.patch(out);
  }

  void noteOff() {
    wave.unpatch(out);
  }
}

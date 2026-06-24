import processing.serial.*;
Serial port;
import ddf.minim.*;
import ddf.minim.ugens.*;
Waveform currentWaveform;
Minim minim;
AudioOutput out;
String currentNote = "";
int currentPos = -1;  // 親機が割り当ててきた localPos (デバッグ表示用)

void setup() {
  size(512, 200);
  minim = new Minim(this);
  out = minim.getLineOut();
  out.setTempo(120);
  currentWaveform = WavetableGenerator.gen10(4096, new float[] { 1.0f, 0.6f, 0.35f, 0.2f, 0.1f, 0.05f });
  // portは各自変えて．
  port = new Serial(this, "/dev/cu.usbmodem64E83364FFA82", 115200);
  port.clear();
  port.bufferUntil('\n');
}

void draw() {
  background(0);
  stroke(255);
  for (int i = 0; i < out.bufferSize() - 1; i++) {
    line(i, 50 + out.left.get(i)*50, i+1, 50 + out.left.get(i+1)*50);
  }
  fill(255);
  text("note: " + currentNote, 10, 170);
  text("pos : " + currentPos, 10, 190);
}

void serialEvent(Serial p) {
  String inString = p.readStringUntil('\n');
  if (inString == null) return;
  inString = trim(inString);
  if (inString.length() == 0) return;
  currentNote = inString;

  // ino側のフォーマット: "ピッチ名,duration秒,amplitude,localPos"
  // localPos は親機が決めた楽譜index。発音には使わずデバッグ表示専用。
  // 旧3列フォーマット(pitch,duration,amplitude)も互換のため受け付ける。
  // それ以外(起動メッセージなど)はパース不能としてスキップ。
  String[] parts = split(inString, ',');
  if (parts.length < 3) {
    println("non-note: " + inString);
    return;
  }
  String pitch = parts[0];
  float duration = float(parts[1]);
  float amplitude = float(parts[2]);
  if (parts.length >= 4) {
    currentPos = int(parts[3]);
  }
  if (pitch.equals("R")) return;
  playNote(pitch, duration, amplitude);
}

void playNote(String pitch, float duration, float amplitude) {
  float freq = Frequency.ofPitch(pitch).asHz();
  out.playNote(0, duration, new PianoInstrument(freq, amplitude, currentWaveform));
}

class PianoInstrument implements Instrument {
  Oscil wave;
  Line ampEnv;
  Oscil vib;
  Summer freqSum;
  Constant baseFreq;
  float maxAmp;

  PianoInstrument(float frequency, float maxAmp, Waveform wf) {
    this.maxAmp = maxAmp;
    freqSum = new Summer();
    baseFreq = new Constant(frequency);
    vib = new Oscil(5, 0, Waves.SINE);
    baseFreq.patch(freqSum);
    vib.patch(freqSum);
    wave = new Oscil(frequency, 0, wf);
    freqSum.patch(wave.frequency);
    ampEnv = new Line();
    ampEnv.patch(wave.amplitude);
  }

  void noteOn(float duration) {
    ampEnv.activate(duration, this.maxAmp, 0);
    wave.patch(out);
  }

  void noteOff() {
    wave.unpatch(out);
  }
}

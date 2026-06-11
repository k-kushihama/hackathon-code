import processing.serial.*;
Serial port;
import ddf.minim.*;
import ddf.minim.ugens.*;
Waveform currentWaveform;
Minim minim;
AudioOutput out;
String currentNote = "";

void setup() {
  size(512, 200);
  minim = new Minim(this);
  out = minim.getLineOut();
  out.setTempo(120);
  currentWaveform = WavetableGenerator.gen10(4096, new float[] { 1.0f, 0.6f, 0.35f, 0.2f, 0.1f, 0.05f });
  port = new Serial(this, "/dev/cu.usbmodem34B7DA64C6002", 921600);
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
  text("note: " + currentNote, 10, 180);
}

void serialEvent(Serial p) {
  String inString = p.readStringUntil('\n');
  if (inString != null) {
    inString = trim(inString);
    if (inString.length() == 0) return;
    currentNote = inString;
    if (inString.equals("R")) return;
    // 子機側 hack-ko.ino の DRUM_NOTE と一致する音名はドラム機からの送信と
    // みなし、ピアノではなくキック専用Instrumentを発音する。
    if (inString.equals("C2")) {
      playKick();
    } else {
      playNote(inString);
    }
  }
}

void playNote(String pitch) {
  float freq = Frequency.ofPitch(pitch).asHz();
  out.playNote(0, 0.4f, new PianoInstrument(freq, 0.6f, currentWaveform));
}

void playKick() {
  // 短い発音時間(0.15秒)でキックの「ドゥン」感を出す。
  out.playNote(0, 0.15f, new KickInstrument());
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

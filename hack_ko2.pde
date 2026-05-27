import processing.serial.*;
Serial port;
import ddf.minim.*;
import ddf.minim.ugens.*;
Waveform currentWaveform;
Minim minim;
AudioOutput out;
int currentNote = 0;

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
    int midi = int(inString);
    currentNote = midi;
    if (midi > 0) {
      playNote(midi);
    }
  }
}

void playNote(int midi) {
  float freq = (float)(440.0 * Math.pow(2.0, (midi - 69) / 12.0));
  out.playNote(0, 0.4f, new PianoInstrument(freq, 0.6f, currentWaveform));
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

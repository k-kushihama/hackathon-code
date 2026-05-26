import processing.serial.*;
import ddf.minim.*;
import ddf.minim.ugens.*;

Minim       minim;
AudioOutput out;
Serial      port;

final String SERIAL_PORT = "/dev/cu.usbmodem34B7DA64C6002";
final int    BAUD        = 115200;

int currentNote = 0;

void setup() {
  size(640, 200);
  minim = new Minim(this);
  out   = minim.getLineOut(Minim.STEREO, 1024);

  try {
    port = new Serial(this, SERIAL_PORT, BAUD);
    port.clear();
    port.bufferUntil('\n');
  } catch (Exception e) {
    println("Serial open failed: " + e.getMessage());
    println("Available: " + join(Serial.list(), ", "));
    port = null;
  }
}

void draw() {
  background(20);
  stroke(120, 200, 255);
  noFill();
  beginShape();
  for (int i = 0; i < out.bufferSize(); i++) {
    vertex(i * (width / (float) out.bufferSize()),
           height / 2 + out.mix.get(i) * 80);
  }
  endShape();

  fill(255);
  textSize(14);
  text("Piano note: " + currentNote, 10, 20);
}

void serialEvent(Serial p) {
  String line = p.readStringUntil('\n');
  if (line == null) return;
  line = trim(line);
  if (line.length() == 0) return;

  String[] kv = split(line, ':');
  if (kv.length != 2) return;
  if (!kv[0].equals("P")) return;

  int note = int(kv[1]);
  currentNote = note;
  if (note > 0) playPianoSound(note);
}

float midiToHz(int midi) {
  return (float)(440.0 * Math.pow(2.0, (midi - 69) / 12.0));
}

void playPianoSound(int note) {
  Waveform wf = WavetableGenerator.gen10(4096,
      new float[] { 1.0f, 0.6f, 0.35f, 0.2f, 0.1f, 0.05f });
  out.playNote(0.0f, 0.4f,
      new PianoInstrument(midiToHz(note), 0.6f, wf));
}

class PianoInstrument implements Instrument {
  Oscil osc;
  ADSR  adsr;

  PianoInstrument(float hz, float amp, Waveform wf) {
    osc  = new Oscil(hz, amp, wf);
    adsr = new ADSR(amp, 0.005f, 0.12f, 0.4f * amp, 0.15f);
    osc.patch(adsr);
  }

  void noteOn(float dur) {
    adsr.noteOn();
    adsr.patch(out);
  }

  void noteOff() {
    adsr.unpatchAfterRelease(out);
    adsr.noteOff();
  }
}

void stop() {
  if (port != null) port.stop();
  out.close();
  minim.stop();
  super.stop();
}

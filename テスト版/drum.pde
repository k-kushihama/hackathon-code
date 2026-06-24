import ddf.minim.*;
import ddf.minim.ugens.*;
import processing.serial.*;

Minim minim;
AudioOutput out;
Serial arduino;

// ==================================================
// シリアル設定
// ==================================================

final int SERIAL_PORT_INDEX = 0;   // 環境に合わせて変更
final int BAUD_RATE = 115200;

// ==================================================
// キック
// ==================================================

Oscil kickOsc;
Multiplier kickAmp;

Oscil kickHarmonic;
Multiplier kickHarmonicAmp;

// ==================================================
// シンバル
// ==================================================

Noise cymNoise;
Multiplier cymNoiseAmp;

Oscil cymMetal1;
Oscil cymMetal2;
Oscil cymMetal3;

Multiplier cymMetalAmp1;
Multiplier cymMetalAmp2;
Multiplier cymMetalAmp3;

// ==================================================
// 自動演奏
// ==================================================

boolean autoPlay = false;
int lastTriggerTime = 0;
int autoStep = 0;

// ==================================================
// 初期設定
// ==================================================

void setup() {
  size(700, 260);

  minim = new Minim(this);
  out = minim.getLineOut(Minim.MONO, 512);

  setupKick();
  setupCymbal();
  setupSerial();

  textSize(18);
}

void setupKick() {
  kickOsc = new Oscil(48.0, 1.0, Waves.SINE);
  kickAmp = new Multiplier(0.0);
  kickOsc.patch(kickAmp).patch(out);

  kickHarmonic = new Oscil(96.0, 1.0, Waves.SINE);
  kickHarmonicAmp = new Multiplier(0.0);
  kickHarmonic.patch(kickHarmonicAmp).patch(out);
}

void setupCymbal() {
  cymNoise = new Noise(1.0, Noise.Tint.WHITE);
  cymNoiseAmp = new Multiplier(0.0);
  cymNoise.patch(cymNoiseAmp).patch(out);

  cymMetal1 = new Oscil(431.0, 1.0, Waves.SQUARE);
  cymMetal2 = new Oscil(647.0, 1.0, Waves.SQUARE);
  cymMetal3 = new Oscil(913.0, 1.0, Waves.SQUARE);

  cymMetalAmp1 = new Multiplier(0.0);
  cymMetalAmp2 = new Multiplier(0.0);
  cymMetalAmp3 = new Multiplier(0.0);

  cymMetal1.patch(cymMetalAmp1).patch(out);
  cymMetal2.patch(cymMetalAmp2).patch(out);
  cymMetal3.patch(cymMetalAmp3).patch(out);
}

void setupSerial() {
  println("=== Serial Ports ===");
  printArray(Serial.list());

  final String SERIAL_PORT_NAME = "/dev/cu.usbmodem34B7DA64C6002";

  arduino = new Serial(this, SERIAL_PORT_NAME, BAUD_RATE);
  arduino.bufferUntil('\n');
}

// ==================================================
// 画面描画
// ==================================================

void draw() {
  background(245);
  fill(0);

  text("Keyboard test:", 30, 40);
  text("1: Kick   2: Cymbal", 30, 70);
  text("A: Auto play ON/OFF", 30, 100);
  text("Arduino serial: 1 = Kick, 2 = Cymbal", 30, 130);
  text("Auto: " + (autoPlay ? "ON" : "OFF"), 30, 170);

  if (autoPlay && millis() - lastTriggerTime >= 500) {
    lastTriggerTime = millis();

    if (autoStep == 0) {
      playKickSound();
    } else if (autoStep == 1) {
      playCymbalSound();
    } else if (autoStep == 2) {
      playKickSound();
    } else if (autoStep == 3) {
      playCymbalSound();
    }

    autoStep = (autoStep + 1) % 4;
  }
}

// ==================================================
// シリアル受信
// ==================================================

void serialEvent(Serial port) {
  String receivedText = port.readStringUntil('\n');

  if (receivedText == null) return;

  receivedText = trim(receivedText);
  if (receivedText.length() == 0) return;

  println("recv: " + receivedText);

  try {
    int note = Integer.parseInt(receivedText);

    if (note == 1) {
      playKickSound();
    } else if (note == 2) {
      playCymbalSound();
    }
  }
  catch (NumberFormatException e) {
    println("invalid data: " + receivedText);
  }
}

// ==================================================
// キー入力
// ==================================================

void keyPressed() {
  if (key == '1') {
    playKickSound();
  } else if (key == '2') {
    playCymbalSound();
  } else if (key == 'a' || key == 'A') {
    autoPlay = !autoPlay;
    lastTriggerTime = millis();
    autoStep = 0;
  }
}

// ==================================================
// キック音
// ==================================================

void playKickSound() {
  new Thread(new Runnable() {
    public void run() {
      final int durationMs = 650;
      final int updateMs = 2;
      int steps = durationMs / updateMs;

      kickAmp.setValue(0.0);
      kickHarmonicAmp.setValue(0.0);

      for (int i = 0; i < steps; i++) {
        float t = i * updateMs / 1000.0;

        float frequency = 48.0 + 122.0 * exp(-28.0 * t);
        float bodyEnvelope = exp(-8.0 * t);
        float attack = constrain(t / 0.004, 0.0, 1.0);

        float bodyVolume = 0.82 * bodyEnvelope * attack;
        float harmonicVolume = 0.025 * exp(-18.0 * t) * attack;

        kickOsc.setFrequency(frequency);
        kickAmp.setValue(bodyVolume);

        kickHarmonic.setFrequency(frequency * 2.0);
        kickHarmonicAmp.setValue(harmonicVolume);

        sleepMs(updateMs);
      }

      kickAmp.setValue(0.0);
      kickHarmonicAmp.setValue(0.0);
    }
  }).start();
}

// ==================================================
// シンバル音
// ==================================================

void playCymbalSound() {
  new Thread(new Runnable() {
    public void run() {
      final int durationMs = 700;
      final int updateMs = 4;
      int steps = durationMs / updateMs;

      cymNoiseAmp.setValue(0.0);
      cymMetalAmp1.setValue(0.0);
      cymMetalAmp2.setValue(0.0);
      cymMetalAmp3.setValue(0.0);

      for (int i = 0; i < steps; i++) {
        float t = i / float(steps - 1);

        float fastDecay = exp(-13.0 * t);
        float slowDecay = exp(-4.0 * t);

        float noiseEnvelope = 0.25 * fastDecay + 0.10 * slowDecay;
        float metalEnvelope = 0.09 * exp(-9.0 * t);
        float attack = constrain(t * 45.0, 0.0, 1.0);

        cymNoiseAmp.setValue(noiseEnvelope * attack);
        cymMetalAmp1.setValue(metalEnvelope * attack);
        cymMetalAmp2.setValue(metalEnvelope * 0.75 * attack);
        cymMetalAmp3.setValue(metalEnvelope * 0.50 * attack);

        sleepMs(updateMs);
      }

      cymNoiseAmp.setValue(0.0);
      cymMetalAmp1.setValue(0.0);
      cymMetalAmp2.setValue(0.0);
      cymMetalAmp3.setValue(0.0);
    }
  }).start();
}

// ==================================================
// 待機処理
// ==================================================

void sleepMs(int milliseconds) {
  try {
    Thread.sleep(milliseconds);
  }
  catch (InterruptedException e) {
    Thread.currentThread().interrupt();
  }
}

// ==================================================
// 終了処理
// ==================================================

void stop() {
  if (kickAmp != null) kickAmp.setValue(0.0);
  if (kickHarmonicAmp != null) kickHarmonicAmp.setValue(0.0);
  if (cymNoiseAmp != null) cymNoiseAmp.setValue(0.0);
  if (cymMetalAmp1 != null) cymMetalAmp1.setValue(0.0);
  if (cymMetalAmp2 != null) cymMetalAmp2.setValue(0.0);
  if (cymMetalAmp3 != null) cymMetalAmp3.setValue(0.0);

  if (arduino != null) arduino.stop();
  if (out != null) out.close();
  if (minim != null) minim.stop();

  super.stop();
}

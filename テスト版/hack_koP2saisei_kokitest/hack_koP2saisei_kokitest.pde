// hack_koP2saisei_kokitest.pde
// 音源テスト用: 親機ロジック(BPM管理+tick進行+楽譜位置トリガ配信) と
// 子機ロジック(トリガ受信→Minim発音) を1つの Processing スケッチに統合。
// 実機(IR/シリアル/Arduino) 不要で PC 単独動作。
//
// - 楽譜(かえるのうた)は hack-koP2.ino から一切改変せずコピー。
// - 音源部(PianoInstrument, gen10 波形) は hackkosaisei/hackko.pde からコピー。
// - GUI: BPM スライダー(40..240) と PLAY/STOP ボタン。SPACE でも再生切替。

import ddf.minim.*;
import ddf.minim.ugens.*;

Minim minim;
AudioOutput out;
Waveform waveform;

// ---- 子機側: 発音管理 ----------------------------------------------------
PianoInstrument currentInstrument = null;  // 直近発音を保持。R(休符)受信で noteOff。

// ---- 楽譜: hack-koP2.ino からそのままコピー ------------------------------
final int SCORE_LENGTH = 40;
final String[] myScore = {
  "C4", "D4", "E4", "F4", "E4", "D4", "C4", "R",
  "E4", "F4", "G4", "A4", "G4", "F4", "E4", "R",
  "C4", "R",  "C4", "R",  "C4", "R",  "C4", "R",
  "C4", "C4", "D4", "D4", "E4", "E4", "F4", "F4",
  "E4", "R",  "D4", "R",  "C4", "R" , "R" , "R"
};
final int DOUBLE_START = 24;
final int TOTAL_TICKS  = DOUBLE_START + (SCORE_LENGTH - DOUBLE_START) / 2;

final float NOTE_DURATION_DEFAULT = 0.40f;
final float NOTE_DURATION_HALF    = 0.20f;
final float NOTE_AMPLITUDE        = 0.60f;

// ---- 親機側: BPM とトランスポート ----------------------------------------
boolean isPlaying = false;
int bpm = 120;                // 既定値は既存(hack_oya_kai.ino)の 120 を踏襲
final int BPM_MIN = 40;
final int BPM_MAX = 240;

long localPos       = -1;     // -1 = 未開始 (hack-koP2.ino と同じ意味)
long lastTickMs     = 0;

// 倍速区間 (DOUBLE_START 以降) は 1 tick で 2 音鳴らす。
// 元 .ino では delay() で半 tick 待って 2 音目を鳴らしていたが、
// draw() の非同期モデルに合わせて "予約" で表現する。
int  pendingHalfIdx = -1;
long pendingHalfMs  = 0;

// ---- GUI レイアウト ------------------------------------------------------
final int SLIDER_X = 40;
final int SLIDER_Y = 80;
final int SLIDER_W = 420;
final int SLIDER_H = 18;
final int BTN_X    = 40;
final int BTN_Y    = 140;
final int BTN_W    = 140;
final int BTN_H    = 48;
boolean sliderDragging = false;

void setup() {
  size(500, 300);
  textSize(14);
  minim = new Minim(this);
  out   = minim.getLineOut();
  out.setTempo(120);
  waveform = WavetableGenerator.gen10(4096,
    new float[] { 1.0f, 0.6f, 0.35f, 0.2f, 0.1f, 0.05f });
}

void draw() {
  background(30);

  fill(230);
  text("hack_koP2saisei_kokitest — 音源テスト (かえるのうた)", 20, 30);
  fill(160);
  text("Click PLAY / drag slider / SPACE toggles play", 20, 52);

  drawBpmSlider();
  drawTransportButton();
  drawStatus();
  drawWaveform();

  // ---- 親機ロジック: BPM に応じて tick を進め、トリガを配信 -------------
  if (isPlaying) {
    long now = millis();
    long interval = tickIntervalMs();
    if (now - lastTickMs >= interval) {
      lastTickMs = now;
      masterTick();
    }
    if (pendingHalfIdx >= 0 && now >= pendingHalfMs) {
      int idx = pendingHalfIdx;
      pendingHalfIdx = -1;
      slaveOnNote(myScore[idx], NOTE_DURATION_HALF, NOTE_AMPLITUDE);
    }
  }
}

// ------------------------------------------------------------------------
// 親機ロジック: hack_oya_kai.ino と hack-koP2.ino の localPos 管理を統合
// ------------------------------------------------------------------------
long tickIntervalMs() {
  // hack_oya_kai.ino: interval = 60000 / BPM / 2 (1tick = 8分音符相当)
  return 60000L / bpm / 2L;
}

void masterTick() {
  localPos++;
  if (localPos >= TOTAL_TICKS) localPos = 0;

  if (localPos < DOUBLE_START) {
    slaveOnNote(myScore[(int) localPos], NOTE_DURATION_DEFAULT, NOTE_AMPLITUDE);
  } else {
    int scoreIdx = DOUBLE_START + 2 * (int)(localPos - DOUBLE_START);
    if (scoreIdx < SCORE_LENGTH) {
      slaveOnNote(myScore[scoreIdx], NOTE_DURATION_HALF, NOTE_AMPLITUDE);
    }
    if (scoreIdx + 1 < SCORE_LENGTH) {
      pendingHalfIdx = scoreIdx + 1;
      pendingHalfMs  = millis() + tickIntervalMs() / 2;
    }
  }
}

// ------------------------------------------------------------------------
// 子機ロジック: hackkosaisei/hackko.pde の serialEvent → playNote 相当
// ------------------------------------------------------------------------
void slaveOnNote(String pitch, float duration, float amplitude) {
  if (pitch.equals("R")) {
    if (currentInstrument != null) {
      currentInstrument.noteOff();
      currentInstrument = null;
    }
    return;
  }
  float freq = Frequency.ofPitch(pitch).asHz();
  currentInstrument = new PianoInstrument(freq, amplitude, waveform);
  out.playNote(0, duration, currentInstrument);
}

// ------------------------------------------------------------------------
// GUI 描画
// ------------------------------------------------------------------------
void drawBpmSlider() {
  fill(210);
  text("BPM: " + bpm + "  (" + BPM_MIN + "–" + BPM_MAX + ")", SLIDER_X, SLIDER_Y - 12);

  noStroke();
  fill(60);
  rect(SLIDER_X, SLIDER_Y, SLIDER_W, SLIDER_H, 4);

  float t = (float)(bpm - BPM_MIN) / (BPM_MAX - BPM_MIN);
  float knobX = SLIDER_X + t * SLIDER_W;
  fill(150, 200, 255);
  rect(SLIDER_X, SLIDER_Y, knobX - SLIDER_X, SLIDER_H, 4);
  fill(230);
  rect(knobX - 4, SLIDER_Y - 4, 8, SLIDER_H + 8, 3);
}

void drawTransportButton() {
  noStroke();
  fill(isPlaying ? color(200, 90, 90) : color(90, 190, 110));
  rect(BTN_X, BTN_Y, BTN_W, BTN_H, 6);
  fill(20);
  textSize(20);
  text(isPlaying ? "STOP" : "PLAY", BTN_X + 40, BTN_Y + 32);
  textSize(14);
}

void drawStatus() {
  fill(220);
  String note = "-";
  if (isPlaying && localPos >= 0 && localPos < TOTAL_TICKS) {
    int scoreIdx = (localPos < DOUBLE_START)
      ? (int) localPos
      : DOUBLE_START + 2 * (int)(localPos - DOUBLE_START);
    if (scoreIdx < SCORE_LENGTH) note = myScore[scoreIdx];
  }
  text("pos : " + localPos + " / " + TOTAL_TICKS, BTN_X + BTN_W + 20, BTN_Y + 20);
  text("note: " + note,                          BTN_X + BTN_W + 20, BTN_Y + 42);
  long ivl = tickIntervalMs();
  text("tick: " + ivl + " ms",                   BTN_X + BTN_W + 180, BTN_Y + 20);
}

void drawWaveform() {
  stroke(120, 220, 160);
  noFill();
  int y0 = 250;
  int step = max(1, out.bufferSize() / 460);
  float prevX = 20, prevY = y0;
  for (int i = 0; i < out.bufferSize(); i += step) {
    float x = 20 + (i / (float) out.bufferSize()) * 460;
    float y = y0 + out.left.get(i) * 30;
    line(prevX, prevY, x, y);
    prevX = x; prevY = y;
  }
  noStroke();
}

// ------------------------------------------------------------------------
// 入力
// ------------------------------------------------------------------------
void mousePressed() {
  if (hitSlider(mouseX, mouseY)) {
    sliderDragging = true;
    updateBpmFromMouse();
    return;
  }
  if (hitButton(mouseX, mouseY)) {
    toggleTransport();
  }
}

void mouseDragged() {
  if (sliderDragging) updateBpmFromMouse();
}

void mouseReleased() {
  sliderDragging = false;
}

void keyPressed() {
  if (key == ' ') toggleTransport();
}

boolean hitSlider(int mx, int my) {
  return mx >= SLIDER_X - 6 && mx <= SLIDER_X + SLIDER_W + 6
      && my >= SLIDER_Y - 8 && my <= SLIDER_Y + SLIDER_H + 8;
}

boolean hitButton(int mx, int my) {
  return mx >= BTN_X && mx <= BTN_X + BTN_W
      && my >= BTN_Y && my <= BTN_Y + BTN_H;
}

void updateBpmFromMouse() {
  float t = constrain((mouseX - SLIDER_X) / (float) SLIDER_W, 0f, 1f);
  bpm = round(BPM_MIN + t * (BPM_MAX - BPM_MIN));
}

void toggleTransport() {
  if (isPlaying) {
    isPlaying = false;
    if (currentInstrument != null) {
      currentInstrument.noteOff();
      currentInstrument = null;
    }
    pendingHalfIdx = -1;
  } else {
    isPlaying   = true;
    localPos    = -1;
    // 開始直後の最初の tick が即発火するよう、lastTickMs を 1 tick 前に置く。
    lastTickMs  = millis() - tickIntervalMs();
  }
}

// ------------------------------------------------------------------------
// hackkosaisei/hackko.pde からそのままコピーした音源クラス
// ------------------------------------------------------------------------
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

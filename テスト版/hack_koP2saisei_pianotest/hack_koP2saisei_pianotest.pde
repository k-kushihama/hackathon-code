// hack_koP2saisei_pianotest.pde
// kokitest の音源を「グランドピアノ風・物理モデリング加算合成」に差し替えた版。
// 親機ロジック(BPM+tick+楽譜)とGUIは hack_koP2saisei_kokitest.pde から無改変で継承。
//
// 音源の改良点（Pythonプロトタイプで音響検証7項目全合格のパラメータを移植）:
//   1. 複弦デチューン(0/+1.2/-0.8 cent)のうなり → 伸びやかさ・抱擁感
//   2. インハーモニシティ B=0.0002*(f0/261.6)^0.8 → 実ピアノ特有の倍音の張り
//   3. 倍音別2段階減衰 → 打鍵直後は明るく、すぐ豊かに丸くなる
//   4. アタック3ms + ハンマーノイズ15ms → 打鍵感
//   5. 音域依存の減衰長T60と倍音ロールオフp → 低音は長く豊か、高音は明るく短い
//   6. ダンパーリリース(τ=0.15s) → 自然な音の終わり
// 方式: 起動時に楽譜の全ピッチ×2種の長さをオフライン合成しSamplerにキャッシュ。
//       発音はtrigger()のみで実行時負荷は極小。

import ddf.minim.*;
import ddf.minim.ugens.*;
import java.util.HashMap;

Minim minim;
AudioOutput out;

// ---- ピアノ音キャッシュ ----------------------------------------------------
HashMap<String, Sampler> pianoCache = new HashMap<String, Sampler>();
final float SAMPLE_RATE  = 44100f;
final float RELEASE_TAIL = 0.8f;   // ノート長に加えてレンダリングする余韻(秒)

// ---- 楽譜: hack-koP2.ino からそのままコピー（kokitestと同一） --------------
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

// ---- 親機側: BPMとトランスポート（kokitestと同一） --------------------------
boolean isPlaying = false;
int bpm = 120;
final int BPM_MIN = 40;
final int BPM_MAX = 240;

long localPos   = -1;
long lastTickMs = 0;

int  pendingHalfIdx = -1;
long pendingHalfMs  = 0;

// ---- GUIレイアウト（kokitestと同一） ----------------------------------------
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

  // 楽譜に登場する全ピッチ×2種の長さをプリレンダリング
  long t0 = millis();
  println("ピアノ音源をプリレンダリング中...");
  for (int i = 0; i < SCORE_LENGTH; i++) {
    String pitch = myScore[i];
    if (pitch.equals("R")) continue;
    for (int d = 0; d < 2; d++) {
      float dur  = (d == 0) ? NOTE_DURATION_DEFAULT : NOTE_DURATION_HALF;
      String key = pitch + (d == 0 ? "_L" : "_S");
      if (!pianoCache.containsKey(key)) {
        pianoCache.put(key, buildSampler(pitch, dur));
        println("  " + key + " OK");
      }
    }
  }
  println("プリレンダ完了: " + pianoCache.size() + "音 / " + (millis() - t0) + "ms");
}

void draw() {
  background(30);

  fill(230);
  text("hack_koP2saisei_pianotest — グランドピアノ音源テスト (かえるのうた)", 20, 30);
  fill(160);
  text("Click PLAY / drag slider / SPACE toggles play", 20, 52);

  drawBpmSlider();
  drawTransportButton();
  drawStatus();
  drawWaveform();

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

// ---- 親機ロジック（kokitestと同一） -----------------------------------------
long tickIntervalMs() {
  return 60000L / bpm;
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

// ---- 子機ロジック: グランドピアノ音源で発音 ----------------------------------
void slaveOnNote(String pitch, float duration, float amplitude) {
  if (pitch.equals("R")) return;  // 休符: 余韻は自然減衰（ダンパーはレンダリング済み）
  String key = pitch + (duration >= 0.3f ? "_L" : "_S");
  Sampler smp = pianoCache.get(key);
  if (smp == null) {  // 安全網: 未キャッシュのピッチは都度生成
    smp = buildSampler(pitch, duration >= 0.3f ? NOTE_DURATION_DEFAULT : NOTE_DURATION_HALF);
    pianoCache.put(key, smp);
  }
  smp.trigger();
}

// ---- グランドピアノ音源（Python検証済みパラメータの移植） --------------------
Sampler buildSampler(String pitch, float dur) {
  float f0 = Frequency.ofPitch(pitch).asHz();
  float[] data = renderPianoNote(f0, dur, NOTE_AMPLITUDE);
  MultiChannelBuffer mcb = new MultiChannelBuffer(data.length, 1);
  mcb.setChannel(0, data);
  Sampler smp = new Sampler(mcb, SAMPLE_RATE, 6);  // 最大6声（余韻の重なり用）
  smp.patch(out);
  return smp;
}

float[] renderPianoNote(float f0, float dur, float amp) {
  int n = (int)((dur + RELEASE_TAIL) * SAMPLE_RATE);
  float[] buf = new float[n];

  // 複弦構成（実ピアノ: 高音ほど弦が多い）。デチューンはcent単位
  float[] cents;
  if      (f0 >= 350f) cents = new float[] { 0f, 1.2f, -0.8f };
  else if (f0 >= 150f) cents = new float[] { 0f, 1.0f };
  else                 cents = new float[] { 0f };

  float p       = 0.9f + 0.5f * constrain((f0 - 110f) / 770f, 0f, 1f);  // 倍音ロールオフ
  float B       = 0.0002f * pow(f0 / 261.6f, 0.8f);                     // インハーモニシティ
  float T60     = constrain(6.0f * pow(261.6f / f0, 0.7f), 0.8f, 8.0f); // 残響長(音域依存)
  float tauBase = T60 / 6.9f;
  float stringAmp = amp / cents.length;

  for (int s = 0; s < cents.length; s++) {
    float fs = f0 * pow(2f, cents[s] / 1200f);
    for (int h = 1; h <= 10; h++) {
      float fh = h * fs * sqrt(1f + B * h * h);   // インハーモニシティ補正周波数
      if (fh > SAMPLE_RATE * 0.45f) break;
      float ah   = stringAmp / pow(h, p);
      float tauH = tauBase / (1f + 0.6f * (h - 1));  // 高次倍音ほど速く減衰
      float w    = TWO_PI * fh / SAMPLE_RATE;
      for (int i = 0; i < n; i++) {
        float t   = i / SAMPLE_RATE;
        float env = 0.3f * exp(-t / 0.12f) + 0.7f * exp(-t / tauH);  // 2段階減衰
        buf[i] += ah * env * sin(w * i);
      }
    }
  }

  // ハンマーノイズ: 最初の15ms、1次LPF(fc=f0*8上限8k)、指数減衰
  float fc = min(f0 * 8f, 8000f);
  float a  = 1f - exp(-TWO_PI * fc / SAMPLE_RATE);
  float lp = 0f;
  int nNoise = (int)(0.015f * SAMPLE_RATE);
  randomSeed(12345);  // 打鍵音の再現性
  for (int i = 0; i < nNoise && i < n; i++) {
    float t = i / SAMPLE_RATE;
    float x = random(-1f, 1f);
    lp += a * (x - lp);
    buf[i] += amp * 0.15f * lp * exp(-t / 0.008f);
  }

  // アタック(3ms)とダンパーリリース(dur経過後 τ=0.15s)
  for (int i = 0; i < n; i++) {
    float t = i / SAMPLE_RATE;
    buf[i] *= (1f - exp(-t / 0.003f));
    if (t > dur) buf[i] *= exp(-(t - dur) / 0.15f);
  }

  // クリップ防止
  float peak = 0f;
  for (int i = 0; i < n; i++) peak = max(peak, abs(buf[i]));
  if (peak > 0.98f) {
    float g = 0.98f / peak;
    for (int i = 0; i < n; i++) buf[i] *= g;
  }
  return buf;
}

// ---- GUI描画（kokitestと同一） -----------------------------------------------
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

// ---- 入力（kokitestと同一。停止時はSampler全停止に変更） ----------------------
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
    pendingHalfIdx = -1;
    for (Sampler s : pianoCache.values()) s.stop();  // 即静音
  } else {
    isPlaying   = true;
    localPos    = -1;
    lastTickMs  = millis() - tickIntervalMs();
  }
}

import processing.serial.*;
Serial port;
import ddf.minim.*;
import ddf.minim.ugens.*;
import java.util.HashMap;
Minim minim;
AudioOutput out;
String currentNote = "";
int currentPos = -1;  // 親機が割り当ててきた localPos (デバッグ表示用)

// ---- グランドピアノ音源（音源部のみ差し替え。シリアル受信の仕組みは既存のまま） ----
// hack_koP2saisei_pianotest.pde と同一の合成アルゴリズム・同一パラメータ（音響検証7/7合格）。
// 方式: 既知ピッチは起動時にプリレンダしてSamplerにキャッシュ、発音はtrigger()のみ。
HashMap<String, Sampler> pianoCache = new HashMap<String, Sampler>();
Sampler lastSampler = null;  // 直近に鳴らした音。R受信時にstop()で即打ち切る（旧noteOff()と同じ役割）
final float SAMPLE_RATE  = 44100f;
final float RELEASE_TAIL = 0.8f;   // ノート長に加えてレンダリングする余韻(秒)

void setup() {
  size(512, 200);
  minim = new Minim(this);
  out = minim.getLineOut();

  // 楽譜(hack-koP2.ino)で使うピッチ×2種の長さを起動時にプリレンダリング。
  // 未知のピッチ/長さ/音量が来ても playNote 側の安全網で都度生成される。
  // ポートを開く前に済ませ、プリレンダ中のシリアル取りこぼしを避ける。
  String[] knownPitches = { "C4", "D4", "E4", "F4", "G4", "A4" };
  float[] knownDurs = { 0.40f, 0.20f };
  long t0 = millis();
  println("ピアノ音源をプリレンダリング中...");
  for (String pitch : knownPitches) {
    for (float dur : knownDurs) {
      String key = cacheKey(pitch, dur, 0.60f);
      pianoCache.put(key, buildSampler(pitch, dur, 0.60f));
      println("  " + key + " OK");
    }
  }
  println("プリレンダ完了: " + pianoCache.size() + "音 / " + (millis() - t0) + "ms");

  // portは各自変えて．
  port = new Serial(this, "/dev/cu.usbserial-A5069RR4", 115200);
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
  // R は休符: 直前の音の余韻が次のtickまで残らないよう、即stop()で打ち切る。
  // (NOTE_DURATION_DEFAULT=0.40s は tick間隔(120BPMで0.25s)より長いので、
  //  R が来た時点で前音がまだ鳴っているのを止めないと休符に聞こえない。)
  if (pitch.equals("R")) {
    if (lastSampler != null) {
      lastSampler.stop();
      lastSampler = null;
    }
    return;
  }
  playNote(pitch, duration, amplitude);
}

void playNote(String pitch, float duration, float amplitude) {
  String key = cacheKey(pitch, duration, amplitude);
  Sampler smp = pianoCache.get(key);
  if (smp == null) {  // 安全網: 未キャッシュの組み合わせは都度生成してキャッシュ
    smp = buildSampler(pitch, duration, amplitude);
    pianoCache.put(key, smp);
  }
  smp.trigger();
  lastSampler = smp;
}

String cacheKey(String pitch, float duration, float amplitude) {
  return pitch + "_" + nf(duration, 0, 2) + "_" + nf(amplitude, 0, 2);
}

// ---- グランドピアノ音源（pianotestから無改変移植。amplitudeのみ引数化） ------
Sampler buildSampler(String pitch, float dur, float amp) {
  float f0 = Frequency.ofPitch(pitch).asHz();
  float[] data = renderPianoNote(f0, dur, amp);
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

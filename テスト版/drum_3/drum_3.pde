import ddf.minim.*;
import ddf.minim.ugens.*;
import processing.serial.*;

Minim minim;
AudioOutput out;

// ==================================================
// シリアル通信
// ==================================================

Serial arduino;

// Serial.list()で表示されたポートの番号を指定する
int SERIAL_PORT_INDEX = 0;

// Arduino側と同じ通信速度
final int BAUD_RATE = 921600;

// Arduinoから送られるコマンド番号
final int NOTE_KICK = 1;
final int NOTE_CYMBAL = 4;

// 最後に受信した値
int lastReceivedNote = -1;
String serialStatus = "未接続";

// ==================================================
// キック
// ==================================================

Oscil kickOsc;
Multiplier kickAmp;

// 低音の余韻用
Oscil kickSubOsc;
Multiplier kickSubAmp;

// パンチ用の倍音
Oscil kickHarmonic;
Multiplier kickHarmonicAmp;

// アタックの「コツッ」という成分
Oscil kickClickOsc;
Multiplier kickClickOscAmp;

Noise kickClickNoise;
Multiplier kickClickNoiseAmp;

// 連打時に古いキック処理が新しい音を邪魔しないようにする
volatile int kickTriggerId = 0;
Object kickLock = new Object();


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
  size(650, 280);

  minim = new Minim(this);
  out = minim.getLineOut(Minim.MONO, 512);

  setupKick();
  setupCymbal();
  setupSerial();

  textSize(18);
}

// ==================================================
// キックの初期設定
// ==================================================

void setupKick() {
  // キック本体
  kickOsc = new Oscil(
    55.0f,
    1.0f,
    Waves.SINE
  );

  kickAmp = new Multiplier(0.0f);

  kickOsc
    .patch(kickAmp)
    .patch(out);

  // サブ低音の余韻
  kickSubOsc = new Oscil(
    50.0f,
    1.0f,
    Waves.SINE
  );

  kickSubAmp = new Multiplier(0.0f);

  kickSubOsc
    .patch(kickSubAmp)
    .patch(out);

  // 短いパンチ成分
  kickHarmonic = new Oscil(
    110.0f,
    1.0f,
    Waves.SINE
  );

  kickHarmonicAmp = new Multiplier(0.0f);

  kickHarmonic
    .patch(kickHarmonicAmp)
    .patch(out);

  // ビーターのクリック感
  kickClickOsc = new Oscil(
    3000.0f,
    1.0f,
    Waves.SINE
  );

  kickClickOscAmp = new Multiplier(0.0f);

  kickClickOsc
    .patch(kickClickOscAmp)
    .patch(out);

  // 短いノイズでアタックを足す
  kickClickNoise = new Noise(
    1.0f,
    Noise.Tint.WHITE
  );

  kickClickNoiseAmp = new Multiplier(0.0f);

  kickClickNoise
    .patch(kickClickNoiseAmp)
    .patch(out);
}

// ==================================================
// シンバルの初期設定
// ==================================================

void setupCymbal() {
  // ノイズ成分
  cymNoise = new Noise(
    1.0,
    Noise.Tint.WHITE
  );

  cymNoiseAmp = new Multiplier(0.0);

  cymNoise
    .patch(cymNoiseAmp)
    .patch(out);

  // 金属成分
  cymMetal1 = new Oscil(
    431.0,
    1.0,
    Waves.SQUARE
  );

  cymMetal2 = new Oscil(
    647.0,
    1.0,
    Waves.SQUARE
  );

  cymMetal3 = new Oscil(
    913.0,
    1.0,
    Waves.SQUARE
  );

  cymMetalAmp1 = new Multiplier(0.0);
  cymMetalAmp2 = new Multiplier(0.0);
  cymMetalAmp3 = new Multiplier(0.0);

  cymMetal1
    .patch(cymMetalAmp1)
    .patch(out);

  cymMetal2
    .patch(cymMetalAmp2)
    .patch(out);

  cymMetal3
    .patch(cymMetalAmp3)
    .patch(out);
}

// ==================================================
// シリアル通信の初期設定
// ==================================================

void setupSerial() {
  // 使用可能なシリアルポートをコンソールに表示
  println("使用可能なシリアルポート:");

  String[] ports = Serial.list();

  for (int i = 0; i < ports.length; i++) {
    println(i + ": " + ports[i]);
  }

  if (ports.length == 0) {
    serialStatus = "シリアルポートが見つかりません";
    println(serialStatus);
    return;
  }

  if (
    SERIAL_PORT_INDEX < 0 ||
    SERIAL_PORT_INDEX >= ports.length
  ) {
    serialStatus = "ポート番号の指定が正しくありません";
    println(serialStatus);
    return;
  }

  try {
    String portName = ports[SERIAL_PORT_INDEX];

    arduino = new Serial(
      this,
      portName,
      BAUD_RATE
    );

    /*
     * Arduino側がSerial.println()で送っているため、
     * 改行を受信した時点でserialEvent()を呼び出す。
     */
    arduino.bufferUntil('\n');

    // 接続直後に残っているデータを消去
    arduino.clear();

    serialStatus = "接続中: " + portName;
    println(serialStatus);
  }
  catch (Exception e) {
    serialStatus = "接続失敗";
    println("シリアル接続に失敗しました");
    println(e.getMessage());

    arduino = null;
  }
}

// ==================================================
// 画面描画
// ==================================================

void draw() {
  background(245);
  fill(0);

  text("キーボード操作", 30, 35);
  text("1: Kick   4: Cymbal", 30, 65);
  text("A: Auto play ON/OFF", 30, 95);

  text(
    "Auto: " + (autoPlay ? "ON" : "OFF"),
    30,
    130
  );

  text("シリアル通信", 30, 175);
  text(serialStatus, 30, 205);

  if (lastReceivedNote >= 0) {
    text(
      "最後に受信した値: " + lastReceivedNote,
      30,
      235
    );
  } else {
    text(
      "最後に受信した値: なし",
      30,
      235
    );
  }

  // 500ミリ秒ごとの自動演奏
  if (
    autoPlay &&
    millis() - lastTriggerTime >= 500
  ) {
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
// Arduinoからデータを受信
// ==================================================

void serialEvent(Serial port) {
  String receivedText = port.readStringUntil('\n');

  if (receivedText == null) {
    return;
  }

  // 改行や空白を取り除く
  receivedText = trim(receivedText);

  if (receivedText.length() == 0) {
    return;
  }

  try {
    int note = Integer.parseInt(receivedText);

    lastReceivedNote = note;

    println("Arduinoから受信: " + note);

    handleReceivedNote(note);
  }
  catch (NumberFormatException e) {
    println(
      "数値として解釈できないデータ: " +
      receivedText
    );
  }
}

// ==================================================
// 受信した番号に応じて発音
// ==================================================

void handleReceivedNote(int note) {
  if (note == NOTE_KICK) {
    playKickSound();
  } else if (note == NOTE_CYMBAL) {
    playCymbalSound();
  } else {
    println(
      "未登録のコマンド番号です: " + note
    );
  }
}

// ==================================================
// キー入力
// ==================================================

void keyPressed() {
  if (key == '1') {
    playKickSound();
  } else if (key == '4') {
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

float smooth01(float x) {
  x = constrain(x, 0.0f, 1.0f);
  return x * x * (3.0f - 2.0f * x);
}

void silenceKick() {
  if (kickAmp != null) {
    kickAmp.setValue(0.0f);
  }

  if (kickSubAmp != null) {
    kickSubAmp.setValue(0.0f);
  }

  if (kickHarmonicAmp != null) {
    kickHarmonicAmp.setValue(0.0f);
  }

  if (kickClickOscAmp != null) {
    kickClickOscAmp.setValue(0.0f);
  }

  if (kickClickNoiseAmp != null) {
    kickClickNoiseAmp.setValue(0.0f);
  }
}


void playKickSound() {
  int id;

  synchronized (kickLock) {
    kickTriggerId++;
    id = kickTriggerId;
  }

  final int myKickId = id;

  new Thread(new Runnable() {
    public void run() {
      final int durationMs = 560;
      final int updateMs = 1;
      final float durationSec = durationMs / 1000.0f;

      int steps = durationMs / updateMs;

      if (myKickId != kickTriggerId) {
        return;
      }

      silenceKick();

      for (int i = 0; i <= steps; i++) {
        if (myKickId != kickTriggerId) {
          return;
        }

        float sec = i * updateMs / 1000.0f;

        // 170Hz付近から50Hz付近へ落ちるピッチエンベロープ
        // 速い下降 + 少し遅い下降を混ぜるとキックらしくなる
        float bodyFreq =
          50.0f +
          92.0f * exp(-sec / 0.014f) +
          26.0f * exp(-sec / 0.075f);

        // サブはあまり動かさず、低音の芯として使う
        float subFreq =
          49.0f +
          10.0f * exp(-sec / 0.090f);

        // 本体のアタック
        float bodyAttack =
          smooth01(sec / 0.0035f);

        // サブは少しだけ遅く立ち上げる
        float subAttack =
          smooth01(sec / 0.006f);

        // 最後をなめらかに消して、音切れノイズを防ぐ
        float endFade =
          1.0f -
          smooth01(
            (sec - (durationSec - 0.045f)) / 0.045f
          );

        // 本体の胴鳴り
        float bodyEnv =
          (
            0.72f * exp(-sec / 0.170f) +
            0.28f * exp(-sec / 0.050f)
          ) *
          bodyAttack *
          endFade;

        // 低音の余韻
        float subEnv =
          exp(-sec / 0.300f) *
          subAttack *
          endFade;

        // 短いパンチ成分
        float punchEnv =
          exp(-sec / 0.040f) *
          bodyAttack *
          endFade;

        // アタッククリック
        float clickToneEnv =
          exp(-sec / 0.006f);

        float clickNoiseEnv =
          exp(-sec / 0.0045f);

        kickOsc.setFrequency(bodyFreq);
        kickSubOsc.setFrequency(subFreq);
        kickHarmonic.setFrequency(bodyFreq * 2.02f);

        // クリックの音程を少しだけ下げる
        kickClickOsc.setFrequency(
          3300.0f -
          900.0f * smooth01(sec / 0.018f)
        );

        kickAmp.setValue(
          0.58f * bodyEnv
        );

        kickSubAmp.setValue(
          0.28f * subEnv
        );

        kickHarmonicAmp.setValue(
          0.070f * punchEnv
        );

        kickClickOscAmp.setValue(
          0.032f * clickToneEnv
        );

        kickClickNoiseAmp.setValue(
          0.045f * clickNoiseEnv
        );

        sleepMs(updateMs);
      }

      if (myKickId == kickTriggerId) {
        silenceKick();
      }
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

        float fastDecay =
          exp(-13.0 * t);

        float slowDecay =
          exp(-4.0 * t);

        float noiseEnvelope =
          0.25 * fastDecay +
          0.10 * slowDecay;

        float metalEnvelope =
          0.09 * exp(-9.0 * t);

        float attack =
          constrain(t * 45.0, 0.0, 1.0);

        cymNoiseAmp.setValue(
          noiseEnvelope * attack
        );

        cymMetalAmp1.setValue(
          metalEnvelope * attack
        );

        cymMetalAmp2.setValue(
          metalEnvelope * 0.75 * attack
        );

        cymMetalAmp3.setValue(
          metalEnvelope * 0.50 * attack
        );

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
// スレッド内の待機処理
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
  kickTriggerId++;
  silenceKick();

  if (cymNoiseAmp != null) {
    cymNoiseAmp.setValue(0.0f);
  }

  if (cymMetalAmp1 != null) {
    cymMetalAmp1.setValue(0.0f);
  }

  if (cymMetalAmp2 != null) {
    cymMetalAmp2.setValue(0.0f);
  }

  if (cymMetalAmp3 != null) {
    cymMetalAmp3.setValue(0.0f);
  }

  if (arduino != null) {
    arduino.stop();
  }

  if (out != null) {
    out.close();
  }

  if (minim != null) {
    minim.stop();
  }

  super.stop();
}

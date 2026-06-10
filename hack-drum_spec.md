# hack-drum.ino 仕様

## 概要
ドラム担当の子機Arduino．親機からIRで届くドラム用のMIDI番号をシリアル経由でProcessingに渡すだけ．

## ハードウェア
- Arduino UNO R4
- IR受光モジュール CHQ1838D
- D2 = 受光信号入力，D13 = 動作LED，USB = PCへ

## ソフトウェア
- ライブラリ: IRremote v4.x (NEC)
- `CHILD_ID = 2` (ドラム)
- 受信: `address == 2` のNECフレームだけ拾う
- 送信: 受信した `command` (MIDI番号) を `Serial.println` で送るのみ

## 動作
1. IR受信
2. NEC かつ address=2 なら → command を Serial に出力 + LED 短時間点灯
3. 自前タイマなし．**テンポは親機任せ**なので可変BPMに自動追従

## 設計の特徴
- 輪唱に参加しない (`CANON_OFFSET` 無関係)
- 楽譜を持たない (親機側で `drumPattern[]` 管理)
- ステートレス．受信→中継のみ

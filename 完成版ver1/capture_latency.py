#!/usr/bin/env python3
"""子機のシリアル出力をキャプチャしてlatency.txtに保存する。

使い方:
  1. Arduino IDE のシリアルモニタを閉じる
  2. python3 capture_latency.py
  3. 親機の Arduino IDE シリアルモニタで 't' を送信
  4. テスト完了まで待つ（自動終了する）
"""
import serial
import sys
import time

PORT = '/dev/cu.usbserial-A5069RR4'
BAUD = 115200
OUTPUT = 'latency.txt'

ser = serial.Serial(PORT, BAUD, timeout=1)
time.sleep(2)  # DTRリセット後の起動待ち
ser.reset_input_buffer()

# テストモード開始
ser.write(b't\n')
print("子機にテストモード開始を送信しました")
print("親機のシリアルモニタで 't' を送信してください")
print("---")

lines = []
recv_count = 0
done = False

while not done:
    raw = ser.readline()
    if not raw:
        continue
    try:
        line = raw.decode('utf-8', errors='ignore').strip()
    except:
        continue
    if not line:
        continue

    print(line)
    lines.append(line)

    # CSV行をカウント
    parts = line.split(',')
    if len(parts) == 2 and parts[0].isdigit():
        recv_count = int(parts[0])

    # サマリー行でテスト完了を検知
    if '[TEST] lost:' in line:
        done = True

ser.write(b't\n')  # テストモード終了
ser.close()

with open(OUTPUT, 'w', encoding='utf-8') as f:
    for l in lines:
        f.write(l + '\n')

print("---")
print(f"保存完了: {OUTPUT} ({len(lines)} 行)")

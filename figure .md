```mermaid
sequenceDiagram
    participant A as Arduino
    participant P as Processing (PC)
    
    Note over P: 起動・シリアルポート準備
    P->>A: 送信リクエストを送る
    Note over A: リクエスト受信 (Serial.available > 0)
    
    A->>P: 演奏データ一括送信 (melody:duration:startTime:amplitude,...)
    Note over P: serialEvent() でデータ受信
    Note over P: 文字列をパースして各配列に格納
    
    Note over P: ユーザーが ボタンを押下
    P->>P: playSong() 実行
    Note over P: スピーカーから音声を生成・出力

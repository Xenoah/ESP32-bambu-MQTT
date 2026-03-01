# ESP32-bambu-MQTT

ESP32-S3-BOX-Lite で Bambu Printer の MQTT 情報を受信し、ディスプレイに状態を表示する Arduino / PlatformIO プロジェクトです。

## 構成

- ボード: ESP32-S3-BOX-Lite
- フレームワーク: Arduino
- 描画ライブラリ: LovyanGFX
- 通信:
  - Wi-Fi
  - MQTT over TLS (`8883`)

## ファイル構成

- `src/AppConfig.h`
  - Wi-Fi、MQTT、プリンタ識別子、タイムアウト、ループ周期
- `src/AppState.h`
  - 画面表示と通信処理で共有する状態
- `src/DisplayManager.cpp`
  - LovyanGFX による画面初期化と描画
- `src/PrinterComm.cpp`
  - Wi-Fi 接続、MQTT 接続、受信 JSON 解析、G-code 送信
- `src/main.cpp`
  - 初期化と `setup()` 内の常駐ループ

## 使い方

1. `src/AppConfig.h` を編集します。
2. 次の値を環境に合わせて設定します。
   - `kWifiSsid`
   - `kWifiPassword`
   - `kPrinterHost`
   - `kPrinterPort`
   - `kPrinterUser`
   - `kPrinterPassword`
   - `kPrinterSerial`
3. ビルドします。

```powershell
pio run
```

4. 実機へ書き込みます。

```powershell
pio run -t upload
```

5. シリアルログを確認する場合は monitor を開きます。

```powershell
pio device monitor -b 115200
```

## 表示仕様

通常画面では以下を表示します。

- `WIFI`
  - ESP32 の Wi-Fi 状態
- `IP`
  - ESP32 のローカル IP
- `MQTT`
  - 現在のイベントまたは通信状態
- `BED`
  - ベッド温度
- `NOZZLE`
  - ノズル温度
- `P_WIFI`
  - プリンタ側 Wi-Fi 信号
- `PROG`
  - 進捗率
- `LAYER`
  - 現在レイヤ / 総レイヤ
- `STATE`
  - プリント状態
- `HOMING`
  - ホーミング状態
- `SEQ`
  - シーケンス ID
- `PRINTER`
  - プリンタのシリアル番号
- `UPTIME`
  - 起動後経過秒数

## エラー仕様

次の条件では自動再起動せず停止し、赤いエラー画面を表示します。

- Wi-Fi 接続タイムアウト
- Wi-Fi 切断
- MQTT 接続失敗上限到達

エラー画面には次を表示します。

- `REASON`
- `WIFI`
- `IP`
- `MQTT`

## 通信仕様

- MQTT 接続先は `device/<serial>/report` を subscribe します。
- コマンド送信先は `device/<serial>/request` を publish します。
- `push_status` を受信すると表示値を更新します。
- MQTT 接続成功時に次を送信します。
  - `G28`
  - `get_position`

## 実装方針

- 通信処理と描画処理は分離しています。
- `PrinterComm` は状態更新のみ行います。
- `DisplayManager` は描画のみ行います。
- `setup()` の中で無限ループを回し、`loop()` は空です。

## 依存ライブラリ

- `knolleary/PubSubClient`
- `bblanchon/ArduinoJson`
- `lovyan03/LovyanGFX`

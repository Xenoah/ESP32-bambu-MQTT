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
  - LovyanGFX によるスプライトダブルバッファ描画
- `src/PrinterComm.cpp`
  - Wi-Fi 接続、MQTT 接続、受信 JSON 解析、ステータス要求
- `src/main.cpp`
  - 初期化と `setup()` 内の常駐ループ

## 使い方

1. `src/AppConfig.h` を編集します。
2. 次の値を環境に合わせて設定します。

   | 定数 | 内容 |
   |------|------|
   | `kWifiSsid` | Wi-Fi SSID |
   | `kWifiPassword` | Wi-Fi パスワード |
   | `kPrinterHost` | プリンタの IP アドレス |
   | `kPrinterPort` | `8883` (変更不要) |
   | `kPrinterUser` | `bblp` (Bambu Lab LAN 固定値・変更不要) |
   | `kPrinterPassword` | プリンタの **Access Code** (設定画面に表示) |
   | `kPrinterSerial` | プリンタのシリアル番号 |

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

- `WIFI` — ESP32 の Wi-Fi 状態
- `IP` — ESP32 のローカル IP
- `MQTT` — 現在のイベントまたは通信状態
- `BED` — ベッド温度
- `NOZZLE` — ノズル温度
- `P_WIFI` — プリンタ側 Wi-Fi 信号 (dBm)
- `PROG` — 進捗率
- `LAYER` — 現在レイヤ / 総レイヤ
- `STATE` — プリント状態
- `HOMING` — ホーミング状態 (IDLE / HOMING / DONE)
- `SEQ` — シーケンス ID
- `PRINTER` — プリンタのシリアル番号
- `UPTIME` — 起動後経過秒数

## エラー仕様

次の条件では自動再起動せず停止し、赤いエラー画面を表示します。

- Wi-Fi 接続タイムアウト → `WIFI ERROR`
- Wi-Fi 切断 → `WIFI ERROR`
- MQTT 接続失敗上限到達 → `MQTT ERROR`

エラー画面には次を表示します。

- `REASON` — エラー理由
- `WIFI` — Wi-Fi 状態
- `IP` — IP アドレス
- `MQTT` — MQTT イベント

## 通信仕様

- Subscribe: `device/<serial>/report`
- Publish: `device/<serial>/request`
- 受信した `push_status` コマンドで表示値を更新します。
- MQTT 接続成功時に `{"pushing": {"command": "pushall"}}` を送信してステータス全量を要求します。
- コマンドは retain=false で送信します（ブローカーへのコマンド残留を防止）。

## 実装方針

- 通信処理と描画処理は分離しています。
- `PrinterComm` は状態更新のみ行います。
- `DisplayManager` は描画のみ行います。
- `LGFX_Sprite` による全画面ダブルバッファで描画フリッカーを排除します。
- `setup()` の中で無限ループを回し、`loop()` は空です。

## 依存ライブラリ

- `knolleary/PubSubClient`
- `bblanchon/ArduinoJson`
- `lovyan03/LovyanGFX`

## 参考資料

### Bambu Lab MQTT プロトコル

- [OpenBambuAPI — mqtt.md](https://github.com/Doridian/OpenBambuAPI/blob/main/mqtt.md)
  - Bambu Lab の LAN MQTT コマンド体系の詳細仕様。トピック名・コマンド名・JSON 構造・`sequence_id` の型・`"print"` / `"pushing"` ラッパーの使い分けを確認するために参照。
- [Bambu Lab X1/X1C MQTT — Home Assistant Community](https://community.home-assistant.io/t/bambu-lab-x1-x1c-mqtt/489510)
  - 実機での MQTT メッセージ例、`push_status` フィールド一覧、LAN 接続時のユーザー名 `bblp` の確認に参照。

### LovyanGFX

- [LovyanGFX — LGFX_AutoDetect_ESP32_all.hpp (GitHub)](https://github.com/lovyan03/LovyanGFX/blob/master/src/lgfx/v1_autodetect/LGFX_AutoDetect_ESP32_all.hpp)
  - ESP32-S3-BOX 系ボードの自動検出ロジックを確認。`#define LGFX_ESP32_S3_BOX_LITE` と `LGFX_AUTODETECT.hpp` を同時に使用すると競合することを特定するために参照。
- [LovyanGFX — Issue #569 (ESP32-S3-BOX v3 対応)](https://github.com/lovyan03/LovyanGFX/issues/569)
  - S3-BOX 系ボードのパネル検出問題の事例として参照。

### ESP32 信頼性

- [ESP-IDF Task Watchdog Timer](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/wdts.html)
  - MQTT リトライ中の長時間ブロッキング `delay()` が WDT リセットを引き起こすリスクを評価するために参照。
- [Arduino String — Avoiding Fragmentation](https://www.forward.com.au/pfod/ArduinoProgramming/ArduinoStrings/index.html)
  - MQTT コールバック内での `String` 動的アロケーションによるヒープ断片化問題の対処方針として参照。

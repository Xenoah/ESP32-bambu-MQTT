# ESP32-bambu-MQTT

ESP32-S3-BOX-Lite で Bambu Lab 3D プリンタの状態を LAN 経由で受信し、本体ディスプレイにリアルタイム表示する PlatformIO / Arduino プロジェクトです。

---

## 必要なもの

| 項目 | 詳細 |
|------|------|
| ハードウェア | ESP32-S3-BOX-Lite |
| プリンタ | Bambu Lab 製（X1C / P1S / P1P / A1 など） |
| ネットワーク | ESP32 とプリンタが同じ LAN に接続されていること |
| ソフトウェア | [PlatformIO](https://platformio.org/) (VS Code 拡張または CLI) |

---

## プリンタ側の事前設定

### LAN モードを有効にする

プリンタの **設定 → ネットワーク → LAN Only Liveview** をオンにします。

> LAN Only Liveview をオンにしないと MQTT がローカル接続を受け付けません。

### 必要な情報をメモしておく

プリンタの設定画面（タッチスクリーン）で以下を確認します。

| 情報 | 場所 | 例 |
|------|------|----|
| IP アドレス | 設定 → ネットワーク → Wi-Fi | `192.168.1.50` |
| Access Code | 設定 → ネットワーク → Access Code | `12345678` |
| シリアル番号 | 設定 → デバイス情報 → シリアル | `01S00C123456789` |

---

## セットアップ手順

### 1. リポジトリをクローンする

```bash
git clone https://github.com/<your-name>/ESP32-bambu-MQTT.git
cd ESP32-bambu-MQTT
```

### 2. 資格情報ファイルを作成する

`src/AppSecrets.h.template` をコピーして `src/AppSecrets.h` を作ります。

```powershell
# PowerShell
Copy-Item src\AppSecrets.h.template src\AppSecrets.h
```

```bash
# bash / Mac / Linux
cp src/AppSecrets.h.template src/AppSecrets.h
```

> **注意:** `src/AppSecrets.h` は `.gitignore` に登録済みです。
> git には絶対コミットされません。実際の値を安心して書いてください。

### 3. 資格情報を入力する

`src/AppSecrets.h` をエディタで開き、以下の 5 か所を書き換えます。

```cpp
constexpr char kWifiSsid[]       = "YOUR_WIFI_SSID";      // ← Wi-Fi の SSID
constexpr char kWifiPassword[]   = "YOUR_WIFI_PASSWORD";  // ← Wi-Fi のパスワード
constexpr char kPrinterHost[]    = "192.168.x.x";         // ← プリンタの IP アドレス
constexpr char kPrinterPassword[] = "YOUR_ACCESS_CODE";   // ← Access Code
constexpr char kPrinterSerial[]  = "YOUR_SERIAL_NUMBER";  // ← シリアル番号
```

> `kPrinterUser`（`"bblp"`）と `kPrinterPort`（`8883`）は変更不要です。
> これらは Bambu Lab LAN MQTT の固定値です。

### 4. ビルドして書き込む

```powershell
# ビルドのみ（エラー確認）
pio run

# ビルド + ESP32-S3-BOX-Lite へ書き込み
pio run -t upload
```

> 初回ビルド時は `src/AppSecrets.h` が自動作成されます（手順 2 を済ませていない場合）。
> その場合はファイルに値を書いて再度 `pio run -t upload` を実行してください。

### 5. シリアルログを確認する（任意）

```powershell
pio device monitor -b 115200
```

---

## 起動の流れ

書き込み後、ESP32-S3-BOX-Lite の電源を入れると次の順番で動作します。

```
[電源オン]
    ↓
[スプラッシュ画面]
  BAMBU MQTT
  ESP32-S3-BOX-LITE
    ↓
[起動ターミナル画面]        ← 接続の進行状況がリアルタイムで流れる
  > JOIN WIFI
  > WIFI READY
  > MQTT 1/5
  > MQTT READY
  > SUB OK
  > PUSHALL SENT           ← 最新行は白、過去の行はグレーで表示
    ↓
[ダッシュボード画面]        ← 接続成功後に切り替わる
  プリンタの状態を表示
```

---

## 画面の見方

### 通常時（ダッシュボード）

ヘッダーバーの色でおおよその状態がわかります。

| 色 | 意味 |
|----|------|
| 緑 | MQTT 接続中・正常 |
| オレンジ | 接続待ち・初期化中 |
| 赤 | MQTT エラー |

各フィールドの内容：

| フィールド | 内容 |
|-----------|------|
| `WIFI` | ESP32 の Wi-Fi 状態（OK / CONNECT など）|
| `IP` | ESP32 に割り当てられた IP アドレス |
| `MQTT` | 直近のイベント（MQTT READY / PUSH STATUS など）|
| `BED` | ベッド温度（℃）|
| `NOZZLE` | ノズル温度（℃）|
| `P_WIFI` | プリンタ側の Wi-Fi 電波強度（dBm）|
| `PROG` | 印刷進捗率（%）|
| `LAYER` | 現在レイヤ / 総レイヤ数 |
| `STATE` | 印刷状態（IDLE / RUNNING / FINISH など）|
| `HOMING` | ホーミング状態（IDLE / HOMING / DONE）|
| `SEQ` | プリンタから受け取った最新シーケンス ID |
| `PRINTER` | プリンタのシリアル番号 |
| `UPTIME` | ESP32 の起動後経過秒数 |

### エラー時（赤い画面）

次の条件で自動停止し、赤いエラー画面を表示します。**自動再起動はしません。**

| 原因 | タイトル |
|------|---------|
| Wi-Fi に接続できなかった（タイムアウト）| `WIFI ERROR` |
| 動作中に Wi-Fi が切断された | `WIFI ERROR` |
| MQTT 接続を 5 回試みてすべて失敗 | `MQTT ERROR` |

エラー画面の見方：

| 表示 | 内容 |
|------|------|
| `REASON` | エラーの具体的な理由 |
| `WIFI` | ESP32 の Wi-Fi 状態 |
| `IP` | ESP32 の IP アドレス |
| `MQTT` | MQTT の最終イベント |

**対処方法:** `src/AppSecrets.h` の設定を確認し、ESP32 をリセットしてください。

---

## よくある問題

### Wi-Fi に繋がらない

- SSID・パスワードを確認してください。
- 2.4GHz 帯の Wi-Fi を使っていることを確認してください（ESP32 は 5GHz 非対応）。

### MQTT ERR -2 / ERR -4 が出る

- プリンタの IP アドレスが正しいか確認してください。
- プリンタの LAN Only Liveview がオンになっているか確認してください。
- Access Code が合っているか確認してください（プリンタ設定画面で再確認）。

### 画面が真っ暗のまま / 一瞬だけ光る

- LovyanGFX のパネル自動検出が失敗している可能性があります。
- `src/DisplayManager.h` でマニュアルパネル設定クラスを定義する対応が必要です。

### ビルドエラー: `AppSecrets.h: No such file or directory`

`src/AppSecrets.h` が存在しません。手順 2 と 3 を実施してください。

---

## 設定値のカスタマイズ

`src/AppConfig.h` に接続・タイミング系の定数があります。

| 定数 | デフォルト | 説明 |
|------|-----------|------|
| `kWifiConnectTimeoutMs` | `20000` | Wi-Fi 接続タイムアウト（ms）|
| `kMqttMaxRetries` | `5` | MQTT 接続リトライ上限回数 |
| `kMqttRetryDelayMs` | `5000` | MQTT リトライ間隔（ms）|
| `kActiveLoopDelayMs` | `10` | 通常動作時のループ周期（ms）|
| `kHaltedLoopDelayMs` | `250` | エラー停止中のループ周期（ms）|

---

## 依存ライブラリ

PlatformIO が自動でインストールします。手動操作は不要です。

| ライブラリ | バージョン | 用途 |
|-----------|-----------|------|
| `knolleary/PubSubClient` | ^2.8 | MQTT クライアント |
| `bblanchon/ArduinoJson` | ^7.4.2 | JSON パース |
| `lovyan03/LovyanGFX` | ^1.2.19 | ディスプレイ描画 |

---

## 参考資料

### Bambu Lab MQTT プロトコル

- [OpenBambuAPI — mqtt.md](https://github.com/Doridian/OpenBambuAPI/blob/main/mqtt.md)
  - トピック名・コマンド名・JSON 構造・`"print"` / `"pushing"` ラッパーの使い分けなど、LAN MQTT の詳細仕様。
- [Bambu Lab X1/X1C MQTT — Home Assistant Community](https://community.home-assistant.io/t/bambu-lab-x1-x1c-mqtt/489510)
  - 実機での MQTT メッセージ例、`push_status` フィールド一覧、LAN 接続ユーザー名 `bblp` の情報。

### LovyanGFX

- [LovyanGFX — LGFX_AutoDetect_ESP32_all.hpp](https://github.com/lovyan03/LovyanGFX/blob/master/src/lgfx/v1_autodetect/LGFX_AutoDetect_ESP32_all.hpp)
  - ESP32-S3-BOX 系の自動検出ロジック。`#define` と AUTODETECT の競合原因を特定するために参照。
- [LovyanGFX — Issue #569](https://github.com/lovyan03/LovyanGFX/issues/569)
  - S3-BOX 系ボードのパネル検出問題の事例。

### ESP32 信頼性

- [ESP-IDF Task Watchdog Timer](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/wdts.html)
  - MQTT リトライ中の長時間 `delay()` による WDT リセットのリスク評価。
- [Arduino String — Avoiding Fragmentation](https://www.forward.com.au/pfod/ArduinoProgramming/ArduinoStrings/index.html)
  - MQTT コールバック内の `String` 動的確保によるヒープ断片化対策の背景。

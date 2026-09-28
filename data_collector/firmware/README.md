# Data Collector Firmware

EK-RA8P1 の OV5640 から 640x480 YUV422 を 5 fps で採取し、USB HS CDC-ACMでPCへ送るFreeRTOSファームウェアです。JPEG化はPC側で行います。

## 実装構成

## 0から画像を収集する手順

### 1. 事前準備

- EK-RA8P1、OV5640、USBケーブル2本（デバッグ用とCDC通信用）を用意する。
- e2 studio 2026-01とFSP 6.4.0をインストールする。FSP 6.2のstandalone RASCは使用しない。
- Arm GNU Toolchain 13.2.Rel1をインストールする。
- Ninja、CMake、SEGGER J-Link Softwareをインストールする。
- ボードのデバッグUSBをPCへ接続し、カメラを接続する。PCのUSB CDCポートは、ファームを書き込んだ後に別のUSB接続で認識される。

### 2. e2 studioプロジェクトを準備する

`data_collector/firmware`にはe2 studioの`.project`/`.cproject`がないため、`configuration.xml`をファイルとして直接開いてはいけない。e2 studioが「対応していないファイル」と表示するのはこのためである。

1. e2 studioを起動し、専用のワークスペースを開く。
2. **File > New > Renesas RA C/C++ Project**を選択する。
3. Project nameを`data_collector_fsp`などにし、Project locationにはまだ存在しない空のフォルダー（例：`D:\e2studio-workspace\data_collector_fsp`）を指定する。既存の`data_collector/firmware`は指定しない。
4. EK-RA8P1、MCU `R7KA8P1KFLCAC`、CPU0、FreeRTOS、Arm GNU Embedded GCCを選択してプロジェクトを作成する。
5. 作成されたプロジェクト内の`configuration.xml`を、リポジトリの`data_collector/firmware/configuration.xml`で置き換える。e2 studioを終了してから置き換え、再起動後にProject ExplorerでプロジェクトをRefreshする。
6. プロジェクト内の`configuration.xml`を右クリックし、**Open With > Renesas Smart Configurator**（または**Open Smart Configurator**）を選択する。

このエラーが出た場合は、プロジェクト作成をキャンセルし、空の別フォルダーを指定してやり直す。既存の`data_collector/firmware`を削除したり、上書き指定したりしない。生成完了後、別フォルダーの`ra_gen`、`ra_cfg`、および追加されたFSPドライバーを元の`data_collector/firmware`へコピーする。

既存プロジェクトを使う場合は、`test/e2studio`を**File > Import > Existing Projects into Workspace**でインポートできる。ただしこれは`test`用のプロジェクトなので、data collector用の生成先としてそのまま使わない。

### 3. FSPコードを生成する（初回または設定変更時のみ）

Smart Configuratorが開いたら、次を設定する。

1. USB Basic (`r_usb_basic`) とPeripheral CDC (`r_usb_pcdc`) を追加し、USB High Speed Deviceに設定する。Basicインスタンス名は `g_basic0` にする。
2. USB callbackを `collector_usb_callback` に設定する。
3. VINが640x480、YCbCr422、stride 640、SDRAM 3面バッファになっていることを確認する。
4. Generate Project Contentを実行する。

生成後に次を確認する。どれかが不足していれば、実機用ビルドは意図的に停止する。

```powershell
Select-String -Path data_collector/firmware/ra_gen/common_data.h `
	-Pattern 'g_basic0|VIN_CFG_IMAGE_STRIDE \(1024\)|VIN_BYTES_PER_FRAME.*600'
```

### 4. ファームをビルドする

PowerShellでリポジトリ直下から実行する。Arm GCCのbinディレクトリは実際のインストール先に合わせる。

```powershell
$env:ARM_GCC_TOOLCHAIN_PATH = 'C:/Program Files (x86)/Arm GNU Toolchain arm-none-eabi/13.2 Rel1/bin'
cmake -S data_collector/firmware -B data_collector/firmware/build/Debug `
	-G Ninja -DCMAKE_BUILD_TYPE=Debug `
	-DCMAKE_TOOLCHAIN_FILE=data_collector/firmware/cmake/gcc.cmake `
	-DCOLLECTOR_FSP_IO_ENABLED=ON `
	-DARM_TOOLCHAIN_PATH="$env:ARM_GCC_TOOLCHAIN_PATH"
cmake --build data_collector/firmware/build/Debug --parallel 8
```

成功すると次の2ファイルが生成される。

- `data_collector/firmware/build/Debug/data_collector_fw.elf`
- `data_collector/firmware/build/Debug/data_collector_fw.srec`

`missing 'g_basic0'` が出た場合は、FSP生成が未完了なので書き込みへ進まない。

### 5. ファームを書き込む

#### 方法A: J-Link Commander

デバッグUSBを接続し、J-Link Commanderがインストールされていることを確認する。PowerShellで次を実行する。

```powershell
@"
device R7KA8P1KF
si SWD
speed 4000
connect
loadfile data_collector/firmware/build/Debug/data_collector_fw.srec
r
g
q
"@ | Set-Content -Encoding ascii data_collector/firmware/build/flash.jlink
JLink.exe -CommandFile data_collector/firmware/build/flash.jlink
```

`loadfile` 完了後にverifyエラーが出ないことを確認する。`JLink.exe` が見つからない場合はSEGGER J-Link Softwareのインストール先をPATHへ追加する。

#### 方法B: e2 studio

e2 studioのDebug Configurationで、デバイスを `R7KA8P1KF`、接続をJ-Link/SWDに設定し、生成した `data_collector_fw.elf` をDebug対象に指定して書き込む。初回はDebug実行後に停止した場合、Resumeでアプリを開始する。

### 6. PC受信環境を準備する

リポジトリ直下で、受信に使うPython環境へ依存パッケージをインストールする。

```powershell
& .venv/Scripts/python.exe -m pip install -r data_collector/pc/requirements.txt
```

ボードのUSB HS Device端子をPCへ接続し、Windowsのデバイスマネージャーでデータ収集用CDCが認識されていることを確認する。データ収集用CDCのUSB識別子は `VID:PID=1209:DCA1` であり、COM番号はWindowsの再列挙で変わることがある。受信側はCOM5を優先するが、COM5でなくてもこのVID/PIDのCDCが1台だけ列挙されていれば自動選択する。`VID:PID=1366:1024` のJ-Link VCOMはデータ収集用ではないため、選択しない。

```powershell
New-Item -ItemType Directory -Force dataset | Out-Null
& .venv/Scripts/python.exe data_collector/pc/collector_receiver.py --output dataset
```

受信プログラムは終了せず待機する。COM5が対象CDCならCOM5を開き、別のCOM番号ならログに選択した番号を表示する。対象VID/PIDが列挙されていない場合は、J-Link VCOMなど別のCOMを開かずエラーとして終了する。受信側はファームウェアのCDC準備条件に合わせてDTRを有効化し、RTSを無効化している。

画素形式の検証を行う間だけ、次のオプションを追加してRAWペイロードを保存できる。`raw/*.bin` はフレームメタデータ16バイトと画像データを含む無加工の転送内容である。このオプションは検証終了後に受信ツールから削除する。

```powershell
& .venv/Scripts/python.exe data_collector/pc/collector_receiver.py --output dataset --save-raw
```

### 7. 収集を開始して画像を保存する

1. 受信プログラムを先に起動する。
2. ボードのSW1を押す。WAITINGからRECORDINGへ遷移し、約5 fpsで収集する。
3. 給餌の瞬間などを記録したい場合はSW2を押す。FEED_MARKERがJSONLへ記録される。
4. 収集を止めるときはSW1をもう一度押す。SESSION_ENDと統計情報が保存される。
5. 受信を止めるときはPowerShellで`Ctrl+C`を押す。

保存結果は次の構成になる。

```text
dataset/
└─ <YYYYMMDD_HHMMSS>/
	 ├─ frames/
	 │  ├─ frame_<sequence>_<timestamp>.jpg
	 │  └─ ...
	 └─ records.jsonl
```

`records.jsonl`のRAWレコードで`width=640`、`height=480`、`format=RGB565`を確認する。SESSION_ENDの`statistics.transmitted_frames`が保存対象フレーム数で、`dropped_frames`が0であることが正常の目安になる。

### 8. 収集できない場合の確認順

1. `ra_gen/common_data.h` に `g_basic0` があるか確認する。
2. ビルド時に `COLLECTOR_FSP_IO_ENABLED=ON` になっているか確認する。
3. J-Link書き込み時に対象デバイスが `R7KA8P1KF` になっているか確認する。
4. カメラ接続とOV5640の電源・リセット状態を確認する。
5. PC側のCOM番号がデバッグ用COMではなく、USB CDC用COMであることを確認する。
6. 受信プログラムをSW1より先に起動する。
7. `records.jsonl`に`usb`関連のERRORレコードがないか確認する。

- `collector_state.[ch]`: WAITING / RECORDING / STOPPING / ERROR 状態機械
- `collector_protocol.[ch]`: 32-byte固定ヘッダー、CRC32、フレームメタデータ
- `collector_app.[ch]`: VIN ISR、SW1/SW2 ISR、RAWフレーム、USB送信を接続するFreeRTOSタスク
- `collector_platform_fsp.c`: FSP 6.4 `r_usb_basic` 実機ポート
- `collector_platform.c`: FSPスタック未生成時に収集開始を拒否する安全な既定ポート
- `../../pc/collector_receiver.py`: Windows CDC受信、CRC検証、JPEG/JSONL保存

SW1は収集開始/停止、SW2は給餌マーカーです。PCが仮想COMポートを開いてDTRが有効な場合のみSW1で開始できます。

## FSP 6.4 設定

現在の `configuration.xml` にはUSB PCDCがありません。e2 studio 2026-01のFSP Configurationで次を追加し、FSP 6.4.0として生成してください。FSP 6.2のstandalone RASCは使用しません。

1. USB Basic (`r_usb_basic`) とPeripheral CDC (`r_usb_pcdc`) をUSB High Speed Deviceとして追加し、生成名を `g_basic0` にする。
2. USB callbackを `collector_usb_callback` にする。
3. 生成後、CMake構成時に `-DCOLLECTOR_FSP_IO_ENABLED=ON` を指定する。

VINの正本設定は640x480、stride 640、YUV422、SDRAM 3面バッファへ更新済みです。FSP生成前の `ra_gen/common_data.*` は旧768x450設定のため、必ずFSP 6.4で再生成してから実機I/Oを有効にしてください。

RAWフレームは8-byte整列のメタデータと640x480 RGB565 little-endianデータで構成し、USBは16 KiB単位で送信します。PC側でJPEG quality 85に変換します。

## ビルド

Arm GNU Toolchain 13.2.Rel1を使用します。

```powershell
cmake -S data_collector/firmware -B data_collector/firmware/build/Debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCOLLECTOR_FSP_IO_ENABLED=ON
cmake --build data_collector/firmware/build/Debug --parallel 8
```

出力は `build/Debug/data_collector_fw.elf` と `build/Debug/data_collector_fw.srec` です。ビルド中にRASCは自動実行しません。

## PC受信

```powershell
python -m pip install -r data_collector/pc/requirements.txt
python data_collector/pc/collector_receiver.py COM6 --output dataset
```

保存先は `dataset/<YYYYMMDD_HHMMSS>/frames/*.jpg` と `records.jsonl` です。フォルダ名はPCがSESSION_STARTを受信したローカル時刻で、同じ秒に複数セッションが始まった場合は末尾に連番が付きます。FWから受信したRGB565 little-endianをPC側でJPEG quality 85へ変換し、幅、高さ、形式、シーケンス番号、セッション開始からの時刻を記録します。

## テスト

```powershell
gcc -std=c11 -Wall -Wextra -Werror -I data_collector/firmware/src data_collector/firmware/src/collector_protocol.c data_collector/firmware/src/collector_state.c data_collector/firmware/test/test_collector_core.c -o data_collector/firmware/test/test_collector_core.exe
data_collector/firmware/test/test_collector_core.exe
Push-Location data_collector/pc
python -m unittest -v
Pop-Location
```
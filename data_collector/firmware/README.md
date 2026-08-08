# Data Collector Firmware

EK-RA8P1 の OV5640 から 640x480 YUV422 を 5 fps で採取し、JPEG 圧縮後に USB HS CDC-ACM で PC へ送る FreeRTOS ファームウェアです。

## 実装構成

- `collector_state.[ch]`: WAITING / RECORDING / STOPPING / ERROR 状態機械
- `collector_protocol.[ch]`: 32-byte固定ヘッダー、CRC32、フレームメタデータ
- `collector_app.[ch]`: VIN ISR、SW1/SW2 ISR、JPEG、USB送信を接続するFreeRTOSタスク
- `collector_platform_fsp.c`: FSP 6.4 `r_jpeg` / `r_usb_basic` 実機ポート
- `collector_platform.c`: FSPスタック未生成時に収集開始を拒否する安全な既定ポート
- `../../pc/collector_receiver.py`: Windows CDC受信、CRC検証、JPEG/JSONL保存

SW1は収集開始/停止、SW2は給餌マーカーです。PCが仮想COMポートを開いてDTRが有効な場合のみSW1で開始できます。

## FSP 6.4 設定

現在の `configuration.xml` にはJPEG CodecとUSB PCDCがありません。e2 studio 2026-01のFSP Configurationで次を追加し、FSP 6.4.0として生成してください。FSP 6.2のstandalone RASCは使用しません。

1. JPEG Codec (`r_jpeg`) を追加し、生成名を `g_jpeg0` にする。
2. Encodeを有効、Decodeを無効、640x480、stride 640、YCbCr 4:2:2、normal byte order、Quality 85にする。
3. Encode callbackを `collector_jpeg_callback` にする。
4. USB Basic (`r_usb_basic`) とPeripheral CDC (`r_usb_pcdc`) をUSB High Speed Deviceとして追加し、生成名を `g_basic0` にする。
5. USB callbackを `collector_usb_callback` にする。
6. 生成後、CMake構成時に `-DCOLLECTOR_FSP_IO_ENABLED=ON` を指定する。

VINの正本設定は640x480、stride 640、YUV422、SDRAM 3面バッファへ更新済みです。FSP生成前の `ra_gen/common_data.*` は旧768x450設定のため、必ずFSP 6.4で再生成してから実機I/Oを有効にしてください。

`collector_platform_fsp.c` はJPEG完了とUSB write完了をFreeRTOS通知で待機します。JPEG出力は8-byte整列、USBは16 KiB単位で送信します。

## ビルド

Arm GNU Toolchain 13.3.1を使用します。

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

保存先は `dataset/session_<id>/frames/*.jpg` と `records.jsonl` です。各JPEGレコードには幅、高さ、品質ID、圧縮時間、シーケンス番号、セッション開始からの時刻が入ります。

## テスト

```powershell
gcc -std=c11 -Wall -Wextra -Werror -I data_collector/firmware/src data_collector/firmware/src/collector_protocol.c data_collector/firmware/src/collector_state.c data_collector/firmware/test/test_collector_core.c -o data_collector/firmware/test/test_collector_core.exe
data_collector/firmware/test/test_collector_core.exe
Push-Location data_collector/pc
python -m unittest -v
Pop-Location
```
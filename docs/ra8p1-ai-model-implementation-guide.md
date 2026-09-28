# EK-RA8P1 AIモデル組み込み実装ガイド

この文書は、EK-RA8P1のEthos-U55へ画像モデルを組み込み、給餌行動検知へ接続するための実装手順をまとめたものです。

## 実行場所の境界

- **EK-RA8P1実機**: カメラフレーム取得、金魚ボックスのNPU推論、餌候補のフレーム差分・追跡、給餌状態機械、給餌検出、LCDへのデバッグ表示を担当します。
- メインアプリは **μT-Kernel 3.0** 上で動作し、給餌状態・デバッグ情報・未給餌アラートをLCDに表示します。
- **PC**: 学習データの受信・保存、アノテーション、学習、TFLite/Vela変換、オフラインの数値比較だけに使用します。実運用の画像検出と給餌判定はPCでは実行しません。

## 0. 調査結果と結論

### 指定された2プロジェクトについて

次のプロジェクトは、現在のワークスペースには存在しません。

- `tflm_cifar10_ek_ra8d1_ep`
- `tflm_person_detection_ek_ra8d1_ep`

Renesas `ra-fsp-examples` の現在の `master` ブランチの `example_projects/ek_ra8d1` 一覧にも両方の名前はなく、GitHubのコード検索でも完全一致の結果はありませんでした。したがって、これらのプロジェクトに含まれていたはずの学習スクリプトや変換スクリプトを、存在するものとして説明することはできません。

代わりに、現在確認できるRA8P1向けの実装リファレンスは次です。

`test/example_projects/ek_ra8p1/ethos_u55_face_detection/`

このサンプルは、次の処理を実装しています。

- Vela最適化済みTFLiteモデルの組み込み
- TFLiteモデルをC配列としてリンク
- TFLMの `MicroInterpreter` 初期化
- 必要な演算子だけをresolverへ登録
- `ARM_NPU` 定義によるEthos-U演算子の登録
- `RM_ETHOSU_Open()` によるEthos-U55初期化
- `AllocateTensors()` と `Invoke()` による推論
- テンソルの型、量子化パラメータ、アリーナ使用量の表示

### 本プロジェクトでの推奨方針

最初の給餌構成は、餌を直接学習する画像分類モデルではなく、**全画面からの初期候補抽出、前フレーム差分による餌候補追跡**と**単一金魚のボックス回帰モデル**を組み合わせます。

```text
入力: 256x256、グレースケール、INT8
出力: 1匹分のボックス回帰
    x, y, w, h, confidence
```

餌の物体検出ヘッドは持たせません。色成分や単一フレームの差分は初期候補の発見にだけ使い、餌の確定には前フレーム差分、連結成分、複数フレームの下降軌跡を必須とします。確定した餌だけを、金魚ボックスとの距離・接近・消失を判定するCPU側の状態機械へ渡します。

餌候補を餌とみなす固定条件は次のとおりです。

```text
全画面の暖色成分
    -> 初期候補
8-bit輝度の abs(frame[t] - frame[t-1])
    -> 上位2%の差分マスク
    -> 3x3オープニング
    -> 連結成分・面積フィルタ・近接成分統合
    -> 前フレーム候補との対応付け
    -> 2回以上連続した下降
    -> FOOD_CONFIRMED
```

固定ROIは使用しません。`frame[t] - frame[t-2]` の2フレーム差分は見失いからの救済に使えますが、それ単独で餌確定してはいけません。餌確定前の候補消失も `feeding_completed` の根拠には使わず、`unknown` または未確定として扱います。

## 1. このリポジトリの役割分担

| パス | 役割 | AI実装との関係 |
|---|---|---|
| `data_collector/` | OV5640から学習用フレームを収集 | 学習用データの入口 |
| `dataset/` | PCへ搬出したフレームとJSONL | 学習・オフライン評価の入力。実機検出には直接使わない |
| `test/example_projects/ek_ra8p1/ethos_u55_face_detection/` | Ethos-U55/TFLMの参考実装 | モデル組み込みの基準 |
| `test/` | FSP 6.4.0、CMake/Ninja、GCCの実験プロジェクト | 本番環境への移植元 |
| `app/` | 相互守護アクアリウム本体 | 最終的な推論実装先。現状は金魚モデルデータのみ |
| `docs/feeding-behavior-model-plan.md` | 給餌モデルの計画 | 方針、評価、マイルストーン |

### バージョン差

顔検出サンプルと本番候補環境は同一ではありません。

| 項目 | 顔検出サンプル | 本リポジトリの本番候補 |
|---|---|---|
| FSP | 6.6.0 | 6.4.0 |
| e2 studio | 2026-07 | 2026-01を基準 |
| コンパイラ | LLVM Embedded Toolchain 22.1.0 | `arm-none-eabi-gcc` |
| RTOS | なし | μT-Kernel 3.0 |
| 入力 | C配列の静止画像 | OV5640のDMAフレーム |
| ビルド | e2 studioプロジェクト | CMake + Ninjaを基本とする |

FSP 6.6.0/LLVMのプロジェクトを、FSP 6.4.0/GCCの本番プロジェクトへ生成コードごとコピーしてはいけません。`configuration.xml`を本番側のFSPで再生成し、AI関連モジュール、メモリ設定、マクロを個別に合わせます。

## 2. 顔検出サンプルから読み取れる実装パターン

### 2.1 重要ファイル

顔検出サンプルの実装場所は次です。

```text
test/example_projects/ek_ra8p1/ethos_u55_face_detection/
└─ ethos_u55_face_detection_ek_ra8p1_ep/e2studio/
   ├─ configuration.xml
   └─ src/
      ├─ ethos_u55.c
      └─ face_detection_app/
         ├─ common/Model.cc
         ├─ generated/object_detection/src/yolo-fastest_192_face_v4_vela.cc
         ├─ source/use_case/object_detection/src/MainLoop.cc
         └─ source/use_case/object_detection/src/YoloFastestModel.cc
```

### 2.2 初期化から推論まで

実行経路は次のとおりです。

```text
face_detection_entry()
  ├─ TERM_INIT()
  ├─ RM_ETHOSU_Open(&g_rm_ethosu0_ctrl, &g_rm_ethosu0_cfg)
  └─ MainLoop()
       ├─ tensor arenaを用意
       ├─ GetModelPointer() / GetModelLen()
       ├─ Model::Init()
       │    ├─ tflite::GetModel()
       │    ├─ schema version確認
       │    ├─ 必要な演算子をresolverへ登録
       │    ├─ MicroAllocator::Create()
       │    ├─ MicroInterpreter生成
       │    └─ AllocateTensors()
       ├─ カメラまたは画像を前処理
       ├─ Invoke()
       └─ 出力を後処理
```

`common/Model.cc`の `Model::Init()` は、金魚ボックス回帰モデルでもそのまま参考になります。特に次を起動時に表示する設計が重要です。

- 入力テンソルの型
- 入力テンソルの形状
- 入力テンソルのバイト数
- 入力のscaleとzero point
- 出力テンソルの型と量子化パラメータ
- `arena_used_bytes()`
- モデル内の演算子一覧

### 2.3 resolverはモデルに合わせて限定する

顔検出サンプルの `YoloFastestModel::EnlistOperations()` は、次の演算子を登録しています。

```cpp
AddDepthwiseConv2D();
AddConv2D();
AddAdd();
AddResizeNearestNeighbor();
AddPad();
AddMaxPool2D();
AddConcatenation();
AddEthosU();
```

金魚ボックス回帰モデルでは、実際のTFLiteモデルを調べた後に、必要なものだけを登録します。たとえば次のような構成です。

```cpp
AddConv2D();
AddDepthwiseConv2D();
AddAveragePool2D();
AddReshape();
AddFullyConnected();
```

`AllOpsResolver`を使うと動作確認は簡単ですが、コードサイズが増えます。本番ではモデルの演算子一覧とresolverの登録一覧を一致させます。

Ethos-U演算子は、NPUを使うビルドでだけ登録します。

```cpp
#if defined(ARM_NPU)
    if (kTfLiteOk != resolver.AddEthosU()) {
        return false;
    }
#endif
```

### 2.4 モデルは学習済みバイナリではなくC配列として組み込む

顔検出サンプルは、Vela出力のTFLiteを次のようなC++ファイルに変換しています。

```text
generated/object_detection/src/yolo-fastest_192_face_v4_vela.cc
```

このファイルはモデルデータと、モデル先頭アドレスおよびサイズを返す関数を提供します。給餌モデルでは次の構成を推奨します。

```text
app/src/ai/model/fish_bbox_model_data.cc
app/src/ai/model/fish_bbox_model_data.hpp
```

モデルは書き込み禁止の領域へ置き、実行時に変更しないでください。モデルが大きくなった場合は、コードフラッシュへの直置き、OSPI XIP、OSPIからSDRAMへのコピーを比較します。

## 3. データセットの現状と扱い

### 3.1 現在のデータ形式

現在の `dataset/20260927_112457/records.jsonl` は、次の形式です。

- `record_type: 1`: セッション設定
- `record_type: 2`: フレーム
- `record_type: 5`: セッション終了
- フレーム画像: `frames/*.jpg`
- 元データ: `raw/*.bin`
- 解像度: 640x480
- 形式: RGB565
- 収集設定: 5 fps

このセッションは、設定1件、フレーム219件、終了1件で、給餌マーカーは含まれていません。したがって、現時点のデータだけでは金魚ボックスと餌軌跡の教師データを作れません。SW2の給餌マーカーを含むセッションを複数回収集し、目視確認でラベルを確定してください。

### 3.2 収集側RAWと顔検出サンプルの入力を分ける

このリポジトリのデータ収集ファームと、顔検出サンプルは画像の受け渡し形式が異なります。顔検出サンプルの手順に合わせる場合、収集側のRAWファイルをそのままサンプルへ渡してはいけません。

#### データ収集側のRAW

`data_collector/pc/collector_receiver.py`が保存するRAWは、フレームメタデータ16バイトとRGB565画像を連結した転送ペイロードです。メタデータは次の構造です。

```python
FRAME_METADATA = struct.Struct("<HHB3xII")
```

画像部分のサイズは次のとおりです。

```text
640 * 480 * 2 = 614400 bytes
16 bytes metadata + 614400 bytes image = 614416 bytes
```

RAWを使う場合だけ、先頭16バイトを `FRAME_METADATA` として読み、残りをRGB565 little-endianとして復号します。通常の学習では、PC側でRGB565からJPEGへ変換済みの `frames/*.jpg` を使う方が簡単です。

#### 顔検出サンプル側の入力

顔検出サンプルはRAWファイルやJPEGファイルを実行時に読みません。入力画像をC配列として静的にリンクします。

サンプルの `sample_files.h` では、入力は次のRGB24データです。

```text
width  = 192
height = 192
bytes per pixel = 3
IMAGE_DATA_SIZE = 192 * 192 * 3 = 110592 bytes
```

READMEの手順も、次の順序です。

```text
192x192の24-bit BMP
    -> ImageMagickでRGB24のrawへ変換
    -> xxd -iでC配列へ変換
    -> sample_files.cからim0として参照
```

つまり、サンプルでいう `raw` は「192x192 RGB24の画素列」であり、データ収集側の「16バイトメタデータ付き640x480 RGB565ペイロード」ではありません。

`UseCaseHandler.cc`では、C配列の1画素3バイトをRGBとしてグレースケール化し、モデル入力へコピーします。入力テンソルが符号付きINT8の場合は、`ImageUtils.cc`の `ConvertImgToInt8()` により各画素から128を引きます。

```text
sample_files.c:       const uint8_t im0[192 * 192 * 3]
UseCaseHandler.cc:     RGB24 -> grayscale uint8
ImageUtils.cc:         uint8 -> int8 (value - 128)
Model::Invoke():       int8 tensor -> inference
```

#### 給餌モデルへ適用する場合

給餌モデルでは、サンプルの静的C配列方式を実機へ組み込んだ既知画像によるスモークテストにだけ使います。PC上では前処理と量子化の数値比較だけを行い、通常の検出は次の実機経路で実行します。

```text
dataset/frames/*.jpg
    -> PCで前処理・学習・量子化を実行
    -> モデルをC配列化して実機へ組み込む
    -> 実機のOV5640からRGB565またはYUV422を取得
    -> 実機で同じ入力表現へ変換し、tensorへコピーしてInvoke()
```

サンプル準拠の確認用画像を作る場合は、収集JPEGを192x192のRGB24へ変換してからC配列化します。収集RAWを使う場合でも、16バイトを除いたRGB565をいったんRGB24へ変換し、サンプルが期待する3 bytes/pixelの配列にします。サンプル入力へ16バイトのメタデータを含めたり、RGB565の2 bytes/pixelを直接渡したりしません。

### 3.3 ラベルファイル

学習用に、JSONLを変更せず、別ファイルを追加します。

```text
dataset/
└─ 20260927_112457/
   ├─ records.jsonl
   ├─ annotations.csv
   └─ frames/
```

`annotations.csv`の最小形式は次です。

```csv
session_id,sequence,timestamp_ms,fish_x,fish_y,fish_w,fish_h,fish_visible,event_state,reviewer
898059,1,9,0.42,0.68,0.24,0.18,1,idle,manual
898059,2,658,0.44,0.66,0.25,0.18,1,food_sinking,manual
898059,3,1306,0.46,0.63,0.25,0.19,1,fish_approaching,manual
```

使用するイベント状態は次に固定します。

- `idle`: 餌候補がない通常状態
- `food_sinking`: 餌候補が出現し、下方向へ移動中
- `fish_approaching`: 餌候補と金魚の距離が縮小中
- `eating_candidate`: 金魚近傍で餌候補が消失
- `feeding_completed`: 消失後、一定時間にわたり餌候補が再出現しない
- `unknown`: 底面残留、遮蔽、反射、振動などで判定不能

### 3.4 セッション単位で分割する

同一セッションの隣接フレームを、ランダムにtrain/validation/testへ分けてはいけません。隣接フレームはほぼ同じ画像なので、フレーム単位で分割するとテスト精度が不当に高くなります。

推奨は次です。

```text
セッションA, B, C: train
セッションD:       validation
セッションE:       test
```

最低でも、異なる日時、照明、給餌量、水面状態を含むセッションを用意します。

給餌マーカーを使う場合の初期候補区間は、各マーカーの前後を次のようにします。

```text
イベント候補: マーカー時刻 - 2秒 から + 8秒
```

これは正解ラベルではありません。5 fpsなら1秒あたり約5フレームなので、候補区間を作った後に目視で金魚ボックス、餌軌跡、イベント状態を修正します。

## 4. 学習モデルの設計

### 4.1 Phase 1のモデル

最初は、単一の金魚を直接回帰し、演算子が少ないモデルにします。餌はこのモデルへ入力せず、フレーム差分と連結成分追跡で扱います。

```text
Input       [1, 256, 256, 1] float32  (学習時)
Conv2D      16ch, 3x3, stride 2, ReLU
Depthwise   16ch, 3x3, stride 2, ReLU
Conv2D      32ch, 3x3, stride 2, ReLU
Depthwise   32ch, 3x3, stride 2, ReLU
Conv2D      64ch, 3x3, stride 1, ReLU
Flatten
 Dense       5 values: x, y, w, h, confidence
Output      [1, 5] float32  (学習時)
```

`x`、`y`、`w`、`h`は画像サイズで正規化した値とします。`x`と`y`はbbox左上座標です。`confidence`を含む5値を出力し、最終出力はlinearとしてCPU側で範囲処理します。`GlobalAveragePooling2D`やSoftmaxは初期構成に追加しません。

学習時の損失は、confidenceの有無とボックス座標誤差を組み合わせます。金魚が見えにくいフレームは `fish_visible=0` として学習・評価から分離します。

### 4.2 時系列処理

時系列はモデルへ持ち込まず、CPU側の状態機械で処理します。

1. 全画面から暖色成分を抽出し、初期の `FOOD_CANDIDATE` を作る
2. 8-bit輝度の前フレーム差分から連結成分を抽出する
3. 3x3オープニング、面積フィルタ、近接成分統合を行う
4. 前フレーム候補と対応付けし、2回以上連続した下方向の移動を確認して `FOOD_CONFIRMED` にする
5. 確定済み餌と金魚ボックスの距離を計算する
6. 金魚近傍で確定済み餌が消失し、一定時間再出現しなければ `feeding_completed` とする
7. 底面残留、遮蔽、全体振動、対応付け失敗、餌確定前の消失を `unknown` とする

RNN、3D-CNN、複数フレーム入力、餌の検出ヘッドは初期構成に追加しません。

## 5. ローカル学習環境

ワークスペース直下の仮想環境を使用します。

```powershell
$Python = ".venv/Scripts/python.exe"
& $Python --version
& $Python -m pip install --upgrade pip
& $Python -m pip install tensorflow numpy pillow scikit-learn matplotlib ethos-u-vela
```

TensorFlowが現在のPythonバージョンに対応しない場合は、エラーに表示された対応バージョンで学習専用の仮想環境を作ります。組み込みビルドに使うFSP環境と、モデル学習用Python環境を混ぜないでください。

確認コマンドです。

```powershell
& $Python -c "import tensorflow as tf; print(tf.__version__)"
& $Python -c "from PIL import Image; import numpy; import sklearn; print('python ml dependencies: OK')"
& .venv/Scripts/vela.exe --help
```

仮想環境のScriptsにコマンドが生成されていない場合は、PATHへ追加して実行します。

```powershell
& $Python -m pip show ethos-u-vela
vela --help
```

## 6. TFLite変換とFull INT8量子化

### 6.1 量子化の前提

Ethos-U55へ載せるモデルは、まずFull INT8 TFLiteにします。Velaに浮動小数点モデルを渡して解決することを前提にしません。

学習時の前処理とファームウェアの前処理を一致させます。

```text
JPEG/RGB565/YUV422
  -> RGB
  -> グレースケール
    -> 同じ補間方法で256x256
  -> float32の0.0から1.0
  -> representative datasetで量子化
```

学習・代表データ・実機の3箇所で、次を一致させます。

- クロップ範囲
- リサイズ方法
- RGB565のバイト順
- グレースケール変換式
- 画素値の範囲
- チャネル順
- 入力テンソルの形状

### 6.2 変換コードの骨子

学習済みKerasモデルを `fish_bbox_float.keras` とした場合の変換骨子です。代表データは金魚の位置、見え方、照明、反射のバリエーションを含めます。

```python
import pathlib
import numpy as np
import tensorflow as tf

MODEL_PATH = "fish_bbox_float.keras"
OUTPUT_PATH = "fish_bbox_int8.tflite"

# ここは学習時と同じ処理で、[1, 256, 256, 1] float32を返す。
def representative_dataset():
    for image in load_calibration_images():
        yield [image.astype(np.float32)]

model = tf.keras.models.load_model(MODEL_PATH)
converter = tf.lite.TFLiteConverter.from_keras_model(model)
converter.optimizations = [tf.lite.Optimize.DEFAULT]
converter.representative_dataset = representative_dataset
converter.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
converter.inference_input_type = tf.int8
converter.inference_output_type = tf.int8

model_bytes = converter.convert()
pathlib.Path(OUTPUT_PATH).write_bytes(model_bytes)
```

このコードの `load_calibration_images()` は、実際のデータセットローダーへ置き換えます。代表データを1クラスだけにすると、もう一方のクラスや環境変化で量子化後の精度が落ちます。

### 6.3 変換直後に確認する

```python
import numpy as np
import tensorflow as tf

interpreter = tf.lite.Interpreter(model_path="fish_bbox_int8.tflite")
interpreter.allocate_tensors()

input_detail = interpreter.get_input_details()[0]
output_detail = interpreter.get_output_details()[0]
print(input_detail)
print(output_detail)

assert input_detail["dtype"] == np.int8
assert output_detail["dtype"] == np.int8
```

確認項目は次です。

- 入力形状が `[1, 256, 256, 1]` である
- 入力dtypeが `int8`
- 出力dtypeが `int8`
- scaleが0ではない
- zero pointがモデルの詳細に存在する
- `TFLITE_BUILTINS_INT8`だけで変換できている
- float32演算が残っていない

量子化モデルのPC上のオフライン評価結果を、元のfloatモデルと比較します。これは実機検出の代替ではありません。ボックスIoU、中心位置誤差、confidenceの閾値を量子化後のtestセットで決めます。

## 7. VelaによるEthos-U55最適化

顔検出サンプルの `default_vela.ini` を基準にします。まずINT8 TFLiteを作り、その後にVelaを実行します。

```powershell
New-Item -ItemType Directory -Force build/ml/vela | Out-Null

& .venv/Scripts/vela.exe .\build\ml\fish_bbox_int8.tflite `
    --accelerator-config=ethos-u55-256 `
    --optimise Performance `
    --config .\test\example_projects\ek_ra8p1\ethos_u55_face_detection\ethos_u55_face_detection_ek_ra8p1_ep\e2studio\src\face_detection_app\resources_downloaded\object_detection\default_vela.ini `
    --memory-mode=Shared_Sram `
    --system-config=Ethos_U55_High_End_Embedded `
    --output-dir=.\build\ml\vela
```

顔検出サンプルのREADMEにあるコマンドと同じ考え方です。出力ディレクトリに生成されたVela済み `.tflite` を使います。

```powershell
Get-ChildItem build/ml/vela -Filter *.tflite
```

Vela後のモデルについて、次を記録します。

- Velaのバージョン
- 入力TFLiteのSHA-256
- 出力TFLiteのSHA-256
- 使用した `default_vela.ini`
- Ethos-Uへオフロードされた演算子
- CPUに残った演算子
- モデルサイズ

Velaがモデル全体をNPUへ載せられなくても、未対応演算子はCPUへ残ります。その場合はまず動作を成立させ、CPU部分の有無とレイテンシを測定します。

## 8. TFLiteをC配列へ変換する

### 8.1 Linux/WSLで `xxd` を使う場合

```sh
xxd -i fish_bbox_int8_vela.tflite > fish_bbox_model_data.cc
```

ただし、`xxd -i`の生成シンボル名は入力ファイル名から自動生成されるため、本番コードで使う名前を確認してから整理します。

### 8.2 Windows PowerShellだけで変換する場合

`xxd`がない場合は、既存の `.venv` のPythonを使えます。

```powershell
$Python = ".venv/Scripts/python.exe"
@'
from pathlib import Path

source = Path("build/ml/vela/fish_bbox_int8_vela.tflite")
target = Path("app/src/ai/model/fish_bbox_model_data.cc")
identifier = "g_fish_bbox_model_data"
data = source.read_bytes()
lines = [
    "#include <cstddef>",
    "#include <cstdint>",
    "",
    "alignas(16) const unsigned char " + identifier + "[] = {",
]
for offset in range(0, len(data), 12):
    row = data[offset:offset + 12]
    values = ", ".join(f"0x{value:02x}" for value in row)
    lines.append("    " + values + ",")
lines.extend([
    "};",
    "const std::size_t g_fish_bbox_model_data_len = sizeof(" + identifier + ");",
    "",
])
target.parent.mkdir(parents=True, exist_ok=True)
target.write_text("\\n".join(lines), encoding="ascii")
'@ | & $Python -
```

生成ファイルは、モデルを更新するたびに再生成します。手編集したバイト列を残さず、入力モデルのハッシュと生成日時を記録します。

ヘッダーは次のようにします。

```cpp
#pragma once

#include <cstddef>
#include <cstdint>

extern const unsigned char g_fish_bbox_model_data[];
extern const std::size_t g_fish_bbox_model_data_len;
```

## 9. TFLM/Ethos-Uへの組み込み

### 9.1 本番側に追加するファイル

```text
app/src/ai/
├─ fish_bbox_model.hpp
├─ fish_bbox_model.cpp
├─ fish_bbox_preprocess.hpp
├─ fish_bbox_preprocess.cpp
├─ fish_bbox_inference.hpp
├─ fish_bbox_inference.cpp
└─ model/
    ├─ fish_bbox_model_data.hpp
    └─ fish_bbox_model_data.cc
```

役割を混ぜないでください。

- `fish_bbox_preprocess`: 画素形式変換、リサイズ、量子化
- `fish_bbox_model`: TFLMモデル、resolver、interpreterの初期化
- `fish_bbox_inference`: 1回の推論、5値出力の解釈、レイテンシ
- `fish_bbox_model_data`: Vela済みTFLiteのバイト列だけ

### 9.2 FSP設定

Smart Configuratorで、本番の `configuration.xml`へ次のモジュールを追加します。

- TFLM Core Library
- CMSIS-NN
- CMSIS-DSP
- Flatbuffers
- `rm_ethosu`
- Ethos-U Core Driver
- 必要に応じてGPT（推論時間計測用）

顔検出サンプルの設定値も確認します。

- CPU: CPU0 / Cortex-M85
- ターゲット: `R7KA8P1KFLCAC`
- Heap: サンプルは `0x4000`
- Main stack: サンプルは `0x4000`
- GPT period: 5000 ms

本番では、これらの値をそのまま固定せず、ビルドと実測で決めます。

コンパイラのマクロは、CとC++の両方へ `ARM_NPU` を定義します。顔検出サンプルREADMEにも、C++ CompilerとCompilerの両方へ設定する手順があります。

`ARM_NPU`がない場合は、TFLMがCPUで動作する比較用ビルドになります。NPUビルドでは `AddEthosU()` が成功し、`RM_ETHOSU_Open()`も成功することを起動ログで確認します。

### 9.3 NPU初期化

初期化と終了は1回ずつ行います。

```c
#ifdef ARM_NPU
fsp_err_t err = RM_ETHOSU_Open(&g_rm_ethosu0_ctrl, &g_rm_ethosu0_cfg);
if (FSP_SUCCESS != err) {
    /* エラーを記録して推論を開始しない */
}
#endif

/* model.Init() と推論 */

#ifdef ARM_NPU
RM_ETHOSU_Close(&g_rm_ethosu0_ctrl);
#endif
```

顔検出サンプルの `ethos_u55.c` にある `face_detection_entry()` が直接の参考です。エラー処理で `Close`を複数回呼ばないよう、本番コードでは所有権を明確にします。

### 9.4 Tensor arena

顔検出サンプルは `MainLoop.cc`で `0x0080000`、つまり512 KiBのtensor arenaを確保しています。金魚ボックスモデルはもっと小さくできる見込みですが、最初は256 KiB以上で起動し、`arena_used_bytes()`を表示してから縮小します。

```cpp
alignas(16) static uint8_t tensor_arena[256 * 1024];
```

NPUがアクセスするバッファは、キャッシュ属性、アラインメント、SDRAMの可視性を確認します。カメラDMAフレームは必ず `.sdram_noinit_nocache`へ配置します。tensor arenaも、NPUドライバーのキャッシュメンテナンス方針を確認できるまでは、非キャッシュ領域を優先します。

### 9.5 モデルのロード

金魚ボックスモデルの初期化で確認する項目は次です。

```text
GetModelPointer()      != NULL
GetModelLen()          > 0
schema version         == TFLITE_SCHEMA_VERSION
AllocateTensors()      == kTfLiteOk
input tensor count     == 1
output tensor count    == 1
input type             == kTfLiteInt8
input shape            == [1, 256, 256, 1]
output shape           == [1, 5]
```

`AllocateTensors()`が失敗した場合は、推論を続けずに次を確認します。

1. tensor arenaを増やす
2. resolverへ不足演算子を追加する
3. TFLite schemaと組み込みTFLMのバージョンを合わせる
4. モデル配列のリンクとアラインメントを確認する
5. Vela前後のモデルを取り違えていないか確認する

## 10. カメラフレームからINT8入力を作る

### 10.1 実機での処理経路

```text
OV5640 / MIPI CSI-2
  -> VIN DMA buffer (.sdram_noinit_nocache)
  -> DMA完了通知
  -> 推論タスクへバッファを渡す
  -> RGB565またはYUV422をRGBへ変換
  -> グレースケール化
    -> 256x256へリサイズ
  -> floatの0.0から1.0相当をINT8へ量子化
  -> input tensorへコピー
  -> interpreter->Invoke()
    -> output tensorからx, y, w, h, confidenceを取得
    -> CPU側で餌候補追跡と状態機械を更新
```

DMA ISRの中で `Invoke()`を呼ばないでください。ISRはバッファをキューへ渡し、推論タスクが処理します。推論中にDMAが同じバッファを書き換えないよう、2面以上のバッファを使います。

### 10.2 量子化式

モデルの入力テンソルから `scale`と `zero_point`を読み、次で量子化します。

$$
q = round(x / scale) + zero\_point
$$

$$
q = clamp(q, -128, 127)
$$

ここで、学習時の入力を `x = gray / 255.0` としている場合は、次のようになります。

```cpp
float normalized = static_cast<float>(gray) / 255.0f;
int32_t quantized = static_cast<int32_t>(std::lround(
    normalized / input_scale)) + input_zero_point;
quantized = std::clamp(quantized, -128, 127);
input_data[index] = static_cast<int8_t>(quantized);
```

`scale`や`zero_point`を固定値でハードコードしないでください。モデルを再学習・再量子化すると変わる可能性があります。起動時に表示した値を、PC側のTFLiteオフライン検証結果と比較します。検出そのものは実機で行います。

### 10.3 RGB565の注意

データ収集のRGB565はlittle-endianです。1画素を次のように復号します。

```text
pixel = low_byte | (high_byte << 8)
R = ((pixel >> 11) & 0x1f) * 255 / 31
G = ((pixel >> 5)  & 0x3f) * 255 / 63
B = ( pixel        & 0x1f) * 255 / 31
gray = 0.299R + 0.587G + 0.114B
```

実機のVIN出力がYUV422の場合は、RGB565用の処理を使わず、Y成分をグレースケール入力として使えるかを検討します。ただし、学習時も同じ入力表現へ揃えます。

## 11. 推論結果を給餌イベントへ変換する

金魚ボックス推論1回だけで給餌完了を発生させないでください。餌候補の沈下軌跡、金魚ボックスとの距離、近傍での消失、消失後の非再出現をCPU側で検証します。

初期状態機械は次のとおりです。

```text
IDLE
    -> 餌候補が出現し、下方向へ移動
FOOD_SINKING
    -> 金魚ボックスとの距離が縮小
FISH_APPROACHING
    -> 金魚近傍で餌候補が消失
EATING_CANDIDATE
    -> 一定時間、餌候補が再出現しない
FEEDING_COMPLETED
```

次の場合は `unknown` とし、最終給餌時刻を更新しません。

- 餌候補が底面で停止した。
- 餌候補が金魚に隠れたが、消失後の再出現を確認できない。
- カメラ振動や照明変化でフレーム差分が全体的に増えた。
- 金魚ボックスまたは餌候補の追跡が途切れた。

金魚が餌候補へ近づくことは `FISH_APPROACHING` の条件であり、それだけで完了とはしません。金魚近傍で餌候補が消失し、一定時間再出現しないことまで確認します。推論周期は5 fpsを初期値とし、完了後はクールダウンを設けて同じ餌を二重計上しません。

### 11.1 未給餌監視と画面通知

- 最後に確認した `FEEDING_COMPLETED` から **3日間** 新たな給餌完了が確認されない場合、CPU側で未給餌アラート状態にします。
- テスト時は閾値を **10分間** に切り替えられる設定を用意します。本番とテストの閾値を混同しないよう、起動時に有効値をデバッグ表示します。
- `unknown` は給餌完了として扱わず、最終給餌時刻を更新しません。判定不能と未給餌アラートは異なる状態として表示します。
- 再起動後も監視を続けるため、最終給餌時刻と有効な時刻基準を保持します。時刻が未設定または無効な場合は閾値判定を保留します。
- LCDには少なくとも、金魚ボックス、餌候補、状態機械の状態、最終給餌からの経過時間、給餌完了通知、未給餌アラート、`unknown`、監視時刻不明を表示します。
- 画面更新と状態表示はμT-Kernel 3.0上のアプリケーションタスクから行い、カメラDMA割り込み内では実行しません。

## 12. ビルドとフラッシュ

### 12.1 顔検出サンプル

顔検出サンプルはFSP 6.6.0/LLVMのe2 studioプロジェクトです。`readme.txt`の手順に従って `configuration.xml`を開き、Generate Project Contentを実行してからビルドします。

このサンプルを本番ビルドの直接入力にせず、次の確認用に使います。

- Ethos-U55ドライバーが開く
- TFLMがVela済みモデルを読める
- 起動ログにテンソル情報が出る
- NPUとCPUの推論時間を比較できる

### 12.2 本番候補のCMake

FSP 6.4.0/GCC側の基本コマンドは次です。

```powershell
$env:ARM_GCC_TOOLCHAIN_PATH = 'C:/Program Files (x86)/Arm GNU Toolchain arm-none-eabi/13.2 Rel1/bin'
cmake -S test -B test/build/Debug `
    -G Ninja `
    -DCMAKE_BUILD_TYPE=Debug `
    -DCMAKE_TOOLCHAIN_FILE=test/cmake/gcc.cmake `
    -DARM_TOOLCHAIN_PATH="$env:ARM_GCC_TOOLCHAIN_PATH"
cmake --build test/build/Debug --parallel 8
```

`app/`は現時点で空のため、本番AIファームウェアのビルドコマンドはまだ確定していません。`app`のFSPプロジェクトを作成した後、`test`のCMake構成と比較して決めます。

### 12.3 実機操作の順序

1. PC上でモデルをTFLite、Vela、C配列の順に生成する
2. C配列のサイズとSHA-256を記録する
3. FSP設定を再生成する
4. GCCでビルドする
5. ELF/SRECのサイズとリンクマップを確認する
6. J-Linkで書き込む
7. 起動ログでschema、dtype、shape、arena、演算子、NPU初期化を確認する
8. 既知画像または既知フレームを実機へ入力して推論する
9. カメラDMAを接続する
10. 実フレームの前処理と実機の推論結果を、PCのTFLiteオフライン評価結果と比較する

ユーザーが実機操作を明示的に依頼していない場合は、フラッシュやCDC受信を自動では実行しません。

## 13. 検証チェックリスト

### PC側（学習・変換・オフライン評価のみ）

- [ ] train/validation/testがセッション単位で分かれている
- [ ] `fish_visible=0`のフレームを学習・評価から分離した
- [ ] 金魚ボックスと餌軌跡の件数・セッション数を確認した
- [ ] floatモデルのIoU、中心位置誤差、confidenceを記録した
- [ ] Full INT8モデルの同じ指標を記録した
- [ ] 代表データが魚の位置、反射、照明変化を含む
- [ ] TFLiteの入力dtype、shape、scale、zero pointを記録した
- [ ] 必要に応じて、Vela前後のモデルのオフライン整合性を確認した（実機検出の代替にはしない）
- [ ] モデルとC配列のSHA-256を記録した

### ファームウェア側

- [ ] `ARM_NPU`がC/C++両方で定義されている
- [ ] `RM_ETHOSU_Open()`が1回成功する
- [ ] `AddEthosU()`が成功する
- [ ] `AllocateTensors()`が成功する
- [ ] 起動ログの入力shape/dtypeがPC側と一致する
- [ ] 起動ログの量子化パラメータがPC側と一致する
- [ ] `arena_used_bytes()`を記録している
- [ ] カメラDMAバッファが`.sdram_noinit_nocache`にある
- [ ] DMA ISRで推論していない
- [ ] `Invoke()`のレイテンシをGPTまたはサイクルカウンタで測定した
- [ ] NPU実行とCPU実行の結果が一致する
- [ ] 5 fps入力でフレーム取りこぼし数を記録している
- [ ] 実機LCDに金魚枠、餌候補、状態を表示して目視確認できる
- [ ] 最終給餌からの経過時間と未給餌アラートをLCDに表示できる

### システム側

- [ ] 画像結果を複数回平滑化している
- [ ] 餌候補の沈下・接近・消失・非再出現を記録している
- [ ] `feeding_completed`、`unknown`、クールダウンを記録している
- [ ] 未給餌閾値を本番3日・テスト10分で切り替えられる
- [ ] box confidenceとイベント誤完了率をtestセットで決めた
- [ ] 照明、反射、清掃、人影、カメラ振動を含むtestを行った

## 14. 失敗時の切り分け

| 症状 | 最初に見る場所 | 対処 |
|---|---|---|
| 指定サンプルが見つからない | Git履歴、外部リポジトリ | 現在は存在未確認。顔検出サンプルを基準にする |
| `Model schema version`エラー | TFLMとTFLiteのバージョン | 同じ変換環境で再生成する |
| `AllocateTensors()`失敗 | arena、resolver、モデル | arena拡大、演算子追加、モデル再確認 |
| `AddEthosU()`失敗 | FSP設定、`ARM_NPU`、リンク | `rm_ethosu`とcore driverを再生成する |
| NPUを開けない | `RM_ETHOSU_Open()`、TrustZone、FSP | CPU0、権限、生成コードを確認する |
| 入力画像だけ結果が悪い | 前処理、scale、zero point | PCと実機で同じ画像の量子化値を比較する |
| CPUでは動くがNPUで失敗 | Vela出力、未対応演算子、配置 | Velaログと演算子一覧を確認する |
| NPU実行時に画像が崩れる | SDRAMキャッシュ、DMAバッファ | `.sdram_noinit_nocache`、アラインメント、バッファ所有権を確認する |
| メモリ不足 | モデル、arena、スタック | モデルを小さくする、SDRAM/OSPI配置、arena実使用量を測る |
| 実機だけ精度が違う | RGB565/YUV変換、リサイズ | 同一フレームをPCと実機で各段階比較する |
| 5 fpsに追いつかない | `Invoke()`、コピー、JPEG | 推論周期を下げる、前処理を最適化、モデルを縮小する |

## 15. 実装の完了条件

最初のPhase 1を完了とする条件は次です。

1. 給餌マーカーを含む複数セッションを収集する
2. セッション単位のtrain/validation/testを作る
3. 256x256グレースケール金魚ボックス回帰モデルを学習する
4. Full INT8 TFLiteへ変換する
5. PC上でfloat/INT8/Vela済みの結果をオフライン比較する
6. Vela済みモデルをC配列化する
7. FSP 6.4.0/GCCの本番候補へTFLMとEthos-Uを組み込む
8. 実機起動ログでテンソル情報とアリーナ使用量を確認する
9. 実機の実カメラフレームを同じ前処理で推論する
10. NPUレイテンシ、フレーム欠落、box IoU、誤完了率を記録する
11. 餌追跡と金魚ボックスを状態機械へ接続し、給餌完了または `unknown` を出力する

## 16. 参照ファイル

- [給餌行動モデル計画](./feeding-behavior-model-plan.md)
- [データ収集ファームウェアREADME](../data_collector/firmware/README.md)
- [RA8P1 AI/NPUリファレンス](../.github/skills/ek-ra8p1/references/ai-npu.md)
- [Ethos-U55顔検出サンプルREADME](../test/example_projects/ek_ra8p1/ethos_u55_face_detection/ethos_u55_face_detection_ek_ra8p1_notes.md)
- [Ethos-U55顔検出サンプル操作説明](../test/example_projects/ek_ra8p1/ethos_u55_face_detection/readme.txt)
- [TFLMモデル初期化](../test/example_projects/ek_ra8p1/ethos_u55_face_detection/ethos_u55_face_detection_ek_ra8p1_ep/e2studio/src/face_detection_app/common/Model.cc)
- [Ethos-U推論エントリ](../test/example_projects/ek_ra8p1/ethos_u55_face_detection/ethos_u55_face_detection_ek_ra8p1_ep/e2studio/src/ethos_u55.c)
- [Ethos-U演算子登録](../test/example_projects/ek_ra8p1/ethos_u55_face_detection/ethos_u55_face_detection_ek_ra8p1_ep/e2studio/src/face_detection_app/source/use_case/object_detection/src/YoloFastestModel.cc)

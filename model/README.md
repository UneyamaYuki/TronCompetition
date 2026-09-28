# 金魚ボックス回帰モデル

`docs/feeding-behavior-model-plan.md` と `docs/ra8p1-ai-model-implementation-guide.md` に従った、Phase 1 の学習パイプラインです。

モデルは餌を直接検出せず、640x480 の収集画像を 256x256 グレースケールへ変換し、次の5値を正規化して出力します。

```text
[x, y, width, height, confidence]
```

`x` と `y` はbboxの左上座標です。すべての値は `0.0` から `1.0` に正規化されています。

## 1. 依存関係

ワークスペース直下の `.venv` を使用します。

```powershell
& .\.venv\Scripts\Activate.ps1
python -m pip install -r model\requirements.txt
python -m unittest discover -s model\tests
```

## 2. アノテーション

収集データには画像と `records.jsonl` しかなく、金魚ボックスの教師ラベルは含まれません。元の収集データを使う場合は、まず各セッションにCSVの雛形を作成します。

```powershell
python -m model.fish_bbox.make_annotation_template --dataset-root dataset
```

各セッションの `annotations.csv` を編集し、すべてのフレームに次を入力します。

```csv
session_id,sequence,timestamp_ms,fish_x,fish_y,fish_w,fish_h,fish_visible,event_state,reviewer
898059,1,9,0.42,0.68,0.24,0.18,1,idle,manual
```

`fish_x` と `fish_y` は左上、`fish_w` と `fish_h` は幅と高さで、すべて画像サイズで割った `0.0` から `1.0` の値です。`fish_visible=0` の場合も座標を `0` で埋めてください。分割時のリークを避けるため、セッションを3つ以上用意します。

Roboflowから256x256のCOCO形式で出力したデータは、`model/dataset/256x256/train`、`model/dataset/256x256/valid`、`model/dataset/256x256/test` に `_annotations.coco.json` がある構成をそのまま読み込めます。モデル入力は256x256グレースケールです。

現在のモデルは1画像につき1個のbboxを出力します。COCOに複数の金魚がある画像では、画像中心に最も近いbboxを教師値として使用します。これは今回の反射像が実体より外側に現れるデータ設計に対応するためです。

学習の既定値は、Keras公式のImageNet事前学習済み `MobileNetV2 alpha=0.35` をバックボーンに使うファインチューニングです。最初の5 epochは検出ヘッドだけを学習し、その後は低い学習率でバックボーンも更新します。MobileNetV2はDepthwiseConv2Dなど標準演算で構成され、Full INT8化とVelaによるEthos-U55最適化を行います。重みの仕様は[Keras MobileNetドキュメント](https://keras.io/api/applications/mobilenet/)に基づきます。

## 3. 学習

```powershell
python -m model.fish_bbox.train `
  --dataset-root dataset `
  --output-dir build\ml\fish_bbox_256 `
  --input-size 256 `
  --epochs 80
```

Roboflow COCOデータを使う場合は次のようにします。

```powershell
python -m model.fish_bbox.train `
  --dataset-root model\dataset\256x256 `
  --output-dir build\ml\fish_bbox_256 `
  --architecture mobilenetv2 `
  --input-size 256 `
  --epochs 80
```

元の小型CNNを重みなしで学習する場合だけ、`--architecture small_cnn` を指定します。

学習ではセッション単位で train / validation / test に分割します。出力は `fish_bbox_float.keras`、分割情報、学習履歴、test loss です。

## 4. Full INT8変換

代表画像にも学習データと同じグレースケール・リサイズ処理を使い、TFLite Builtin INT8だけで変換します。

```powershell
python -m model.fish_bbox.convert `
  --model build\ml\fish_bbox_256\fish_bbox_float.keras `
  --dataset-root model\dataset\256x256 `
  --input-size 256 `
  --output build\ml\fish_bbox_256\fish_bbox_int8.tflite
```

変換後は入力・出力のdtype、形状、量子化scaleを検査し、`.json`へ記録します。続けてVelaを実行します。

```powershell
vela build\ml\fish_bbox_256\fish_bbox_int8.tflite `
  --accelerator-config=ethos-u55-256 `
  --optimise Performance `
  --memory-mode=Shared_Sram `
  --system-config=Ethos_U55_High_End_Embedded `
  --output-dir=build\ml\fish_bbox_256\vela
```

## 5. 実機組み込み用C配列

Vela済みTFLiteを、`app/src/ai/model/` のC++配列へ変換します。

```powershell
python -m model.fish_bbox.export_c_array `
  --input build\ml\fish_bbox_256\vela\fish_bbox_int8_vela.tflite
```

この出力をTFLM/Ethos-U55のモデルデータとして組み込みます。前処理は実機でも同じ256x256グレースケール化と量子化を行ってください。学習・変換スクリプトの既定入力サイズも256です。

## 6. 検証済み成果物

2026-09-28時点の256x256モデルのCOCO test split（111画像）では、Float Kerasモデルの `mean_iou=0.429438`、`iou50=0.405405`、Full INT8のCPU評価では `mean_iou=0.354975`、`iou50=0.270270` でした。`fish_bbox_int8.json`では入力 `[1, 256, 256, 1]` とINT8量子化を確認しています。実機投入前にはEthos-Uランタイム上の結果も確認します。

256x256モデルのVela summaryは、NPU演算70件、推定推論時間約5.99 ms（500 MHz設定）、SRAM 1216.59 KiB、off-chip flash 494.66 KiB、76,047,616 MACsです。Vela済みモデルは `ethos-u` カスタム演算を含むため、ホストのTensorFlow Lite Interpreterでは実行できず、Ethos-Uランタイム上で検証します。

## 現在のデータについて

Roboflowデータセットは `train=781`、`valid=223`、`test=111` 画像で、COCOラベルを直接読み込みます。学習結果の `test_metrics.json` には、損失に加えてbboxの `mean_iou` と `iou50` を保存します。`test_loss` だけでは位置検出性能を判断できないため、IoUも確認してください。
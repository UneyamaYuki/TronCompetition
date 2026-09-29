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
  --learning-rate 1e-4 `
  --fine-tune-learning-rate 1e-6 `
  --epochs 80
```

元の小型CNNを重みなしで学習する場合だけ、`--architecture small_cnn` を指定します。

学習ではセッション単位で train / validation / test に分割します。optimizerはAdamで、既定の学習率はヘッドが `1e-4`、MobileNetV2のfine-tune時が `1e-6` です。出力は `fish_bbox_float.keras`、分割情報、学習履歴、test loss です。

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

旧データセットの256x256モデル（COCO test split 111画像）では、Float Kerasモデルの `mean_iou=0.429438`、`iou50=0.405405`、Full INT8のCPU評価では `mean_iou=0.354975`、`iou50=0.270270` でした。`fish_bbox_int8.json`では入力 `[1, 256, 256, 1]` とINT8量子化を確認しています。

negative画像を追加した前回の再学習モデルは `build/ml/fish_bbox_256_retrained/` に保存しています。COCO test splitは161画像（positive 113、negative 48）です。

- Float positive-only bbox: `mean_iou=0.233028`、`iou50=0.141593`
- Full INT8 positive-only bbox: `mean_iou=0.141839`、`iou50=0.070796`
- Float confidence（positive / negative平均）: `0.993069 / 0.015325`
- INT8 confidence（positive / negative平均）: `0.985032 / 0.016602`
- confidence `0.5`でのtest結果: Float/INT8ともpositive recall `113/113`、negative false positive `0/48`

全体IoUはnegative画像のbboxを0として計算するため、bbox位置精度の比較にはpositive-only値を使用します。評価詳細は `build/ml/fish_bbox_256_retrained/evaluation.json`、可視化は `build/ml/fish_bbox_256_retrained/test_visualizations_20/contact_sheet_20.jpg` に保存しています。この前回モデルは現在のapp配列には使用していません。実機投入前にはEthos-Uランタイム上の結果も確認します。

今回、Adamの学習率をヘッド `1e-4`、fine-tune `1e-6` に下げて再学習したモデルは `build/ml/fish_bbox_256_retrained_lr1e4/` に保存しています。同じtest splitで次の結果になりました。

- Float positive-only bbox: `mean_iou=0.295863`、`iou50=0.203540`
- Full INT8 positive-only bbox: `mean_iou=0.245458`、`iou50=0.115044`
- Float confidence（positive / negative平均）: `0.996647 / 0.014553`
- INT8 confidence（positive / negative平均）: `0.994573 / 0.023031`
- confidence `0.5`でのtest結果: Float/INT8ともpositive recall `113/113`、negative false positive `0/48`

低学習率モデルの評価詳細は `build/ml/fish_bbox_256_retrained_lr1e4/evaluation.json`、Vela済みTFLiteは `build/ml/fish_bbox_256_retrained_lr1e4/vela/fish_bbox_int8_vela.tflite`、専用C++配列は同ディレクトリの `fish_bbox_model_data.cc/.hpp` です。Vela済みモデルは `app/src/ai/model/fish_bbox_model_data.cc` へ組み込み済みで、公開シンボルは既存の `g_fish_bbox_model_data` を維持しています。

低学習率256x256モデルのVela summaryは、NPU演算70件、推定推論時間約5.99 ms（500 MHz設定）、SRAM 1216.59 KiB、off-chip flash 494.78 KiB、76,047,616 MACsです。Vela済みモデルは `ethos-u` カスタム演算を含むため、ホストのTensorFlow Lite Interpreterでは実行できず、Ethos-Uランタイム上で検証します。

## 現在のデータについて

今回のRoboflowデータセットは `train=3405`、`valid=324`、`test=161` 画像で、COCOラベルを直接読み込みます。negative画像はそれぞれ990、95、48枚です。学習結果の `test_metrics.json` には、損失に加えてbboxの `mean_iou` と `iou50` を保存します。`test_loss` だけでは位置検出性能を判断できないため、positive-only IoUとconfidenceのpositive/negative分離も確認してください。
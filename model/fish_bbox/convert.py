from __future__ import annotations

import argparse
import json
from pathlib import Path

import numpy as np
import tensorflow as tf

from .dataset import INPUT_SIZE, load_calibration_images


def _quantization(detail: dict) -> dict[str, object]:
    scale, zero_point = detail["quantization"]
    return {"scale": float(scale), "zero_point": int(zero_point)}


def convert(
    model_path: Path,
    dataset_root: Path,
    output_path: Path,
    annotation_name: str,
    representative_count: int,
    input_size: int,
) -> None:
    model = tf.keras.models.load_model(model_path, compile=False)
    calibration_images = load_calibration_images(
        dataset_root, annotation_name, input_size=input_size
    )
    if not calibration_images:
        raise ValueError("no calibration images were found")
    calibration_images = calibration_images[:representative_count]

    def representative_dataset():
        for image in calibration_images:
            yield [image[np.newaxis, ...].astype(np.float32)]

    converter = tf.lite.TFLiteConverter.from_keras_model(model)
    converter.optimizations = [tf.lite.Optimize.DEFAULT]
    converter.representative_dataset = representative_dataset
    converter.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
    converter.inference_input_type = tf.int8
    converter.inference_output_type = tf.int8
    model_bytes = converter.convert()

    interpreter = tf.lite.Interpreter(model_content=model_bytes)
    interpreter.allocate_tensors()
    input_detail = interpreter.get_input_details()[0]
    output_detail = interpreter.get_output_details()[0]
    if input_detail["dtype"] is not np.int8 or output_detail["dtype"] is not np.int8:
        raise RuntimeError("converted model is not full INT8")
    if list(input_detail["shape"]) != [1, input_size, input_size, 1]:
        raise RuntimeError(f"unexpected input shape: {input_detail['shape']}")
    if _quantization(input_detail)["scale"] == 0.0 or _quantization(output_detail)["scale"] == 0.0:
        raise RuntimeError("converted model has a zero quantization scale")

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_bytes(model_bytes)
    metadata = {
        "input": {
            "shape": [int(value) for value in input_detail["shape"]],
            "dtype": str(input_detail["dtype"]),
            "quantization": _quantization(input_detail),
        },
        "output": {
            "shape": [int(value) for value in output_detail["shape"]],
            "dtype": str(output_detail["dtype"]),
            "quantization": _quantization(output_detail),
        },
        "representative_images": len(calibration_images),
    }
    output_path.with_suffix(".json").write_text(
        json.dumps(metadata, indent=2, ensure_ascii=True) + "\n", encoding="utf-8"
    )
    print(f"saved: {output_path}")
    print(json.dumps(metadata, indent=2, ensure_ascii=True))


def main() -> None:
    parser = argparse.ArgumentParser(description="Convert the trained fish model to full INT8 TFLite.")
    parser.add_argument("--model", type=Path, default=Path("build/ml/fish_bbox_256/fish_bbox_float.keras"))
    parser.add_argument("--dataset-root", type=Path, default=Path("dataset"))
    parser.add_argument("--output", type=Path, default=Path("build/ml/fish_bbox_256/fish_bbox_int8.tflite"))
    parser.add_argument("--annotation-name", default="annotations.csv")
    parser.add_argument("--representative-count", type=int, default=256)
    parser.add_argument("--input-size", type=int, default=INPUT_SIZE)
    args = parser.parse_args()
    convert(
        model_path=args.model,
        dataset_root=args.dataset_root,
        output_path=args.output,
        annotation_name=args.annotation_name,
        representative_count=args.representative_count,
        input_size=args.input_size,
    )


if __name__ == "__main__":
    main()
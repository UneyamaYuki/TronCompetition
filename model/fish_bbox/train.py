from __future__ import annotations

import argparse
import json
from pathlib import Path

import numpy as np
import tensorflow as tf

from .dataset import (
    INPUT_SIZE,
    find_labeled_sessions,
    has_coco_dataset,
    load_coco_dataset_splits,
    load_session_samples,
    samples_to_arrays,
    session_names,
    split_by_session,
)
from .model import (
    DEFAULT_LEARNING_RATE,
    bbox_loss,
    build_model,
    build_mobilenetv2_model,
)

DEFAULT_FINE_TUNE_LEARNING_RATE = 1e-6


def _jsonable_history(history: dict[str, list[float]]) -> dict[str, list[float]]:
    return {name: [float(value) for value in values] for name, values in history.items()}


def _bbox_metrics(predictions: np.ndarray, targets: np.ndarray) -> dict[str, float]:
    predicted = np.clip(predictions[:, :4], 0.0, 1.0)
    predicted_right = np.clip(predicted[:, 0] + predicted[:, 2], 0.0, 1.0)
    predicted_bottom = np.clip(predicted[:, 1] + predicted[:, 3], 0.0, 1.0)
    target_right = np.clip(targets[:, 0] + targets[:, 2], 0.0, 1.0)
    target_bottom = np.clip(targets[:, 1] + targets[:, 3], 0.0, 1.0)
    intersection = np.maximum(
        0.0, np.minimum(predicted_right, target_right) - np.maximum(predicted[:, 0], targets[:, 0])
    ) * np.maximum(
        0.0, np.minimum(predicted_bottom, target_bottom) - np.maximum(predicted[:, 1], targets[:, 1])
    )
    predicted_area = np.maximum(0.0, predicted_right - predicted[:, 0]) * np.maximum(
        0.0, predicted_bottom - predicted[:, 1]
    )
    target_area = targets[:, 2] * targets[:, 3]
    union = predicted_area + target_area - intersection
    ious = np.divide(intersection, union, out=np.zeros_like(intersection), where=union > 0.0)
    return {
        "mean_iou": float(np.mean(ious)),
        "iou50": float(np.mean(ious >= 0.5)),
    }


def _merge_histories(*histories: dict[str, list[float]]) -> dict[str, list[float]]:
    merged: dict[str, list[float]] = {}
    for history in histories:
        for name, values in history.items():
            merged.setdefault(name, []).extend(values)
    return merged


def train(
    dataset_root: Path,
    output_dir: Path,
    annotation_name: str,
    epochs: int,
    batch_size: int,
    seed: int,
    architecture: str,
    input_size: int,
    learning_rate: float = DEFAULT_LEARNING_RATE,
    fine_tune_learning_rate: float = DEFAULT_FINE_TUNE_LEARNING_RATE,
) -> None:
    tf.keras.utils.set_random_seed(seed)
    if has_coco_dataset(dataset_root):
        coco_splits = load_coco_dataset_splits(dataset_root)
        splits = {
            "train": coco_splits["train"],
            "validation": coco_splits["valid"],
            "test": coco_splits["test"],
        }
    else:
        session_dirs = find_labeled_sessions(dataset_root, annotation_name)
        samples = [
            sample
            for session_dir in session_dirs
            for sample in load_session_samples(session_dir, annotation_name)
        ]
        splits = split_by_session(samples, seed=seed)
    train_images, train_targets = samples_to_arrays(splits["train"], input_size=input_size)
    validation_images, validation_targets = samples_to_arrays(
        splits["validation"], input_size=input_size
    )
    test_images, test_targets = samples_to_arrays(splits["test"], input_size=input_size)

    output_dir.mkdir(parents=True, exist_ok=True)
    split_manifest = {
        "architecture": architecture,
        "input_size": input_size,
        "optimizer": "Adam",
        "learning_rate": learning_rate,
        "fine_tune_learning_rate": fine_tune_learning_rate,
    }
    split_manifest.update(
        {
            split_name: {
                "frames": len(split_samples),
                "sessions": session_names(split_samples),
            }
            for split_name, split_samples in splits.items()
        }
    )
    (output_dir / "split_manifest.json").write_text(
        json.dumps(split_manifest, indent=2, ensure_ascii=True) + "\n", encoding="utf-8"
    )

    if architecture == "mobilenetv2":
        model, backbone = build_mobilenetv2_model(
            input_size=input_size, learning_rate=learning_rate
        )
    else:
        model = build_model(input_size=input_size, learning_rate=learning_rate)
        backbone = None
    checkpoint_path = str(output_dir / "fish_bbox_best.keras")
    training_log_path = str(output_dir / "training.csv")
    callbacks = [
        tf.keras.callbacks.EarlyStopping(
            monitor="val_loss", patience=10, restore_best_weights=True
        ),
        tf.keras.callbacks.ModelCheckpoint(
            filepath=checkpoint_path, monitor="val_loss", save_best_only=True
        ),
        tf.keras.callbacks.CSVLogger(filename=training_log_path),
    ]
    histories: list[dict[str, list[float]]] = []
    if backbone is not None:
        warmup_epochs = min(5, epochs)
        warmup_history = model.fit(
            train_images,
            train_targets,
            validation_data=(validation_images, validation_targets),
            epochs=warmup_epochs,
            batch_size=batch_size,
            shuffle=True,
            callbacks=callbacks,
            verbose=2,
        )
        histories.append(warmup_history.history)
        if warmup_epochs < epochs:
            backbone.trainable = True
            for layer in backbone.layers:
                if isinstance(layer, tf.keras.layers.BatchNormalization):
                    layer.trainable = False
            model.compile(
                optimizer=tf.keras.optimizers.Adam(learning_rate=fine_tune_learning_rate),
                loss=bbox_loss,
            )
            fine_tune_history = model.fit(
                train_images,
                train_targets,
                validation_data=(validation_images, validation_targets),
                initial_epoch=warmup_epochs,
                epochs=epochs,
                batch_size=batch_size,
                shuffle=True,
                callbacks=callbacks,
                verbose=2,
            )
            histories.append(fine_tune_history.history)
    else:
        history = model.fit(
            train_images,
            train_targets,
            validation_data=(validation_images, validation_targets),
            epochs=epochs,
            batch_size=batch_size,
            shuffle=True,
            callbacks=callbacks,
            verbose=2,
        )
        histories.append(history.history)
    test_metrics = model.evaluate(test_images, test_targets, batch_size=batch_size, verbose=0)
    if isinstance(test_metrics, list):
        test_loss = float(test_metrics[0])
    else:
        test_loss = float(test_metrics)
    test_predictions = model.predict(test_images, batch_size=batch_size, verbose=0)
    bbox_metrics = _bbox_metrics(test_predictions, test_targets)
    model.save(str(output_dir / "fish_bbox_float.keras"))
    (output_dir / "history.json").write_text(
        json.dumps(_jsonable_history(_merge_histories(*histories)), indent=2, ensure_ascii=True) + "\n",
        encoding="utf-8",
    )
    (output_dir / "test_metrics.json").write_text(
        json.dumps({"loss": test_loss, **bbox_metrics}, indent=2, ensure_ascii=True) + "\n",
        encoding="utf-8",
    )
    print(f"saved: {output_dir / 'fish_bbox_float.keras'}")
    print(f"test_loss: {test_loss:.6f}")
    print(f"mean_iou: {bbox_metrics['mean_iou']:.6f}")
    print(f"iou50: {bbox_metrics['iou50']:.6f}")


def main() -> None:
    parser = argparse.ArgumentParser(description="Train the INT8-ready fish bounding-box regression model.")
    parser.add_argument("--dataset-root", type=Path, default=Path("dataset"))
    parser.add_argument("--output-dir", type=Path, default=Path("build/ml/fish_bbox_256"))
    parser.add_argument("--annotation-name", default="annotations.csv")
    parser.add_argument("--epochs", type=int, default=80)
    parser.add_argument("--batch-size", type=int, default=16)
    parser.add_argument("--seed", type=int, default=20260927)
    parser.add_argument("--input-size", type=int, default=INPUT_SIZE)
    parser.add_argument("--learning-rate", type=float, default=DEFAULT_LEARNING_RATE)
    parser.add_argument(
        "--fine-tune-learning-rate",
        type=float,
        default=DEFAULT_FINE_TUNE_LEARNING_RATE,
    )
    parser.add_argument(
        "--architecture",
        choices=("mobilenetv2", "small_cnn"),
        default="mobilenetv2",
        help="Use public ImageNet MobileNetV2 weights or the original scratch CNN.",
    )
    args = parser.parse_args()
    train(
        dataset_root=args.dataset_root,
        output_dir=args.output_dir,
        annotation_name=args.annotation_name,
        epochs=args.epochs,
        batch_size=args.batch_size,
        seed=args.seed,
        architecture=args.architecture,
        input_size=args.input_size,
        learning_rate=args.learning_rate,
        fine_tune_learning_rate=args.fine_tune_learning_rate,
    )


if __name__ == "__main__":
    main()
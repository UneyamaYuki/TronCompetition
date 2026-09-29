"""Run a small, reproducible YOLOv5 INT8 test-set visualization."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import numpy as np
import tensorflow as tf
from PIL import Image, ImageDraw, ImageFont


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--model",
        type=Path,
        default=Path("build/ml/yolov5s_256_RGB_train/weights/best-int8.tflite"),
    )
    parser.add_argument(
        "--dataset-root",
        type=Path,
        default=Path("build/ml/yolov5s_256_RGB_dataset"),
    )
    parser.add_argument(
        "--selection-summary",
        type=Path,
        default=Path("build/ml/fish_bbox_256/test_visualizations_20/summary.json"),
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=Path("build/ml/yolov5s_256_RGB_test_visualizations_20"),
    )
    parser.add_argument("--confidence", type=float, default=0.25)
    parser.add_argument("--iou", type=float, default=0.45)
    return parser.parse_args()


def stem_key(path: Path) -> str:
    return path.stem.split(".rf.", 1)[0]


def dequantize(values: np.ndarray, details: dict) -> np.ndarray:
    scale, zero_point = details["quantization"]
    if scale:
        return (values.astype(np.float32) - zero_point) * scale
    return values.astype(np.float32)


def quantize_input(image: np.ndarray, details: dict) -> np.ndarray:
    scale, zero_point = details["quantization"]
    if not scale:
        return image.astype(details["dtype"])[None]
    normalized = image.astype(np.float32) / 255.0
    quantized = np.rint(normalized / scale + zero_point)
    info = np.iinfo(details["dtype"])
    return np.clip(quantized, info.min, info.max).astype(details["dtype"])[None]


def box_iou(box: np.ndarray, boxes: np.ndarray) -> np.ndarray:
    top_left = np.maximum(box[:2], boxes[:, :2])
    bottom_right = np.minimum(box[2:], boxes[:, 2:])
    intersection_size = np.maximum(0.0, bottom_right - top_left)
    intersection = intersection_size[:, 0] * intersection_size[:, 1]
    box_area = max(0.0, box[2] - box[0]) * max(0.0, box[3] - box[1])
    boxes_area = np.maximum(0.0, boxes[:, 2] - boxes[:, 0]) * np.maximum(
        0.0, boxes[:, 3] - boxes[:, 1]
    )
    return intersection / np.maximum(box_area + boxes_area - intersection, 1e-8)


def nms(boxes: np.ndarray, scores: np.ndarray, iou_threshold: float) -> list[int]:
    order = scores.argsort()[::-1]
    kept: list[int] = []
    while order.size:
        index = int(order[0])
        kept.append(index)
        if order.size == 1:
            break
        overlaps = box_iou(boxes[index], boxes[order[1:]])
        order = order[1:][overlaps <= iou_threshold]
    return kept


def decode_predictions(
    output: np.ndarray,
    image_size: tuple[int, int],
    confidence_threshold: float,
    iou_threshold: float,
) -> list[dict[str, float | list[float]]]:
    height, width = image_size
    candidates = output[0]
    objectness = np.clip(candidates[:, 4], 0.0, 1.0)
    class_score = np.clip(candidates[:, 5:].max(axis=1), 0.0, 1.0)
    scores = objectness * class_score
    mask = scores >= confidence_threshold
    if not np.any(mask):
        return []

    selected = candidates[mask]
    selected_scores = scores[mask]
    centers = selected[:, :2]
    sizes = selected[:, 2:4]
    valid_size = np.all(sizes > 0.0, axis=1)
    selected = selected[valid_size]
    selected_scores = selected_scores[valid_size]
    objectness = objectness[mask][valid_size]
    class_score = class_score[mask][valid_size]
    sizes = selected[:, 2:4]
    centers = selected[:, :2]
    if not len(selected):
        return []
    boxes = np.concatenate((centers - sizes / 2.0, centers + sizes / 2.0), axis=1)
    boxes *= np.array([width, height, width, height], dtype=np.float32)
    boxes[:, [0, 2]] = np.clip(boxes[:, [0, 2]], 0.0, width)
    boxes[:, [1, 3]] = np.clip(boxes[:, [1, 3]], 0.0, height)

    kept = nms(boxes, selected_scores, iou_threshold)
    predictions = []
    for index in kept:
        predictions.append(
            {
                "box": boxes[index].round(3).tolist(),
                "score": float(selected_scores[index]),
                "objectness": float(objectness[index]),
                "class_score": float(class_score[index]),
            }
        )
    return predictions


def read_labels(label_path: Path, image_size: tuple[int, int]) -> list[list[float]]:
    height, width = image_size
    if not label_path.exists():
        return []
    boxes = []
    for line in label_path.read_text(encoding="ascii").splitlines():
        values = line.split()
        if len(values) != 5:
            continue
        _, center_x, center_y, box_width, box_height = map(float, values)
        boxes.append(
            [
                (center_x - box_width / 2.0) * width,
                (center_y - box_height / 2.0) * height,
                (center_x + box_width / 2.0) * width,
                (center_y + box_height / 2.0) * height,
            ]
        )
    return boxes


def center_box(boxes: list[list[float]], image_size: tuple[int, int]) -> list[float] | None:
    if not boxes:
        return None
    height, width = image_size
    image_center = np.array([width / 2.0, height / 2.0])
    return min(
        boxes,
        key=lambda box: np.linalg.norm(
            (np.array([box[0] + box[2], box[1] + box[3]]) / 2.0) - image_center
        ),
    )


def draw_box(draw: ImageDraw.ImageDraw, box: list[float], color: str, width: int) -> None:
    draw.rectangle(tuple(box), outline=color, width=width)


def render_image(
    image: Image.Image,
    predictions: list[dict[str, float | list[float]]],
    ground_truth: list[list[float]],
    selected_index: int | None,
) -> Image.Image:
    rendered = image.copy()
    draw = ImageDraw.Draw(rendered)
    for box in ground_truth:
        draw_box(draw, box, "lime", 2)
    for index, prediction in enumerate(predictions):
        box = prediction["box"]
        score = float(prediction["score"])
        color = "red" if index == selected_index else "deepskyblue"
        draw_box(draw, box, color, 3 if index == selected_index else 2)
        draw.text((float(box[0]) + 2, float(box[1]) + 2), f"{score:.2f}", fill=color)
    return rendered


def make_contact_sheet(images: list[Image.Image], output_path: Path) -> None:
    tile_width, tile_height = 320, 320
    columns = 4
    rows = (len(images) + columns - 1) // columns
    sheet = Image.new("RGB", (columns * tile_width, rows * tile_height), "white")
    for index, image in enumerate(images):
        tile = image.copy()
        tile.thumbnail((tile_width, tile_height))
        x = (index % columns) * tile_width + (tile_width - tile.width) // 2
        y = (index // columns) * tile_height + (tile_height - tile.height) // 2
        sheet.paste(tile, (x, y))
    sheet.save(output_path, quality=95)


def intersection_over_union(first: list[float], second: list[float]) -> float:
    first_array = np.asarray(first, dtype=np.float32)
    second_array = np.asarray(second, dtype=np.float32)
    return float(box_iou(first_array, second_array[None])[0])


def main() -> None:
    args = parse_args()
    test_images = args.dataset_root / "test" / "images"
    test_labels = args.dataset_root / "test" / "labels"
    args.output_dir.mkdir(parents=True, exist_ok=True)

    selection = json.loads(args.selection_summary.read_text(encoding="utf-8"))
    all_images = {stem_key(path): path for path in test_images.glob("*.jpg")}
    selected_names = [entry["image"] for entry in selection["images"][:20]]
    image_paths = []
    for selected_name in selected_names:
        key = stem_key(Path(selected_name))
        if key not in all_images:
            raise FileNotFoundError(f"RGB test image not found for {selected_name}")
        image_paths.append(all_images[key])

    interpreter = tf.lite.Interpreter(model_path=str(args.model))
    interpreter.allocate_tensors()
    input_details = interpreter.get_input_details()[0]
    output_details = interpreter.get_output_details()[0]
    rendered_images = []
    image_results = []

    for image_index, image_path in enumerate(image_paths, start=1):
        image = Image.open(image_path).convert("RGB")
        image_array = np.asarray(image)
        interpreter.set_tensor(input_details["index"], quantize_input(image_array, input_details))
        interpreter.invoke()
        output = dequantize(interpreter.get_tensor(output_details["index"]), output_details)
        predictions = decode_predictions(
            output,
            image.size[::-1],
            args.confidence,
            args.iou,
        )
        labels = read_labels(test_labels / f"{image_path.stem}.txt", image.size[::-1])
        target = center_box(labels, image.size[::-1])
        selected_index = None
        if predictions:
            image_center = np.array([image.width / 2.0, image.height / 2.0])
            selected_index = min(
                range(len(predictions)),
                key=lambda index: np.linalg.norm(
                    (np.asarray(predictions[index]["box"])[[0, 1, 2, 3]].reshape(2, 2).mean(axis=0))
                    - image_center
                ),
            )
        selected_prediction = predictions[selected_index] if selected_index is not None else None
        output_image = render_image(image, predictions, labels, selected_index)
        output_name = f"{image_index:02d}_{image_path.name}"
        output_image.save(args.output_dir / output_name, quality=95)
        rendered_images.append(output_image)

        result = {
            "index": image_index,
            "image": image_path.name,
            "ground_truth_count": len(labels),
            "detections": predictions,
            "selected_detection_index": selected_index,
            "selected_iou": (
                intersection_over_union(selected_prediction["box"], target)
                if selected_prediction is not None and target is not None
                else None
            ),
        }
        image_results.append(result)

    make_contact_sheet(rendered_images, args.output_dir / "contact_sheet_20.jpg")
    positive_results = [result for result in image_results if result["ground_truth_count"]]
    selected_ious = [result["selected_iou"] for result in positive_results if result["selected_iou"] is not None]
    summary = {
        "model": str(args.model),
        "dataset": str(args.dataset_root),
        "count": len(image_results),
        "confidence_threshold": args.confidence,
        "iou_threshold": args.iou,
        "mean_selected_iou": float(np.mean(selected_ious)) if selected_ious else None,
        "images_with_detection": sum(bool(result["detections"]) for result in image_results),
        "total_detections_after_nms": sum(len(result["detections"]) for result in image_results),
        "images": image_results,
    }
    (args.output_dir / "summary.json").write_text(
        json.dumps(summary, indent=2, ensure_ascii=True), encoding="utf-8"
    )
    print(json.dumps({key: value for key, value in summary.items() if key != "images"}, indent=2))


if __name__ == "__main__":
    main()
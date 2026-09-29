"""Convert the Roboflow COCO splits to a YOLOv5 dataset."""

import argparse
import json
import shutil
from collections import defaultdict
from pathlib import Path


SPLITS = ("train", "valid", "test")


def _parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--source-root",
        type=Path,
        default=Path("model/dataset/256x256"),
    )
    parser.add_argument("--output-root", type=Path, required=True)
    parser.add_argument("--category-id", type=int, default=1)
    return parser.parse_args()


def _yolo_box(annotation: dict, image_width: int, image_height: int) -> str | None:
    x, y, width, height = (float(value) for value in annotation["bbox"])
    left = max(0.0, min(x, image_width))
    top = max(0.0, min(y, image_height))
    right = max(left, min(x + width, image_width))
    bottom = max(top, min(y + height, image_height))
    clipped_width = right - left
    clipped_height = bottom - top
    if clipped_width <= 0.0 or clipped_height <= 0.0:
        return None

    center_x = (left + right) / 2.0 / image_width
    center_y = (top + bottom) / 2.0 / image_height
    normalized_width = clipped_width / image_width
    normalized_height = clipped_height / image_height
    return (
        f"0 {center_x:.6f} {center_y:.6f} "
        f"{normalized_width:.6f} {normalized_height:.6f}"
    )


def _convert_split(source_root: Path, output_root: Path, split: str, category_id: int) -> dict:
    source_split = source_root / split
    annotation_path = source_split / "_annotations.coco.json"
    data = json.loads(annotation_path.read_text(encoding="utf-8"))
    category_ids = {category["id"] for category in data["categories"]}
    if category_id not in category_ids:
        raise ValueError(f"category_id={category_id} is missing from {annotation_path}")

    annotations_by_image: dict[int, list[dict]] = defaultdict(list)
    for annotation in data["annotations"]:
        if annotation["category_id"] == category_id:
            annotations_by_image[annotation["image_id"]].append(annotation)

    image_output = output_root / split / "images"
    label_output = output_root / split / "labels"
    image_output.mkdir(parents=True, exist_ok=True)
    label_output.mkdir(parents=True, exist_ok=True)

    image_count = 0
    box_count = 0
    for image in data["images"]:
        source_image = source_split / image["file_name"]
        if not source_image.is_file():
            raise FileNotFoundError(source_image)
        destination_image = image_output / source_image.name
        shutil.copy2(source_image, destination_image)

        lines = []
        for annotation in annotations_by_image[image["id"]]:
            line = _yolo_box(annotation, image["width"], image["height"])
            if line is not None:
                lines.append(line)
        (label_output / f"{source_image.stem}.txt").write_text(
            "\n".join(lines) + ("\n" if lines else ""),
            encoding="ascii",
        )
        image_count += 1
        box_count += len(lines)

    return {"images": image_count, "boxes": box_count}


def _write_yaml(output_root: Path) -> None:
    dataset_path = output_root.resolve().as_posix()
    yaml_text = (
        f"path: {dataset_path}\n"
        "train: train/images\n"
        "val: valid/images\n"
        "test: test/images\n"
        "nc: 1\n"
        "names:\n"
        "  0: goldfish\n"
    )
    (output_root / "dataset.yaml").write_text(yaml_text, encoding="ascii")


def main() -> None:
    args = _parse_args()
    args.output_root.mkdir(parents=True, exist_ok=True)
    results = {
        split: _convert_split(args.source_root, args.output_root, split, args.category_id)
        for split in SPLITS
    }
    _write_yaml(args.output_root)
    print(json.dumps(results, sort_keys=True))


if __name__ == "__main__":
    main()
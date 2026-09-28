from __future__ import annotations

import csv
import json
import re
from collections import Counter, defaultdict
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable, Sequence

import numpy as np
from PIL import Image

INPUT_SIZE = 256
ANNOTATION_FIELDS = (
    "session_id",
    "sequence",
    "timestamp_ms",
    "fish_x",
    "fish_y",
    "fish_w",
    "fish_h",
    "fish_visible",
    "event_state",
    "reviewer",
)
EVENT_STATES = frozenset(
    {
        "idle",
        "food_sinking",
        "fish_approaching",
        "eating_candidate",
        "feeding_completed",
        "unknown",
    }
)
COCO_SPLITS = ("train", "valid", "test")
COCO_ANNOTATION_FILE = "_annotations.coco.json"


@dataclass(frozen=True)
class FrameRecord:
    session_dir: Path
    session_id: int
    sequence: int
    timestamp_ms: int
    image_path: Path


@dataclass(frozen=True)
class LabeledFrame:
    frame: FrameRecord
    target: np.ndarray


def read_frame_records(session_dir: Path) -> list[FrameRecord]:
    records_path = session_dir / "records.jsonl"
    if not records_path.is_file():
        raise FileNotFoundError(f"records.jsonl was not found: {records_path}")

    frames: list[FrameRecord] = []
    for line_number, line in enumerate(
        records_path.read_text(encoding="utf-8").splitlines(), start=1
    ):
        if not line.strip():
            continue
        try:
            record = json.loads(line)
        except json.JSONDecodeError as error:
            raise ValueError(f"invalid JSON at {records_path}:{line_number}") from error
        if record.get("record_type") != 2:
            continue
        filename = record.get("filename")
        if not isinstance(filename, str):
            continue
        image_path = session_dir / filename
        if not image_path.is_file():
            raise FileNotFoundError(f"frame image was not found: {image_path}")
        frames.append(
            FrameRecord(
                session_dir=session_dir,
                session_id=int(record["session_id"]),
                sequence=int(record["sequence"]),
                timestamp_ms=int(record["timestamp_ms"]),
                image_path=image_path,
            )
        )

    if not frames:
        raise ValueError(f"no frame records with images were found: {records_path}")
    return frames


def _read_annotations(annotation_path: Path) -> dict[tuple[int, int], np.ndarray]:
    with annotation_path.open(newline="", encoding="utf-8") as stream:
        reader = csv.DictReader(stream)
        fieldnames = reader.fieldnames or []
        missing_fields = [field for field in ANNOTATION_FIELDS if field not in fieldnames]
        if missing_fields:
            raise ValueError(
                f"{annotation_path} is missing columns: {', '.join(missing_fields)}"
            )

        annotations: dict[tuple[int, int], np.ndarray] = {}
        for row_number, row in enumerate(reader, start=2):
            try:
                session_id = int(row["session_id"])
                sequence = int(row["sequence"])
                values = np.asarray(
                    [
                        float(row["fish_x"]),
                        float(row["fish_y"]),
                        float(row["fish_w"]),
                        float(row["fish_h"]),
                        float(row["fish_visible"]),
                    ],
                    dtype=np.float32,
                )
            except (TypeError, ValueError) as error:
                raise ValueError(f"invalid annotation at {annotation_path}:{row_number}") from error

            key = (session_id, sequence)
            if key in annotations:
                raise ValueError(f"duplicate annotation at {annotation_path}:{row_number}")
            if not np.all(np.isfinite(values)):
                raise ValueError(f"non-finite annotation at {annotation_path}:{row_number}")
            if np.any(values[:4] < 0.0) or np.any(values[:4] > 1.0):
                raise ValueError(f"box values must be normalized to [0, 1]: {annotation_path}:{row_number}")
            if values[4] not in (0.0, 1.0):
                raise ValueError(f"fish_visible must be 0 or 1: {annotation_path}:{row_number}")
            if row["event_state"] not in EVENT_STATES:
                raise ValueError(f"unknown event_state at {annotation_path}:{row_number}")
            if not row["reviewer"].strip():
                raise ValueError(f"reviewer must not be empty: {annotation_path}:{row_number}")
            annotations[key] = values
    return annotations


def load_session_samples(session_dir: Path, annotation_name: str = "annotations.csv") -> list[LabeledFrame]:
    annotation_path = session_dir / annotation_name
    if not annotation_path.is_file():
        raise FileNotFoundError(
            f"annotation file was not found: {annotation_path}. "
            "Create it with make_annotation_template.py and fill every row."
        )

    frames = read_frame_records(session_dir)
    annotations = _read_annotations(annotation_path)
    samples: list[LabeledFrame] = []
    for frame in frames:
        key = (frame.session_id, frame.sequence)
        target = annotations.get(key)
        if target is None:
            raise ValueError(f"missing annotation for session={key[0]} sequence={key[1]}")
        samples.append(LabeledFrame(frame=frame, target=target))

    frame_keys = {(frame.session_id, frame.sequence) for frame in frames}
    extra_keys = sorted(set(annotations) - frame_keys)
    if extra_keys:
        raise ValueError(f"annotations refer to unknown frames: {extra_keys[:3]}")
    return samples


def _coco_category_id(payload: dict[str, Any], category_name: str) -> int:
    categories = payload.get("categories")
    annotations = payload.get("annotations")
    if not isinstance(categories, list) or not isinstance(annotations, list):
        raise ValueError("COCO JSON must contain categories and annotations lists")

    category_ids = {
        int(category["id"])
        for category in categories
        if isinstance(category, dict)
        and str(category.get("name", "")).casefold() == category_name.casefold()
    }
    if not category_ids:
        available = [str(category.get("name", "")) for category in categories]
        raise ValueError(
            f"category {category_name!r} was not found in COCO JSON; available: {available}"
        )

    annotation_counts = Counter(
        int(annotation["category_id"])
        for annotation in annotations
        if isinstance(annotation, dict) and "category_id" in annotation
    )
    selected_id = max(category_ids, key=lambda category_id: annotation_counts[category_id])
    if annotation_counts[selected_id] == 0:
        raise ValueError(f"COCO category {category_name!r} has no annotations")
    return selected_id


def _coco_frame_metadata(image: dict[str, Any], image_id: int) -> tuple[int, int]:
    extra = image.get("extra")
    extra_name = extra.get("name") if isinstance(extra, dict) else None
    match = re.search(r"frame_(\d+)_(\d+)", str(extra_name or image.get("file_name", "")))
    if match is None:
        return image_id + 1, 0
    return int(match.group(1)), int(match.group(2))


def _coco_box_target(annotation: dict[str, Any], width: int, height: int) -> np.ndarray:
    bbox = annotation.get("bbox")
    if not isinstance(bbox, list) or len(bbox) != 4:
        raise ValueError(f"COCO annotation has an invalid bbox: {annotation}")
    x, y, box_width, box_height = (float(value) for value in bbox)
    if not np.all(np.isfinite([x, y, box_width, box_height])):
        raise ValueError(f"COCO annotation has a non-finite bbox: {annotation}")
    left = max(0.0, min(float(width), x))
    top = max(0.0, min(float(height), y))
    right = max(left, min(float(width), x + box_width))
    bottom = max(top, min(float(height), y + box_height))
    if right <= left or bottom <= top:
        raise ValueError(f"COCO annotation bbox is outside the image: {annotation}")
    return np.asarray(
        [left / width, top / height, (right - left) / width, (bottom - top) / height, 1.0],
        dtype=np.float32,
    )


def _select_coco_annotation(
    annotations: Sequence[dict[str, Any]], width: int, height: int
) -> dict[str, Any]:
    image_center_x = width / 2.0
    image_center_y = height / 2.0

    def selection_key(annotation: dict[str, Any]) -> tuple[float, float]:
        x, y, box_width, box_height = (float(value) for value in annotation["bbox"])
        center_x = x + box_width / 2.0
        center_y = y + box_height / 2.0
        distance_squared = (center_x - image_center_x) ** 2 + (center_y - image_center_y) ** 2
        area = box_width * box_height
        return distance_squared, -area

    return min(annotations, key=selection_key)


def _load_coco_split(split_dir: Path, category_name: str) -> list[LabeledFrame]:
    annotation_path = split_dir / COCO_ANNOTATION_FILE
    payload = json.loads(annotation_path.read_text(encoding="utf-8"))
    if not isinstance(payload, dict):
        raise ValueError(f"COCO root must be an object: {annotation_path}")
    category_id = _coco_category_id(payload, category_name)

    annotations_by_image: dict[int, list[dict[str, Any]]] = defaultdict(list)
    annotations = payload["annotations"]
    for annotation in annotations:
        if isinstance(annotation, dict) and int(annotation.get("category_id", -1)) == category_id:
            annotations_by_image[int(annotation["image_id"])].append(annotation)

    images = payload.get("images")
    if not isinstance(images, list):
        raise ValueError(f"COCO JSON does not contain an images list: {annotation_path}")

    samples: list[LabeledFrame] = []
    for image in images:
        if not isinstance(image, dict):
            raise ValueError(f"COCO image must be an object: {annotation_path}")
        image_id = int(image["id"])
        filename = image.get("file_name")
        if not isinstance(filename, str):
            raise ValueError(f"COCO image has no file_name: {annotation_path}:{image_id}")
        image_path = split_dir / filename
        if not image_path.is_file():
            raise FileNotFoundError(f"COCO image was not found: {image_path}")
        width = int(image["width"])
        height = int(image["height"])
        sequence, timestamp_ms = _coco_frame_metadata(image, image_id)
        frame = FrameRecord(
            session_dir=split_dir,
            session_id=0,
            sequence=sequence,
            timestamp_ms=timestamp_ms,
            image_path=image_path,
        )
        image_annotations = annotations_by_image.get(image_id, [])
        if image_annotations:
            # Reflections are annotated as extra fish; the real fish is nearest the image center.
            annotation = _select_coco_annotation(image_annotations, width, height)
            target = _coco_box_target(annotation, width, height)
        else:
            target = np.zeros(5, dtype=np.float32)
        samples.append(LabeledFrame(frame=frame, target=target))
    return samples


def has_coco_dataset(dataset_root: Path) -> bool:
    return all(
        (dataset_root / split / COCO_ANNOTATION_FILE).is_file()
        for split in COCO_SPLITS
    )


def load_coco_dataset_splits(
    dataset_root: Path, category_name: str = "goldfish"
) -> dict[str, list[LabeledFrame]]:
    if not has_coco_dataset(dataset_root):
        raise ValueError(
            f"expected COCO splits train/valid/test under {dataset_root} "
            f"with {COCO_ANNOTATION_FILE}"
        )
    return {
        split: _load_coco_split(dataset_root / split, category_name)
        for split in COCO_SPLITS
    }


def find_labeled_sessions(dataset_root: Path, annotation_name: str = "annotations.csv") -> list[Path]:
    sessions = sorted(
        session_dir
        for session_dir in dataset_root.iterdir()
        if session_dir.is_dir() and (session_dir / "records.jsonl").is_file()
    )
    labeled_sessions = [session_dir for session_dir in sessions if (session_dir / annotation_name).is_file()]
    if not labeled_sessions:
        raise ValueError(
            f"no {annotation_name} files were found under {dataset_root}. "
            "The collected frames do not contain training labels yet."
        )
    return labeled_sessions


def load_calibration_images(
    dataset_root: Path,
    annotation_name: str = "annotations.csv",
    input_size: int = INPUT_SIZE,
) -> list[np.ndarray]:
    if has_coco_dataset(dataset_root):
        return [
            load_image(sample.frame.image_path, input_size=input_size)
            for split_samples in load_coco_dataset_splits(dataset_root).values()
            for sample in split_samples
        ]
    sessions = find_labeled_sessions(dataset_root, annotation_name)
    images: list[np.ndarray] = []
    for session_dir in sessions:
        for frame in read_frame_records(session_dir):
            images.append(load_image(frame.image_path, input_size=input_size))
    return images


def load_image(image_path: Path, input_size: int = INPUT_SIZE) -> np.ndarray:
    if input_size <= 0:
        raise ValueError("input_size must be positive")
    with Image.open(image_path) as image:
        grayscale = image.convert("L")
        resized = grayscale.resize((input_size, input_size), Image.Resampling.BILINEAR)
        array = np.asarray(resized, dtype=np.float32) / 255.0
    return array[..., np.newaxis]


def samples_to_arrays(
    samples: Sequence[LabeledFrame], input_size: int = INPUT_SIZE
) -> tuple[np.ndarray, np.ndarray]:
    if not samples:
        raise ValueError("cannot create arrays from an empty sample set")
    images = np.stack(
        [load_image(sample.frame.image_path, input_size=input_size) for sample in samples]
    )
    targets = np.stack([sample.target for sample in samples]).astype(np.float32)
    return images, targets


def split_by_session(
    samples: Sequence[LabeledFrame],
    validation_fraction: float = 0.2,
    test_fraction: float = 0.2,
    seed: int = 20260927,
) -> dict[str, list[LabeledFrame]]:
    if not 0.0 < validation_fraction < 1.0 or not 0.0 < test_fraction < 1.0:
        raise ValueError("validation_fraction and test_fraction must be between 0 and 1")
    sessions: dict[Path, list[LabeledFrame]] = {}
    for sample in samples:
        sessions.setdefault(sample.frame.session_dir, []).append(sample)
    if len(sessions) < 3:
        raise ValueError(
            "at least three labeled sessions are required for train/validation/test splitting; "
            f"found {len(sessions)}"
        )

    session_names_list = list(sessions)
    generator = np.random.default_rng(seed)
    generator.shuffle(session_names_list)
    test_count = max(1, round(len(session_names_list) * test_fraction))
    validation_count = max(1, round(len(session_names_list) * validation_fraction))
    if test_count + validation_count >= len(session_names_list):
        test_count = 1
        validation_count = 1

    test_sessions = session_names_list[:test_count]
    validation_sessions = session_names_list[test_count : test_count + validation_count]
    train_sessions = session_names_list[test_count + validation_count :]
    return {
        "train": [sample for session in train_sessions for sample in sessions[session]],
        "validation": [sample for session in validation_sessions for sample in sessions[session]],
        "test": [sample for session in test_sessions for sample in sessions[session]],
    }


def session_names(samples: Iterable[LabeledFrame]) -> list[str]:
    return sorted({sample.frame.session_dir.name for sample in samples})
from __future__ import annotations

import csv
import json
import tempfile
import unittest
from pathlib import Path

import numpy as np
from PIL import Image

from model.fish_bbox.dataset import (
    load_coco_dataset_splits,
    load_session_samples,
    samples_to_arrays,
    split_by_session,
)


def create_session(root: Path, name: str, session_id: int) -> list:
    session_dir = root / name
    frames_dir = session_dir / "frames"
    frames_dir.mkdir(parents=True)
    image_path = frames_dir / "frame.png"
    Image.fromarray(np.full((8, 8), 128, dtype=np.uint8)).save(image_path)
    record = {
        "record_type": 2,
        "session_id": session_id,
        "sequence": 1,
        "timestamp_ms": 10,
        "filename": "frames/frame.png",
    }
    (session_dir / "records.jsonl").write_text(json.dumps(record) + "\n", encoding="utf-8")
    with (session_dir / "annotations.csv").open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(
            stream,
            fieldnames=[
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
            ],
        )
        writer.writeheader()
        writer.writerow(
            {
                "session_id": session_id,
                "sequence": 1,
                "timestamp_ms": 10,
                "fish_x": 0.25,
                "fish_y": 0.25,
                "fish_w": 0.5,
                "fish_h": 0.5,
                "fish_visible": 1,
                "event_state": "idle",
                "reviewer": "test",
            }
        )
    return load_session_samples(session_dir)


class DatasetTest(unittest.TestCase):
    def test_loads_grayscale_image_and_normalized_target(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            samples = create_session(Path(temporary_directory), "session", 1)
            images, targets = samples_to_arrays(samples)

        self.assertEqual((1, 256, 256, 1), images.shape)
        self.assertEqual((1, 5), targets.shape)
        self.assertAlmostEqual(128.0 / 255.0, float(images[0, 0, 0, 0]), places=6)
        self.assertEqual([0.25, 0.25, 0.5, 0.5, 1.0], targets[0].tolist())

    def test_split_keeps_sessions_together(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            samples = [
                sample
                for index in range(3)
                for sample in create_session(root, f"session-{index}", index + 1)
            ]
            splits = split_by_session(samples, seed=1)

        split_names = {
            split_name: {sample.frame.session_dir.name for sample in split_samples}
            for split_name, split_samples in splits.items()
        }
        self.assertEqual(3, sum(len(names) for names in split_names.values()))
        self.assertEqual(0, len(split_names["train"] & split_names["validation"]))
        self.assertEqual(0, len(split_names["train"] & split_names["test"]))
        self.assertEqual(0, len(split_names["validation"] & split_names["test"]))

    def test_loads_coco_splits_and_selects_largest_fish(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            for split in ("train", "valid", "test"):
                split_dir = root / split
                split_dir.mkdir(parents=True)
                Image.fromarray(np.full((256, 256), 128, dtype=np.uint8)).save(
                    split_dir / "frame_0000000001_0000000010.jpg"
                )
                payload = {
                    "images": [
                        {
                            "id": 1,
                            "file_name": "frame_0000000001_0000000010.jpg",
                            "width": 256,
                            "height": 256,
                        }
                    ],
                    "annotations": [
                        {"image_id": 1, "category_id": 1, "bbox": [2, 3, 8, 8]},
                        {"image_id": 1, "category_id": 1, "bbox": [48, 52, 32, 24]},
                    ],
                    "categories": [
                        {"id": 0, "name": "goldfish"},
                        {"id": 1, "name": "goldfish"},
                    ],
                }
                (split_dir / "_annotations.coco.json").write_text(
                    json.dumps(payload), encoding="utf-8"
                )

            splits = load_coco_dataset_splits(root)

        self.assertEqual([48 / 256, 52 / 256, 32 / 256, 24 / 256, 1.0], splits["train"][0].target.tolist())


if __name__ == "__main__":
    unittest.main()
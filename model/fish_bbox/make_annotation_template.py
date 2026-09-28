from __future__ import annotations

import argparse
import csv
from pathlib import Path

from .dataset import ANNOTATION_FIELDS, read_frame_records


def write_template(session_dir: Path, overwrite: bool) -> Path:
    annotation_path = session_dir / "annotations.csv"
    if annotation_path.exists() and not overwrite:
        raise FileExistsError(f"annotation file already exists: {annotation_path}")
    frames = read_frame_records(session_dir)
    with annotation_path.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=ANNOTATION_FIELDS)
        writer.writeheader()
        for frame in frames:
            writer.writerow(
                {
                    "session_id": frame.session_id,
                    "sequence": frame.sequence,
                    "timestamp_ms": frame.timestamp_ms,
                    "fish_x": "",
                    "fish_y": "",
                    "fish_w": "",
                    "fish_h": "",
                    "fish_visible": "",
                    "event_state": "",
                    "reviewer": "",
                }
            )
    return annotation_path


def main() -> None:
    parser = argparse.ArgumentParser(description="Create per-session fish bounding-box annotation templates.")
    parser.add_argument("--dataset-root", type=Path, default=Path("dataset"))
    parser.add_argument("--overwrite", action="store_true")
    args = parser.parse_args()

    session_count = 0
    for session_dir in sorted(args.dataset_root.iterdir()):
        if not session_dir.is_dir() or not (session_dir / "records.jsonl").is_file():
            continue
        path = write_template(session_dir, args.overwrite)
        print(path)
        session_count += 1
    if session_count == 0:
        raise SystemExit(f"no dataset sessions were found under {args.dataset_root}")


if __name__ == "__main__":
    main()
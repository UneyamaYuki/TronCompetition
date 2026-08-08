from __future__ import annotations

import argparse
import binascii
import json
import struct
import time
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import BinaryIO, Iterator

MAGIC = 0x43445141
MAGIC_BYTES = struct.pack("<I", MAGIC)
VERSION = 1
HEADER = struct.Struct("<IBBHIIIIII")
FRAME_METADATA = struct.Struct("<HHB3xII")
SESSION_STATISTICS = struct.Struct("<IIIII")
MAX_PAYLOAD_SIZE = 2 * 1024 * 1024

SESSION_START = 1
JPEG_FRAME = 2
FEED_MARKER = 3
ERROR = 4
SESSION_END = 5


@dataclass(frozen=True)
class RecordHeader:
    magic: int
    version: int
    record_type: int
    header_size: int
    session_id: int
    sequence: int
    timestamp_ms: int
    payload_size: int
    payload_crc32: int
    header_crc32: int

    @classmethod
    def unpack(cls, data: bytes) -> RecordHeader:
        return cls(*HEADER.unpack(data))


class RecordParser:
    def __init__(self) -> None:
        self._buffer = bytearray()

    def feed(self, data: bytes) -> Iterator[tuple[RecordHeader, bytes]]:
        self._buffer.extend(data)
        while True:
            magic_offset = self._buffer.find(MAGIC_BYTES)
            if magic_offset < 0:
                del self._buffer[:-3]
                return
            if magic_offset:
                del self._buffer[:magic_offset]
            if len(self._buffer) < HEADER.size:
                return

            header_bytes = bytes(self._buffer[: HEADER.size])
            header = RecordHeader.unpack(header_bytes)
            expected_header_crc = binascii.crc32(header_bytes[:-4]) & 0xFFFFFFFF
            if (
                header.version != VERSION
                or header.header_size != HEADER.size
                or header.payload_size > MAX_PAYLOAD_SIZE
                or header.header_crc32 != expected_header_crc
            ):
                del self._buffer[0]
                continue

            record_size = HEADER.size + header.payload_size
            if len(self._buffer) < record_size:
                return
            payload = bytes(self._buffer[HEADER.size:record_size])
            del self._buffer[:record_size]
            if (binascii.crc32(payload) & 0xFFFFFFFF) != header.payload_crc32:
                continue
            yield header, payload


class SessionWriter:
    def __init__(self, output_root: Path) -> None:
        self._output_root = output_root
        self._session_dirs: dict[int, Path] = {}

    def write(self, header: RecordHeader, payload: bytes) -> None:
        session_dir = self._session_dirs.get(header.session_id)
        if session_dir is None:
            session_dir = self._output_root / f"session_{header.session_id:08x}"
            (session_dir / "frames").mkdir(parents=True, exist_ok=True)
            self._session_dirs[header.session_id] = session_dir

        metadata = asdict(header)
        if header.record_type == JPEG_FRAME:
            if len(payload) < FRAME_METADATA.size:
                metadata["save_error"] = "missing_frame_metadata"
            else:
                width, height, quality, encode_time_us, _reserved = FRAME_METADATA.unpack_from(payload)
                metadata.update(
                    width=width,
                    height=height,
                    quality=quality,
                    encode_time_us=encode_time_us,
                )
                payload = payload[FRAME_METADATA.size :]
            if "save_error" not in metadata and not (
                payload.startswith(b"\xff\xd8") and payload.endswith(b"\xff\xd9")
            ):
                metadata["save_error"] = "invalid_jpeg_markers"
            elif "save_error" not in metadata:
                filename = f"frame_{header.sequence:010d}_{header.timestamp_ms:010d}.jpg"
                (session_dir / "frames" / filename).write_bytes(payload)
                metadata["filename"] = f"frames/{filename}"
        elif header.record_type == SESSION_END and len(payload) == SESSION_STATISTICS.size:
            captured, encoded, transmitted, dropped, markers = SESSION_STATISTICS.unpack(payload)
            metadata["statistics"] = {
                "captured_frames": captured,
                "encoded_frames": encoded,
                "transmitted_frames": transmitted,
                "dropped_frames": dropped,
                "feed_markers": markers,
            }
        elif payload:
            try:
                metadata["payload"] = json.loads(payload.decode("utf-8"))
            except (UnicodeDecodeError, json.JSONDecodeError):
                metadata["payload_hex"] = payload.hex()

        with (session_dir / "records.jsonl").open("a", encoding="utf-8") as stream:
            stream.write(json.dumps(metadata, ensure_ascii=False) + "\n")


def receive(stream: BinaryIO, output_root: Path, chunk_size: int = 64 * 1024) -> None:
    parser = RecordParser()
    writer = SessionWriter(output_root)
    while True:
        chunk = stream.read(chunk_size)
        if not chunk:
            time.sleep(0.01)
            continue
        for header, payload in parser.feed(chunk):
            writer.write(header, payload)


def main() -> None:
    parser = argparse.ArgumentParser(description="EK-RA8P1 feeding dataset receiver")
    parser.add_argument("port", help="CDC virtual COM port, for example COM6")
    parser.add_argument("--output", type=Path, default=Path("dataset"))
    parser.add_argument("--baudrate", type=int, default=12_000_000)
    args = parser.parse_args()

    try:
        import serial
    except ImportError as error:
        raise SystemExit("pyserial is required: python -m pip install -r requirements.txt") from error

    args.output.mkdir(parents=True, exist_ok=True)
    with serial.Serial(args.port, args.baudrate, timeout=1) as stream:
        receive(stream, args.output)


if __name__ == "__main__":
    main()
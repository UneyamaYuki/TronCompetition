from __future__ import annotations

import argparse
import binascii
import json
import logging
from queue import Queue
import struct
import sys
from threading import Thread
import time
from dataclasses import asdict, dataclass
from datetime import datetime
from pathlib import Path
from typing import Callable, Iterable, Iterator, Protocol

MAGIC = 0x43445141
MAGIC_BYTES = struct.pack("<I", MAGIC)
VERSION = 1
HEADER = struct.Struct("<IBBHIIIIII")
FRAME_METADATA = struct.Struct("<HHB3xII")
SESSION_STATISTICS = struct.Struct("<IIIII")
MAX_PAYLOAD_SIZE = 2 * 1024 * 1024
DEFAULT_PORT = "COM5"
TARGET_USB_VID_PID = "VID:PID=1209:DCA1"
STATUS_INTERVAL_SECONDS = 5.0

LOGGER = logging.getLogger(__name__)

SESSION_START = 1
RAW_FRAME = 2
JPEG_FRAME = RAW_FRAME
FEED_MARKER = 3
ERROR = 4
SESSION_END = 5


class ReadableStream(Protocol):
    def read(self, size: int = -1) -> bytes: ...


class SerialPortInfo(Protocol):
    device: str
    description: str
    hwid: str


def _describe_port(port: SerialPortInfo) -> str:
    details = " ".join(part for part in (port.description, port.hwid) if part)
    return f"{port.device} ({details})" if details else port.device


def _is_target_port(port: SerialPortInfo) -> bool:
    return TARGET_USB_VID_PID in port.hwid.upper()


def resolve_port(requested_port: str, available_ports: Iterable[SerialPortInfo]) -> str:
    ports = list(available_ports)
    requested = requested_port.upper()
    requested_info = next((port for port in ports if port.device.upper() == requested), None)
    if requested_info is not None and _is_target_port(requested_info):
        return requested_info.device

    target_ports = [port for port in ports if _is_target_port(port)]
    if len(target_ports) == 1:
        selected_port = target_ports[0].device
        LOGGER.warning(
            "requested %s is not the data-collector CDC; using %s (%s)",
            requested_port,
            selected_port,
            TARGET_USB_VID_PID,
        )
        return selected_port

    available = ", ".join(_describe_port(port) for port in ports) or "none"
    if requested_info is not None:
        raise ValueError(
            f"{requested_port} is not the data-collector CDC ({TARGET_USB_VID_PID}); "
            f"available ports: {available}"
        )
    raise ValueError(
        f"data-collector CDC ({TARGET_USB_VID_PID}) was not found; "
        f"requested {requested_port}; available ports: {available}"
    )


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

    def feed(self, data: bytes | bytearray) -> Iterator[tuple[RecordHeader, bytes]]:
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
            if (binascii.crc32(payload) & 0xFFFFFFFF) != header.payload_crc32:
                del self._buffer[0]
                continue
            del self._buffer[:record_size]
            yield header, payload


class SessionWriter:
    def __init__(
        self,
        output_root: Path,
        clock: Callable[[], datetime] = datetime.now,
        save_raw: bool = False,
    ) -> None:
        self._output_root = output_root
        self._clock = clock
        self._save_raw = save_raw
        self._session_dirs: dict[int, Path] = {}
        self._frames_written = 0

    def write(self, header: RecordHeader, payload: bytes) -> None:
        session_dir = self._session_dirs.get(header.session_id)
        if session_dir is None:
            start_name = self._clock().strftime("%Y%m%d_%H%M%S")
            session_dir = self._unique_session_dir(start_name)
            (session_dir / "frames").mkdir(parents=True, exist_ok=True)
            self._session_dirs[header.session_id] = session_dir
            LOGGER.info("session started: id=%08x directory=%s", header.session_id, session_dir)

        metadata = asdict(header)
        if header.record_type == RAW_FRAME:
            raw_payload = payload
            raw_filename = f"frame_{header.sequence:010d}_{header.timestamp_ms:010d}.bin"
            if self._save_raw:
                raw_dir = session_dir / "raw"
                raw_dir.mkdir(parents=True, exist_ok=True)
                (raw_dir / raw_filename).write_bytes(raw_payload)
                metadata["raw_filename"] = f"raw/{raw_filename}"
                metadata["raw_payload_size"] = len(raw_payload)

            if len(payload) < FRAME_METADATA.size:
                metadata["save_error"] = "missing_frame_metadata"
            else:
                width, height, _quality, encode_time_us, _reserved = FRAME_METADATA.unpack_from(payload)
                metadata.update(
                    width=width,
                    height=height,
                    format="RGB565",
                    quality=85,
                    encode_time_us=encode_time_us,
                )
                payload = payload[FRAME_METADATA.size :]
            expected_size = metadata.get("width", 0) * metadata.get("height", 0) * 2
            if "save_error" not in metadata and len(payload) != expected_size:
                metadata["save_error"] = "invalid_rgb565_size"
            elif "save_error" not in metadata:
                filename = f"frame_{header.sequence:010d}_{header.timestamp_ms:010d}.jpg"
                try:
                    jpeg = rgb565_le_to_jpeg(
                        payload, metadata["width"], metadata["height"], quality=metadata["quality"]
                    )
                except (ImportError, ValueError) as error:
                    metadata["save_error"] = str(error)
                else:
                    (session_dir / "frames" / filename).write_bytes(jpeg)
                    metadata["filename"] = f"frames/{filename}"
                    self._frames_written += 1
                    if self._frames_written == 1 or self._frames_written % 10 == 0:
                        LOGGER.info(
                            "frames saved: count=%d latest=%s",
                            self._frames_written,
                            metadata["filename"],
                        )
            if "save_error" in metadata:
                LOGGER.warning(
                    "frame not saved: session=%08x sequence=%d reason=%s",
                    header.session_id,
                    header.sequence,
                    metadata["save_error"],
                )
        elif header.record_type == SESSION_END and len(payload) == SESSION_STATISTICS.size:
            captured, encoded, transmitted, dropped, markers = SESSION_STATISTICS.unpack(payload)
            metadata["statistics"] = {
                "captured_frames": captured,
                "encoded_frames": encoded,
                "transmitted_frames": transmitted,
                "dropped_frames": dropped,
                "feed_markers": markers,
            }
            LOGGER.info(
                "session ended: id=%08x captured=%d encoded=%d transmitted=%d dropped=%d markers=%d",
                header.session_id,
                captured,
                encoded,
                transmitted,
                dropped,
                markers,
            )
        elif payload:
            try:
                metadata["payload"] = json.loads(payload.decode("utf-8"))
            except (UnicodeDecodeError, json.JSONDecodeError):
                metadata["payload_hex"] = payload.hex()

        with (session_dir / "records.jsonl").open("a", encoding="utf-8") as stream:
            stream.write(json.dumps(metadata, ensure_ascii=False) + "\n")

    def _unique_session_dir(self, start_name: str) -> Path:
        base = self._output_root / start_name
        if not base.exists():
            return base
        for suffix in range(1, 10_000):
            candidate = self._output_root / f"{base.name}_{suffix:03d}"
            if not candidate.exists():
                return candidate
        raise RuntimeError(f"too many sessions with start time {start_name}")


def yuyv422_to_jpeg(payload: bytes, width: int, height: int, quality: int = 85) -> bytes:
    if width <= 0 or height <= 0 or len(payload) != width * height * 2:
        raise ValueError("invalid YUYV422 frame dimensions")
    try:
        from PIL import Image
    except ImportError as error:
        raise ImportError("Pillow is required: python -m pip install -r requirements.txt") from error

    rgb = bytearray(width * height * 3)
    output_index = 0
    for input_index in range(0, len(payload), 4):
        y0, cb, y1, cr = payload[input_index : input_index + 4]
        for luminance in (y0, y1):
            c = luminance - 16
            d = cb - 128
            e = cr - 128
            rgb[output_index] = max(0, min(255, (298 * c + 409 * e + 128) >> 8))
            rgb[output_index + 1] = max(0, min(255, (298 * c - 100 * d - 208 * e + 128) >> 8))
            rgb[output_index + 2] = max(0, min(255, (298 * c + 516 * d + 128) >> 8))
            output_index += 3

    image = Image.frombytes("RGB", (width, height), bytes(rgb))
    from io import BytesIO

    output = BytesIO()
    image.save(output, format="JPEG", quality=quality)
    return output.getvalue()


def rgb565_le_to_jpeg(payload: bytes, width: int, height: int, quality: int = 85) -> bytes:
    if width <= 0 or height <= 0 or len(payload) != width * height * 2:
        raise ValueError("invalid RGB565 frame dimensions")
    try:
        from PIL import Image
    except ImportError as error:
        raise ImportError("Pillow is required: python -m pip install -r requirements.txt") from error

    image = Image.frombytes("RGB", (width, height), payload, "raw", "BGR;16")
    from io import BytesIO

    output = BytesIO()
    image.save(output, format="JPEG", quality=quality)
    return output.getvalue()

def receive(
    stream: ReadableStream,
    output_root: Path,
    chunk_size: int = 64 * 1024,
    save_raw: bool = False,
) -> None:
    parser = RecordParser()
    writer = SessionWriter(output_root, save_raw=save_raw)
    record_queue: Queue[tuple[RecordHeader, bytes] | None] = Queue(maxsize=16)

    def write_records() -> None:
        while True:
            record = record_queue.get()
            try:
                if record is None:
                    return
                writer.write(*record)
            finally:
                record_queue.task_done()

    writer_thread = Thread(target=write_records, name="collector-writer", daemon=True)
    writer_thread.start()
    total_bytes = 0
    total_records = 0
    total_frames = 0
    last_status = time.monotonic()
    try:
        while True:
            chunk = stream.read(chunk_size)
            now = time.monotonic()
            if not chunk:
                if now - last_status >= STATUS_INTERVAL_SECONDS:
                    LOGGER.info(
                        "waiting for USB data: bytes=%d records=%d frames=%d",
                        total_bytes,
                        total_records,
                        total_frames,
                    )
                    last_status = now
                time.sleep(0.01)
                continue
            total_bytes += len(chunk)
            records_in_chunk = 0
            for header, payload in parser.feed(chunk):
                records_in_chunk += 1
                total_records += 1
                if header.record_type == RAW_FRAME:
                    total_frames += 1
                LOGGER.debug(
                    "record received: type=%d sequence=%d payload=%d",
                    header.record_type,
                    header.sequence,
                    len(payload),
                )
                record_queue.put((header, payload))
            if now - last_status >= STATUS_INTERVAL_SECONDS:
                if records_in_chunk == 0:
                    LOGGER.info(
                        "received bytes but no complete valid record yet: bytes=%d records=%d",
                        total_bytes,
                        total_records,
                    )
                else:
                    LOGGER.info(
                        "receiver progress: bytes=%d records=%d frames=%d",
                        total_bytes,
                        total_records,
                        total_frames,
                    )
                last_status = now
    finally:
        record_queue.put(None)
        record_queue.join()
        writer_thread.join()


def main() -> None:
    parser = argparse.ArgumentParser(description="EK-RA8P1 feeding dataset receiver")
    parser.add_argument("port", nargs="?", default=DEFAULT_PORT, help="CDC virtual COM port")
    parser.add_argument("--output", type=Path, default=Path("dataset"))
    parser.add_argument("--baudrate", type=int, default=12_000_000)
    parser.add_argument(
        "--save-raw",
        action="store_true",
        help="save each raw record payload, including its 16-byte frame metadata, as raw/*.bin",
    )
    parser.add_argument(
        "--log-level",
        type=str.upper,
        choices=("DEBUG", "INFO", "WARNING", "ERROR", "CRITICAL"),
        default="INFO",
    )
    args = parser.parse_args()

    logging.basicConfig(
        level=getattr(logging, args.log_level),
        format="%(asctime)s %(levelname)s %(message)s",
        datefmt="%H:%M:%S",
        stream=sys.stdout,
        force=True,
    )
    LOGGER.info(
        "starting receiver: port=%s baudrate=%d output=%s save_raw=%s",
        args.port,
        args.baudrate,
        args.output,
        args.save_raw,
    )

    try:
        import serial
        from serial.tools import list_ports
    except ImportError as error:
        raise SystemExit("pyserial is required: python -m pip install -r requirements.txt") from error

    try:
        selected_port = resolve_port(args.port, list_ports.comports())
    except ValueError as error:
        LOGGER.error("%s", error)
        raise SystemExit(1) from error

    args.output.mkdir(parents=True, exist_ok=True)
    stream = serial.Serial()
    stream.port = selected_port
    stream.baudrate = args.baudrate
    stream.timeout = 1
    stream.dtr = True
    stream.rts = False
    LOGGER.info("opening port: %s", selected_port)
    try:
        stream.open()
    except serial.SerialException as error:
        LOGGER.error("failed to open %s: %s", selected_port, error)
        raise SystemExit(1) from error

    LOGGER.info("opened %s (DTR=%s RTS=%s)", stream.name, stream.dtr, stream.rts)
    try:
        with stream:
            receive(stream, args.output, save_raw=args.save_raw)
    except KeyboardInterrupt:
        LOGGER.info("receiver stopped")


if __name__ == "__main__":
    main()
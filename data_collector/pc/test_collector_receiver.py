import binascii
import json
import struct
import tempfile
import unittest
from io import BytesIO
from datetime import datetime
from types import SimpleNamespace
from pathlib import Path

from PIL import Image

from collector_receiver import (
    FRAME_METADATA,
    HEADER,
    RAW_FRAME,
    MAGIC,
    SESSION_START,
    SESSION_END,
    SESSION_STATISTICS,
    RecordParser,
    SessionWriter,
    rgb565_le_to_jpeg,
    yuyv422_to_jpeg,
    resolve_port,
)


def make_record(record_type: int, sequence: int, payload: bytes) -> bytes:
    fields = [MAGIC, 1, record_type, HEADER.size, 0x1234, sequence, 250, len(payload)]
    payload_crc = binascii.crc32(payload) & 0xFFFFFFFF
    partial = struct.pack("<IBBHIIIII", *fields, payload_crc)
    header_crc = binascii.crc32(partial) & 0xFFFFFFFF
    return partial + struct.pack("<I", header_crc) + payload


class RecordParserTest(unittest.TestCase):
    def test_reassembles_fragmented_record_and_resynchronizes(self) -> None:
        payload = FRAME_METADATA.pack(2, 1, 0, 0, 0) + bytes((128, 128, 128, 128))
        record = make_record(RAW_FRAME, 3, payload)
        parser = RecordParser()

        self.assertEqual([], list(parser.feed(b"noise" + record[:11])))
        self.assertEqual([], list(parser.feed(record[11:31])))
        parsed = list(parser.feed(record[31:]))

        self.assertEqual(1, len(parsed))
        self.assertEqual(3, parsed[0][0].sequence)
        self.assertEqual(payload, parsed[0][1])

    def test_discards_bad_payload_crc(self) -> None:
        payload = FRAME_METADATA.pack(2, 1, 0, 0, 0) + bytes((128, 128, 128, 128))
        record = bytearray(make_record(RAW_FRAME, 1, payload))
        record[-3] ^= 0xFF
        self.assertEqual([], list(RecordParser().feed(record)))

    def test_recovers_record_after_interrupted_payload(self) -> None:
        interrupted = make_record(RAW_FRAME, 1, b"x" * 100)[: HEADER.size + 10]
        valid = make_record(SESSION_START, 2, b'{"width":640}')

        parsed = list(RecordParser().feed(interrupted + valid + b"x" * 90))

        self.assertEqual([(2, b'{"width":640}')], [(header.sequence, payload) for header, payload in parsed])

    def test_converts_raw_frame_to_jpeg_and_writes_metadata(self) -> None:
        raw = bytes((128, 128, 128, 128))
        payload = FRAME_METADATA.pack(2, 1, 0, 0, 0) + raw
        header, parsed_payload = list(RecordParser().feed(make_record(RAW_FRAME, 9, payload)))[0]
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            SessionWriter(root).write(header, parsed_payload)
            session = next(root.iterdir())
            images = list((session / "frames").glob("*.jpg"))
            jpeg = images[0].read_bytes()
            self.assertTrue(jpeg.startswith(b"\xff\xd8"))
            self.assertTrue(jpeg.endswith(b"\xff\xd9"))
            metadata = json.loads((session / "records.jsonl").read_text(encoding="utf-8"))
            self.assertEqual(9, metadata["sequence"])
            self.assertEqual(2, metadata["width"])
            self.assertEqual(1, metadata["height"])
            self.assertEqual(85, metadata["quality"])
            self.assertEqual("RGB565", metadata["format"])
            self.assertEqual(0, metadata["encode_time_us"])

    def test_converts_rgb565_le_red_pixels_without_swapping_bytes(self) -> None:
        red_rgb565 = bytes((0x00, 0xF8)) * 4

        jpeg = rgb565_le_to_jpeg(red_rgb565, 2, 2, quality=100)
        pixel = Image.open(BytesIO(jpeg)).convert("RGB").getpixel((0, 0))

        self.assertGreater(pixel[0], 150)
        self.assertLess(pixel[1], 100)
        self.assertLess(pixel[2], 100)

    def test_converts_yuyv422_red_pixels_without_swapping_luma_and_chroma(self) -> None:
        red_yuyv = bytes((82, 90, 82, 240)) * 2

        jpeg = yuyv422_to_jpeg(red_yuyv, 2, 2, quality=100)
        pixel = Image.open(BytesIO(jpeg)).convert("RGB").getpixel((0, 0))

        self.assertGreater(pixel[0], 150)
        self.assertLess(pixel[1], 100)
        self.assertLess(pixel[2], 100)

    def test_saves_raw_record_payload_without_modifying_it(self) -> None:
        raw = FRAME_METADATA.pack(2, 1, 0, 0, 0) + bytes((82, 90, 82, 240))
        header, parsed_payload = list(RecordParser().feed(make_record(RAW_FRAME, 4, raw)))[0]

        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            SessionWriter(root, save_raw=True).write(header, parsed_payload)
            session = next(root.iterdir())
            raw_file = next((session / "raw").glob("*.bin"))
            metadata = json.loads((session / "records.jsonl").read_text(encoding="utf-8"))

            self.assertEqual(raw, raw_file.read_bytes())
            self.assertEqual(f"raw/{raw_file.name}", metadata["raw_filename"])
            self.assertEqual(len(raw), metadata["raw_payload_size"])

    def test_writes_session_statistics(self) -> None:
        payload = SESSION_STATISTICS.pack(10, 9, 8, 2, 1)
        header, parsed_payload = list(RecordParser().feed(make_record(SESSION_END, 10, payload)))[0]
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            SessionWriter(root).write(header, parsed_payload)
            metadata = json.loads(
                (next(root.iterdir()) / "records.jsonl").read_text(encoding="utf-8")
            )
            self.assertEqual(10, metadata["statistics"]["captured_frames"])
            self.assertEqual(2, metadata["statistics"]["dropped_frames"])
            self.assertEqual(1, metadata["statistics"]["feed_markers"])

    def test_saves_fragmented_firmware_session_without_overwriting_existing_data(self) -> None:
        raw = bytes((128, 128, 128, 128))
        records = b"".join(
            (
                make_record(SESSION_START, 0, b'{"width":640,"height":480}'),
                make_record(RAW_FRAME, 1, FRAME_METADATA.pack(2, 1, 0, 0, 0) + raw),
                make_record(SESSION_END, 2, SESSION_STATISTICS.pack(1, 1, 1, 0, 0)),
            )
        )

        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            start_time = datetime(2026, 9, 26, 18, 12, 17)
            existing = root / "20260926_181217"
            (existing / "frames").mkdir(parents=True)
            sentinel = existing / "frames" / "frame_0000000001_0000000250.jpg"
            sentinel.write_bytes(b"existing")

            parser = RecordParser()
            writer = SessionWriter(root, clock=lambda: start_time)
            for offset in range(0, len(records), 17):
                for header, payload in parser.feed(records[offset : offset + 17]):
                    writer.write(header, payload)

            session = root / "20260926_181217_001"
            images = list((session / "frames").glob("*.jpg"))
            metadata = [
                json.loads(line)
                for line in (session / "records.jsonl").read_text(encoding="utf-8").splitlines()
            ]
            self.assertEqual(b"existing", sentinel.read_bytes())
            self.assertTrue(images[0].read_bytes().startswith(b"\xff\xd8"))
            self.assertEqual([SESSION_START, RAW_FRAME, SESSION_END], [item["record_type"] for item in metadata])
            self.assertEqual(1, metadata[-1]["statistics"]["transmitted_frames"])

    def test_names_session_directory_by_start_time(self) -> None:
        start_time = datetime(2026, 9, 26, 18, 12, 17)
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            writer = SessionWriter(root, clock=lambda: start_time)

            header, payload = list(RecordParser().feed(make_record(SESSION_START, 0, b"{}")))[0]
            writer.write(header, payload)

            self.assertTrue((root / "20260926_181217" / "records.jsonl").exists())


class PortSelectionTest(unittest.TestCase):
    @staticmethod
    def port(device: str, hwid: str) -> SimpleNamespace:
        return SimpleNamespace(device=device, description="USB シリアル デバイス", hwid=hwid)

    def test_prefers_requested_com5_when_it_is_the_data_collector(self) -> None:
        ports = [self.port("COM5", "USB VID:PID=1209:DCA1"), self.port("COM3", "USB VID:PID=1366:1024")]

        self.assertEqual("COM5", resolve_port("COM5", ports))

    def test_finds_data_collector_when_windows_assigns_another_com_number(self) -> None:
        ports = [self.port("COM3", "USB VID:PID=1366:1024"), self.port("COM7", "USB VID:PID=1209:DCA1")]

        self.assertEqual("COM7", resolve_port("COM5", ports))

    def test_rejects_debug_vcom_when_data_collector_is_absent(self) -> None:
        ports = [self.port("COM3", "USB VID:PID=1366:1024")]

        with self.assertRaisesRegex(ValueError, "1209:DCA1"):
            resolve_port("COM5", ports)


if __name__ == "__main__":
    unittest.main()
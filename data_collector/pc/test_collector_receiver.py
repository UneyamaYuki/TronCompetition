import binascii
import json
import struct
import tempfile
import unittest
from pathlib import Path

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
            session = root / "session_00001234"
            images = list((session / "frames").glob("*.jpg"))
            jpeg = images[0].read_bytes()
            self.assertTrue(jpeg.startswith(b"\xff\xd8"))
            self.assertTrue(jpeg.endswith(b"\xff\xd9"))
            metadata = json.loads((session / "records.jsonl").read_text(encoding="utf-8"))
            self.assertEqual(9, metadata["sequence"])
            self.assertEqual(2, metadata["width"])
            self.assertEqual(1, metadata["height"])
            self.assertEqual(85, metadata["quality"])
            self.assertEqual("YUYV422", metadata["format"])
            self.assertEqual(0, metadata["encode_time_us"])

    def test_writes_session_statistics(self) -> None:
        payload = SESSION_STATISTICS.pack(10, 9, 8, 2, 1)
        header, parsed_payload = list(RecordParser().feed(make_record(SESSION_END, 10, payload)))[0]
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            SessionWriter(root).write(header, parsed_payload)
            metadata = json.loads(
                (root / "session_00001234" / "records.jsonl").read_text(encoding="utf-8")
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
            existing = root / "session_00001234"
            (existing / "frames").mkdir(parents=True)
            sentinel = existing / "frames" / "frame_0000000001_0000000250.jpg"
            sentinel.write_bytes(b"existing")

            parser = RecordParser()
            writer = SessionWriter(root)
            for offset in range(0, len(records), 17):
                for header, payload in parser.feed(records[offset : offset + 17]):
                    writer.write(header, payload)

            session = root / "session_00001234_001"
            images = list((session / "frames").glob("*.jpg"))
            metadata = [
                json.loads(line)
                for line in (session / "records.jsonl").read_text(encoding="utf-8").splitlines()
            ]
            self.assertEqual(b"existing", sentinel.read_bytes())
            self.assertTrue(images[0].read_bytes().startswith(b"\xff\xd8"))
            self.assertEqual([SESSION_START, RAW_FRAME, SESSION_END], [item["record_type"] for item in metadata])
            self.assertEqual(1, metadata[-1]["statistics"]["transmitted_frames"])


if __name__ == "__main__":
    unittest.main()
import binascii
import json
import struct
import tempfile
import unittest
from pathlib import Path

from collector_receiver import (
    FRAME_METADATA,
    HEADER,
    JPEG_FRAME,
    MAGIC,
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
        payload = FRAME_METADATA.pack(640, 480, 85, 1234, 0) + b"\xff\xd8jpeg\xff\xd9"
        record = make_record(JPEG_FRAME, 3, payload)
        parser = RecordParser()

        self.assertEqual([], list(parser.feed(b"noise" + record[:11])))
        self.assertEqual([], list(parser.feed(record[11:31])))
        parsed = list(parser.feed(record[31:]))

        self.assertEqual(1, len(parsed))
        self.assertEqual(3, parsed[0][0].sequence)
        self.assertEqual(payload, parsed[0][1])

    def test_discards_bad_payload_crc(self) -> None:
        payload = FRAME_METADATA.pack(640, 480, 85, 1234, 0) + b"\xff\xd8x\xff\xd9"
        record = bytearray(make_record(JPEG_FRAME, 1, payload))
        record[-3] ^= 0xFF
        self.assertEqual([], list(RecordParser().feed(record)))

    def test_writes_jpeg_and_metadata(self) -> None:
        jpeg = b"\xff\xd8jpeg\xff\xd9"
        payload = FRAME_METADATA.pack(640, 480, 85, 1234, 0) + jpeg
        header, parsed_payload = list(RecordParser().feed(make_record(JPEG_FRAME, 9, payload)))[0]
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            SessionWriter(root).write(header, parsed_payload)
            session = root / "session_00001234"
            images = list((session / "frames").glob("*.jpg"))
            self.assertEqual([jpeg], [image.read_bytes() for image in images])
            metadata = json.loads((session / "records.jsonl").read_text(encoding="utf-8"))
            self.assertEqual(9, metadata["sequence"])
            self.assertEqual(640, metadata["width"])
            self.assertEqual(480, metadata["height"])
            self.assertEqual(85, metadata["quality"])
            self.assertEqual(1234, metadata["encode_time_us"])

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


if __name__ == "__main__":
    unittest.main()
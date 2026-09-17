import json
import struct
import tempfile
import unittest
from pathlib import Path

from native_bmp import validate_bmp


def bmp(width, height, *, offset=54, trailing=b""):
    row = ((width * 24 + 31) // 32) * 4
    pattern = bytes(range(251))
    payload = (pattern * ((row * height + len(pattern) - 1) // len(pattern)))[:row * height]
    size = offset + len(payload) + len(trailing)
    header = bytearray(54)
    header[:2] = b"BM"
    struct.pack_into("<I", header, 2, size)
    struct.pack_into("<I", header, 10, offset)
    struct.pack_into("<IiiHHI", header, 14, 40, width, height, 1, 24, 0)
    return bytes(header) + bytes(offset - 54) + payload + trailing


class NativeBmpTests(unittest.TestCase):
    def write(self, data):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        path = Path(directory.name) / "capture.bmp"
        path.write_bytes(data)
        return path

    def test_logical_size_is_required_without_drawable_diagnostic(self):
        path = self.write(bmp(1280, 720))
        validate_bmp(path, 1280, 720, "test")
        with self.assertRaisesRegex(RuntimeError, "geometry"):
            validate_bmp(path, 2560, 1440, "test")

    def test_reported_capture_size_is_exactly_required(self):
        path = self.write(bmp(2560, 1440))
        evidence = 'native_capture=' + __import__("json").dumps(
            {"path": str(path), "width": 2560, "height": 1440}, separators=(",", ":"))
        validate_bmp(path, 1280, 720, "test", stdout=evidence)
        with self.assertRaisesRegex(RuntimeError, "geometry"):
            logical = self.write(bmp(1280, 720))
            logical_evidence = 'native_capture=' + __import__("json").dumps(
                {"path": str(logical), "width": 2560, "height": 1440}, separators=(",", ":"))
            validate_bmp(logical, 1280, 720, "test", stdout=logical_evidence)

    def test_duplicate_or_conflicting_capture_evidence_is_rejected(self):
        path = self.write(bmp(2560, 1440))
        import json
        record = {"path": str(path), "width": 2560, "height": 1440}
        line = "native_capture=" + json.dumps(record, separators=(",", ":"))
        with self.assertRaisesRegex(RuntimeError, "duplicated"):
            validate_bmp(path, 1280, 720, "test", stdout=line + "\n" + line)
        conflict = {"path": str(path), "width": 1920, "height": 1080}
        with self.assertRaisesRegex(RuntimeError, "duplicated"):
            validate_bmp(path, 1280, 720, "test", stdout=line + "\n" +
                         "native_capture=" + json.dumps(conflict, separators=(",", ":")))

    def test_capture_evidence_for_another_path_does_not_fall_back(self):
        path = self.write(bmp(1280, 720))
        other = self.write(bmp(1280, 720))
        line = "native_capture=" + json.dumps(
            {"path": str(other), "width": 1280, "height": 720}, separators=(",", ":"))
        with self.assertRaisesRegex(RuntimeError, "missing"):
            validate_bmp(path, 1280, 720, "test", stdout=line)

    def test_startup_and_final_capture_sizes_are_matched_independently(self):
        startup = self.write(bmp(1280, 720))
        final = self.write(bmp(2560, 1440))
        import json
        def evidence(path, width, height):
            return "native_capture=" + json.dumps(
                {"path": str(path), "width": width, "height": height}, separators=(",", ":"))
        stdout = evidence(startup, 1280, 720) + "\n" + evidence(final, 2560, 1440)
        validate_bmp(startup, 1280, 720, "test", stdout=stdout)
        validate_bmp(final, 1280, 720, "test", stdout=stdout)

    def test_header_and_payload_bounds_are_strict(self):
        path = self.write(bmp(1280, 720)[:-1])
        with self.assertRaisesRegex(RuntimeError, "geometry"):
            validate_bmp(path, 1280, 720, "test")

    def test_alpha_and_padding_do_not_count_as_rendered_variation(self):
        width, height = 3, 2
        row = ((width * 24 + 31) // 32) * 4
        pixel = b"\x10\x20\x30"
        data = bytearray(54 + row * height)
        data[:2] = b"BM"
        struct.pack_into("<I", data, 2, len(data))
        struct.pack_into("<I", data, 10, 54)
        struct.pack_into("<IiiHHI", data, 14, 40, width, height, 1, 24, 0)
        for start in (54, 54 + row):
            data[start:start + width * 3] = pixel * width
            data[start + width * 3:start + row] = b"\xff" * (row - width * 3)
        with self.assertRaisesRegex(RuntimeError, "variation"):
            validate_bmp(self.write(data), width, height, "test")

    def test_bitfields_require_32_bit_masks_before_pixels(self):
        data = bytearray(bmp(2, 2))
        struct.pack_into("<H", data, 28, 32)
        struct.pack_into("<I", data, 30, 3)
        with self.assertRaisesRegex(RuntimeError, "geometry"):
            validate_bmp(self.write(data), 2, 2, "test")

    def test_duplicate_json_keys_are_rejected(self):
        path = self.write(bmp(1280, 720))
        line = ('native_capture={"path":' + __import__("json").dumps(str(path)) +
                ',"width":1280,"width":1280,"height":720}')
        with self.assertRaisesRegex(RuntimeError, "malformed"):
            validate_bmp(path, 1280, 720, "test", stdout=line)

    def test_malformed_capture_diagnostic_is_rejected(self):
        with self.assertRaisesRegex(RuntimeError, "capture diagnostic"):
            validate_bmp(self.write(bmp(1280, 720)), 1280, 720, "test",
                         stdout="native_capture={bad}")


if __name__ == "__main__":
    unittest.main()

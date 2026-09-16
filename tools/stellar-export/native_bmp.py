"""Strict validation for native renderer BMP captures."""
from __future__ import annotations

import json
import os
import re
import struct
from pathlib import Path


_CAPTURE = re.compile(r"^native_capture=(\{.*\})$")


def _canonical(path: Path) -> str:
    return os.path.normcase(os.path.abspath(os.fspath(path)))


def capture_dimensions(path: Path, stdout: str | None, label: str) -> tuple[int, int] | None:
    """Return the uniquely matching native-reported capture size."""
    if not stdout:
        return None
    records = []
    for line in stdout.splitlines():
        if not line.startswith("native_capture="):
            continue
        match = _CAPTURE.fullmatch(line)
        if not match:
            raise RuntimeError(f"Native {label} capture diagnostic is malformed")
        def unique_object(pairs):
            result = {}
            for key, value in pairs:
                if key in result:
                    raise ValueError("duplicate JSON key")
                result[key] = value
            return result
        try:
            record = json.loads(match.group(1), object_pairs_hook=unique_object)
        except (ValueError, json.JSONDecodeError) as error:
            raise RuntimeError(f"Native {label} capture diagnostic is malformed") from error
        if (not isinstance(record, dict) or set(record) != {"path", "width", "height"} or
                not isinstance(record["path"], str) or
                type(record["width"]) is not int or type(record["height"]) is not int or
                record["width"] <= 0 or record["height"] <= 0):
            raise RuntimeError(f"Native {label} capture diagnostic is invalid")
        records.append(record)
    matches = [record for record in records if _canonical(Path(record["path"])) == _canonical(path)]
    if len(matches) > 1:
        raise RuntimeError(f"Native {label} capture diagnostic is duplicated")
    if not matches:
        if records:
            raise RuntimeError(f"Native {label} capture diagnostic is missing")
        return None
    return matches[0]["width"], matches[0]["height"]


def validate_bmp(path: Path, width: int, height: int, label: str,
                 *, stdout: str | None = None) -> bytes:
    """Validate BMP structure, payload bounds, and an explicit capture size."""
    data = path.read_bytes() if path.is_file() else b""
    if len(data) < 54 or data[:2] != b"BM":
        raise RuntimeError(f"Native {label} did not capture a BMP frame")
    if width <= 0 or height <= 0:
        raise RuntimeError(f"Native {label} expected capture dimensions are invalid")
    try:
        declared = struct.unpack_from("<I", data, 2)[0]
        offset = struct.unpack_from("<I", data, 10)[0]
        dib_size = struct.unpack_from("<I", data, 14)[0]
        actual_width, actual_height, planes, bits = struct.unpack_from("<iiHH", data, 18)
        compression = struct.unpack_from("<I", data, 30)[0]
    except struct.error as error:
        raise RuntimeError(f"Native {label} capture has invalid renderer geometry") from error
    reported = capture_dimensions(path, stdout, label)
    sizes = {reported if reported is not None else (width, height)}
    row_bytes = ((actual_width * bits + 31) // 32) * 4 if actual_width > 0 else 0
    required = row_bytes * abs(actual_height)
    masks = None
    if compression == 3:
        if bits != 32:
            raise RuntimeError(f"Native {label} capture has invalid renderer geometry")
        mask_offset = 14 + 40 if dib_size >= 52 else 54
        if dib_size < 52 and offset < mask_offset + 12:
            raise RuntimeError(f"Native {label} capture has invalid renderer geometry")
        if mask_offset + 12 > len(data):
            raise RuntimeError(f"Native {label} capture has invalid renderer geometry")
        masks = struct.unpack_from("<III", data, mask_offset)
        if (not all(masks) or masks[0] & masks[1] or masks[0] & masks[2] or
                masks[1] & masks[2] or any(mask & ~0xFFFFFFFF for mask in masks)):
            raise RuntimeError(f"Native {label} capture has invalid renderer geometry")
    if (declared != len(data) or dib_size < 40 or offset < 14 + dib_size or
            offset > len(data) or (actual_width, abs(actual_height)) not in sizes or
            actual_width <= 0 or actual_height == 0 or planes != 1 or
            bits not in (24, 32) or compression not in (0, 3) or
            required <= 0 or offset + required > len(data)):
        raise RuntimeError(f"Native {label} capture has invalid renderer geometry")
    # Stop at the first changed RGB value: high-resolution captures must not
    # allocate a set containing millions of distinct colors. Ignore row padding
    # and alpha; bitfield BMPs may place channels outside the first three bytes.
    rgb_mask = masks[0] | masks[1] | masks[2] if masks else 0x00FFFFFF
    def color(at):
        return int.from_bytes(data[at:at + bits // 8], "little") & rgb_mask
    first = color(offset)
    for row in range(abs(actual_height)):
        for column in range(actual_width):
            if color(offset + row * row_bytes + column * (bits // 8)) != first:
                return data[offset:offset + required]
    raise RuntimeError(f"Native {label} capture contains no rendered variation")

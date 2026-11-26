"""PNG helpers shared by skins and instance icons."""

import base64
import re
import struct
import zlib


PNG_SIG = b"\x89PNG\r\n\x1a\n"
MAX_SKIN_BYTES = 200_000


def decode_png_uri(uri: str) -> bytes:
    m = re.fullmatch(r"data:image/png;base64,([A-Za-z0-9+/=\s]+)", uri) if isinstance(uri, str) else None
    if not m:
        raise ValueError("That isn't a PNG image.")
    encoded = "".join(m.group(1).split())
    if len(encoded) > 4 * ((MAX_SKIN_BYTES + 2) // 3):
        raise ValueError("That file is too big to be a skin.")
    try:
        data = base64.b64decode(encoded, validate=True)
    except Exception:
        raise ValueError("That image couldn't be read.")
    skin_png_size(data)
    return data


def skin_png_size(data: bytes) -> tuple[int, int]:
    if not isinstance(data, bytes) or not data.startswith(PNG_SIG + b"\x00\x00\x00\rIHDR") or len(data) < 33:
        raise ValueError("That isn't a PNG image.")
    if len(data) > MAX_SKIN_BYTES:
        raise ValueError("That file is too big to be a skin.")
    width, height = struct.unpack(">II", data[16:24])
    if (width, height) not in ((64, 64), (64, 32)):
        raise ValueError("Skins must be 64x64 (or 64x32) pixels.")
    # Check the container, including CRCs, without expanding compressed pixel data.
    offset, has_pixels = 8, False
    while offset + 12 <= len(data):
        length, kind = struct.unpack_from(">I4s", data, offset)
        end = offset + 8 + length
        if end + 4 > len(data) or zlib.crc32(data[offset + 4:end]) != struct.unpack_from(">I", data, end)[0]:
            break
        if kind == b"IDAT" and length:
            has_pixels = True
        if kind == b"IEND":
            if has_pixels and length == 0 and end + 4 == len(data):
                return width, height
            break
        offset = end + 4
    raise ValueError("That PNG is damaged or incomplete.")


def png_uri(data: bytes) -> str:
    return "data:image/png;base64," + base64.b64encode(data).decode("ascii")

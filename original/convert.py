#!/usr/bin/env python3
"""Convert the original PMD PC-98 text files to UTF-8.

The originals are Shift-JIS (CP932) with PC-98 extensions: JIS X 0208 row 12
box-drawing characters, which CP932 leaves undefined. They also use the JIS X
0208 characters that Windows-31J replaced with compatibility characters. This
script restores both and writes UTF-8 with LF line endings.

Usage:
    convert.py INPUT [OUTPUT]
    convert.py --all ORIGINALS_DIR
"""

import sys
from pathlib import Path

# PC-98 JIS X 0208 row 12 (SJIS 0x86A2..0x86ED), one char per code point.
_ROW12 = (
    "─━│┃─━│┃─━│┃┌┌┌┏┐┐┐┓└└└┗┘┘┘┛"
    "├├├├┠├├┣┤┤┤┤┨┤┤┫┬┬┬┬┯┬┬┳┴┴┴┴┷┴┴┻┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼╋"
)
assert len(_ROW12) == 0x86EE - 0x86A2

_OVERRIDES = {
    0x8150: 0x203E,  # ￣ -> ‾  OVERLINE
    0x8160: 0x301C,  # ～ -> 〜  WAVE DASH
    0x8161: 0x2016,  # ∥ -> ‖  DOUBLE VERTICAL LINE
    0x817C: 0x2212,  # － -> −  MINUS SIGN
    0x8191: 0x00A2,  # ￠ -> ¢  CENT SIGN
    0x8192: 0x00A3,  # ￡ -> £  POUND SIGN
    0x81CA: 0x00AC,  # ￢ -> ¬  NOT SIGN
}
_OVERRIDES.update({0x86A2 + i: ord(ch) for i, ch in enumerate(_ROW12)})


def convert(data: bytes) -> bytes:
    out = []
    i = 0
    while i < len(data):
        b = data[i]
        if b == 0x0D:
            out.append("\n")
            i += 2 if i + 1 < len(data) and data[i + 1] == 0x0A else 1
        elif b == 0x0A:
            out.append("\n")
            i += 1
        elif 0x81 <= b <= 0x9F or 0xE0 <= b <= 0xFC:
            if i + 1 == len(data):
                raise ValueError(f"truncated multibyte sequence at offset {i}")
            pair = (b << 8) | data[i + 1]
            cp = _OVERRIDES.get(pair)
            if cp is None:
                try:
                    cp = ord(data[i:i + 2].decode("cp932"))
                except UnicodeDecodeError:
                    raise ValueError(
                        f"invalid Shift-JIS sequence {pair:04X} at offset {i}"
                    ) from None
            out.append(chr(cp))
            i += 2
        elif b >= 0x80:
            out.append(data[i:i + 1].decode("cp932"))
            i += 1
        else:
            out.append(chr(b))
            i += 1
    return "".join(out).encode("utf-8")


def main(argv):
    if not 1 <= len(argv) <= 2:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    data = sys.stdin.buffer.read() if argv[0] == "-" else Path(argv[0]).read_bytes()
    result = convert(data)
    if len(argv) == 2 and argv[1] != "-":
        Path(argv[1]).write_bytes(result)
    else:
        sys.stdout.buffer.write(result)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

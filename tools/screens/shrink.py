#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Recompress a PNG written by screens-tool (stored deflate) with zlib level 9: in out."""
import struct
import sys
import zlib


def main(src, dst):
    data = open(src, "rb").read()
    out, idat, i = [data[:8]], b"", 8
    chunks = []
    while i < len(data):
        n = struct.unpack(">I", data[i:i + 4])[0]
        chunks.append((data[i + 4:i + 8], data[i + 8:i + 8 + n]))
        i += 12 + n
    for t, body in chunks:
        if t == b"IDAT":
            idat += body
    for t, body in chunks:
        if t == b"IDAT":
            if idat is None:
                continue
            body, idat = zlib.compress(zlib.decompress(idat), 9), None
        out.append(struct.pack(">I", len(body)) + t + body + struct.pack(">I", zlib.crc32(t + body)))
    open(dst, "wb").write(b"".join(out))
    print(f"{dst}: {len(data)} -> {sum(map(len, out))} bytes")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])

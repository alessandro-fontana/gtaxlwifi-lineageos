#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
#
# Generates the boot splash (bootsplash) animation from the LineageOS
# bootanimation.
#
# Why this way: the splash has to run in first stage, inside the ramdisk,
# as a static binary with no dependencies (no libpng, no libz). The
# bootanimation frames are grayscale PNGs with mixed bit depths and
# palettes; here they are decoded once, at build time, and rewritten in a
# format the binary can read with fifteen lines of code: RLE in (count,
# value) pairs over A8.
#
# Measured on the 15/09 build: 150 frames (part0 + part1), 4 500 000 bytes
# expanded, 257 618 bytes as RLE. With zlib it would be 110 952, but that
# would cost one more library in the ramdisk to save 150 KB on a 256 MB p26.
#
#   ./make-bootsplash-anim.py \
#       /aosp/lineage/out/target/product/gtaxlwifi/system/product/media/bootanimation.zip \
#       ../bootsplash/anim.rle
#
# The format (little-endian):
#   magic "GSPL"  u32 version=1
#   u32 width      u32 height     u32 n_frames   u32 loop_start
#   u32 fps        u32 rle_length
#   ...RLE stream: pairs (u8 count 1..255, u8 value) that expand to
#      width*height*n_frames bytes, one frame after the other.

import hashlib
import struct
import sys
import zipfile
import zlib

MAGIC = b"GSPL"
VERSION = 1


def decode_png(d):
    """(w, h, bytes) with one luminance byte per pixel."""
    pos = 8
    idat = b""
    plte = None
    w = h = bd = ct = None
    while pos < len(d):
        ln = struct.unpack(">I", d[pos:pos + 4])[0]
        chunk_type = d[pos + 4:pos + 8]
        data = d[pos + 8:pos + 8 + ln]
        if chunk_type == b"IHDR":
            w, h, bd, ct, _comp, _filt, inter = struct.unpack(">IIBBBBB", data)
            if inter:
                raise SystemExit("interlaced PNG: not supported")
        elif chunk_type == b"PLTE":
            plte = data
        elif chunk_type == b"IDAT":
            idat += data
        elif chunk_type == b"IEND":
            break
        pos += 12 + ln

    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[ct]
    bits_per_pixel = bd * channels
    stride = (w * bits_per_pixel + 7) // 8
    fbpp = max(1, (bits_per_pixel + 7) // 8)

    raw = zlib.decompress(idat)
    out = bytearray(w * h)
    prev = bytearray(stride)
    p = 0
    for y in range(h):
        f = raw[p]
        p += 1
        row = bytearray(raw[p:p + stride])
        p += stride
        if f == 1:
            for x in range(fbpp, stride):
                row[x] = (row[x] + row[x - fbpp]) & 255
        elif f == 2:
            for x in range(stride):
                row[x] = (row[x] + prev[x]) & 255
        elif f == 3:
            for x in range(stride):
                a = row[x - fbpp] if x >= fbpp else 0
                row[x] = (row[x] + ((a + prev[x]) >> 1)) & 255
        elif f == 4:
            for x in range(stride):
                a = row[x - fbpp] if x >= fbpp else 0
                b = prev[x]
                c = prev[x - fbpp] if x >= fbpp else 0
                pp = a + b - c
                pa, pb, pc = abs(pp - a), abs(pp - b), abs(pp - c)
                row[x] = (row[x] + (a if (pa <= pb and pa <= pc)
                                    else (b if pb <= pc else c))) & 255
        elif f != 0:
            raise SystemExit(f"unknown PNG filter {f}")
        out[y * w:(y + 1) * w] = _luminance(row, w, bd, ct, channels, plte)
        prev = row
    return w, h, bytes(out)


def _luminance(row, w, bd, ct, channels, plte):
    out = bytearray(w)
    if bd == 8 and ct == 0:
        out[:] = row[:w]
    elif bd == 1 and ct == 0:
        for x in range(w):
            out[x] = 255 if (row[x >> 3] >> (7 - (x & 7))) & 1 else 0
    elif bd == 8 and ct == 3:
        for x in range(w):
            i = row[x] * 3
            out[x] = (plte[i] * 77 + plte[i + 1] * 151 + plte[i + 2] * 28) >> 8
    elif bd == 1 and ct == 3:
        for x in range(w):
            i = ((row[x >> 3] >> (7 - (x & 7))) & 1) * 3
            out[x] = (plte[i] * 77 + plte[i + 1] * 151 + plte[i + 2] * 28) >> 8
    elif bd == 8 and ct in (2, 6):
        for x in range(w):
            b = x * channels
            out[x] = (row[b] * 77 + row[b + 1] * 151 + row[b + 2] * 28) >> 8
    else:
        raise SystemExit(f"PNG bit depth {bd} color type {ct}: not supported")
    return out


def compress_rle(data):
    out = bytearray()
    i = 0
    n = len(data)
    while i < n:
        v = data[i]
        j = i + 1
        while j < n and data[j] == v and j - i < 255:
            j += 1
        out.append(j - i)
        out.append(v)
        i = j
    return bytes(out)


def main():
    if len(sys.argv) != 3:
        raise SystemExit(f"usage: {sys.argv[0]} <bootanimation.zip> <anim.rle>")
    source, destination = sys.argv[1], sys.argv[2]

    z = zipfile.ZipFile(source)
    desc = z.read("desc.txt").decode().splitlines()
    width, height, fps = (int(x) for x in desc[0].split()[:3])

    # Take the parts up to the first one with count 0, which is the one the
    # bootanimation repeats forever: it is exactly the loop the splash needs
    # while the rest of the system boots. The following parts (the closing
    # on the logo) are played by the real bootanimation.
    parts = []
    loop_start = None
    n_frames = 0
    for line in desc[1:]:
        fields = line.split()
        if not fields or fields[0] not in ("p", "c"):
            continue
        count, _pause, name = int(fields[1]), int(fields[2]), fields[3]
        frames = sorted(n for n in z.namelist()
                        if n.startswith(name + "/") and n.endswith(".png"))
        parts.append((name, count, frames))
        if count == 0:
            loop_start = n_frames
            n_frames += len(frames)
            break
        n_frames += len(frames)

    if loop_start is None:
        raise SystemExit("desc.txt: no looping part (count 0)")

    expanded = bytearray()
    for name, count, frames in parts:
        for f in frames:
            w, h, px = decode_png(z.read(f))
            if (w, h) != (width, height):
                raise SystemExit(f"{f}: {w}x{h} instead of {width}x{height}")
            expanded += px
        print(f"  {name}: {len(frames)} frames, count {count}")

    rle = compress_rle(bytes(expanded))
    header = struct.pack("<4sIIIIIII", MAGIC, VERSION, width, height,
                         n_frames, loop_start, fps, len(rle))
    with open(destination, "wb") as f:
        f.write(header)
        f.write(rle)

    print(f"{width}x{height}, {n_frames} frames at {fps} fps, "
          f"loop from {loop_start}")
    print(f"expanded {len(expanded)} bytes, RLE {len(rle)} bytes "
          f"({100 * len(rle) / len(expanded):.1f} %)")
    print(f"wrote {destination} ({len(header) + len(rle)} bytes)")
    print("sha256 " + hashlib.sha256(open(destination, 'rb').read()).hexdigest())


if __name__ == "__main__":
    main()

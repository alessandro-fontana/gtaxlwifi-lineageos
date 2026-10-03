#!/usr/bin/env python3
#
# SPDX-FileCopyrightText: 2026 The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
#
"""Builds the boot images of gtaxlwifi (SM-T580) inside the Android build.

S-BOOT, Samsung's bootloader, stays: it starts BOOT (p9) or RECOVERY (p10)
as Samsung boot images, ignores the command line in them and refuses
ramdisks over 14 MiB (docs §65.12). So the "kernel" of both images is the
loader (bootloader/loader, docs §73), followed by its payload: a header, the
real kernel compressed with LZ4, the DTB, the ramdisk and the command line.
The loader puts them in place, completes the DTB and starts Linux.

Used by mkbootimg.mk (BOARD_CUSTOM_BOOTIMG_MK); needs only Python and the
host lz4 of the tree, so that LineageOS's build servers make the same images.
"""

import argparse
import hashlib
import os
import struct
import subprocess
import sys
import tempfile

PAGE = 2048
# Samsung/osm0sis mkbootimg defaults, as in every image S-BOOT took so far
BASE = 0x10000000
KERNEL_OFFSET = 0x00008000
RAMDISK_OFFSET = 0x01000000
SECOND_OFFSET = 0x00f00000
TAGS_OFFSET = 0x00000100
SEANDROID = b"SEANDROIDENFORCE"
# from S-BOOT's code (docs §65.12): the "kernel" field, header and padding
KERNEL_FIELD_MAX = 0x3d7ff00
RECOVERY_PARTITION = 77824 * 512    # RECOVERY, p10: 38 MiB


def pad(data, size=PAGE):
    return data + b"\0" * ((size - len(data) % size) % size)


def samsung_bootimg(kernel, dt):
    """A boot image as S-BOOT wants it: header v0 with dt_size, no ramdisk."""
    sha = hashlib.sha1()
    for blob in (kernel, b"", b"", dt):   # kernel, ramdisk, second, dt
        sha.update(blob)
        sha.update(struct.pack("<I", len(blob)))
    header = struct.pack(
        "<8s10I16s512s32s1024s",
        b"ANDROID!",
        len(kernel), BASE + KERNEL_OFFSET,
        0, BASE + RAMDISK_OFFSET,
        0, BASE + SECOND_OFFSET,
        BASE + TAGS_OFFSET, PAGE, len(dt), 0,
        b"", b"", sha.digest(), b"")
    return pad(header) + pad(kernel) + pad(dt) + SEANDROID


BOOT_PARTITION = 65536 * 512        # BOOT, p9: 32 MiB
PAYLOAD_OFFSET = 0x100000           # bootloader/loader/loader.h
PAYLOAD_RECOVERY = 1


def cmd_loader(args):
    """The loader (bootloader/loader) as the "kernel" of a Samsung boot
    image, followed by its payload: header, LZ4 kernel, DTB, ramdisk and
    command line. For BOOT (Android) or RECOVERY (--recovery)."""
    with open(args.loader, "rb") as f:
        loader = f.read()
    if len(loader) > PAYLOAD_OFFSET:
        sys.exit("loader over PAYLOAD_OFFSET")
    with tempfile.TemporaryDirectory() as tmp:
        lz = os.path.join(tmp, "Image.lz4")
        subprocess.run([args.lz4, "-l", "-12", "--favor-decSpeed", "-f", "-q",
                        args.kernel, lz], check=True)
        with open(lz, "rb") as f:
            kernel_lz4 = f.read()
    with open(args.kernel, "rb") as f:
        kernel_raw_size = len(f.read())
    pieces = []
    for path in (args.dtb, args.ramdisk, args.cmdline):
        with open(path, "rb") as f:
            pieces.append(f.read())
    dtb, ramdisk, cmdline = pieces
    header_size = 64
    offsets, blob = [], b""
    for data in (kernel_lz4, dtb, ramdisk, cmdline):
        blob += b"\0" * ((-(header_size + len(blob))) % 4096)   # page aligned
        offsets.append(header_size + len(blob))
        blob += data
    flags = PAYLOAD_RECOVERY if args.recovery else 0
    header = struct.pack("<8s10I", b"GXPAYLD1", flags,
                         offsets[0], len(kernel_lz4), kernel_raw_size,
                         offsets[1], len(dtb), offsets[2], len(ramdisk),
                         offsets[3], len(cmdline))
    header += b"\0" * (header_size - len(header))
    kernel_field = loader + b"\0" * (PAYLOAD_OFFSET - len(loader)) + header + blob
    if len(kernel_field) > KERNEL_FIELD_MAX:
        sys.exit("kernel field over S-BOOT's limit")
    with open(args.dt, "rb") as f:
        dt = f.read()
    img = samsung_bootimg(kernel_field, dt)
    limit = RECOVERY_PARTITION if args.recovery else BOOT_PARTITION
    if len(img) > limit:
        sys.exit("%d bytes, the partition holds %d" % (len(img), limit))
    with open(args.output, "wb") as f:
        f.write(img)


def main():
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = p.add_subparsers(dest="cmd", required=True)
    ld = sub.add_parser("loader")
    ld.add_argument("--loader", required=True)
    ld.add_argument("--kernel", required=True)
    ld.add_argument("--dtb", required=True)
    ld.add_argument("--ramdisk", required=True)
    ld.add_argument("--cmdline", required=True)
    ld.add_argument("--dt", required=True)
    ld.add_argument("--lz4", required=True)
    ld.add_argument("--recovery", action="store_true")
    ld.add_argument("--output", required=True)
    args = p.parse_args()
    {"loader": cmd_loader}[args.cmd](args)


if __name__ == "__main__":
    main()

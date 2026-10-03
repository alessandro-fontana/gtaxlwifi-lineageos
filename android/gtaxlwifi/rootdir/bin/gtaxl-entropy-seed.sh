#!/vendor/bin/sh
#
# SPDX-FileCopyrightText: 2026 The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
#
# Refreshes the entropy seed that the loader will pass to the kernel on the
# NEXT boot (§43.6).
#
# Why it exists. Without a seed, every boot lost 1.27 s: generic_init's
# start blocks until the CRNG is initialised, and this SoC has no hardware
# generator. The seed fixes that, but add_bootloader_randomness() **credits**
# it as real entropy: crediting something the loader makes up from a counter
# and a bit of jitter would be lying to the kernel. This script is the half
# that makes the credit honest: thirty-two bytes taken from /dev/urandom
# **now**, on a booted system, when the pool really is mature, kept for the
# next boot. It is the same thing distributions do with their random-seed.
#
# Where. First sector of the GPT partition "OTA" (p11): eight megabytes,
# zeroed and used by nobody (§32.2). Raw, no filesystem, so the loader reads
# it with a single sector read and Android does not have to mount anything.
#
#   byte 0..7    "GXSEED01"
#   byte 8..39   thirty-two bytes from /dev/urandom
#
# The write order is NOT accidental: first the seed, then the signature. If
# the first write fails, the signature never lands, and the loader does not
# use an old seed believing it new. Without a signature (a fresh install)
# the loader carries on with local sources only.

NODE=/dev/block/by-name/OTA

if ! dd if=/dev/urandom of=$NODE bs=8 seek=1 count=4 conv=notrunc 2>/dev/null; then
    echo "gtaxl-entropy-seed: writing the seed failed" > /dev/kmsg
    exit 1
fi
sync

if ! echo -n 'GXSEED01' | dd of=$NODE bs=8 count=1 conv=notrunc 2>/dev/null; then
    echo "gtaxl-entropy-seed: writing the signature failed" > /dev/kmsg
    exit 1
fi
sync

echo "gtaxl-entropy-seed: seed refreshed for the next boot" > /dev/kmsg

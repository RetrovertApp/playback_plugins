#!/usr/bin/env python3
"""Generate the self-authored playback fixture retrovert_selftest.ptm.

A 4-channel Protracker4 module (the IFF FORM MODL wrapper around a
ProTracker M.K. module) built from scratch: one looped triangle-wave
sample and two 64-row patterns of an arpeggiated figure, ~15 s at
speed 6, tempo 125. Deterministic output: the committed fixture and its
sha256 in harness.toml must match what this script emits.
"""

import struct
from pathlib import Path

OUT = Path(__file__).parent / "retrovert_selftest.ptm"

CHANNELS = 4
ROWS = 64
PATTERNS = 2
SAMPLE_BYTES = 64  # one triangle cycle, 32 words

# Amiga periods: C-1, E-1, G-1 and C-2 in ProTracker's naming.
C2, E2, G2, C3 = 856, 678, 570, 428


def triangle():
    # Signed 8-bit triangle, full scale, one cycle over SAMPLE_BYTES.
    n = SAMPLE_BYTES
    q = n // 4
    out = bytearray()
    for i in range(n):
        if i < q:
            v = i * 127 // q
        elif i < 3 * q:
            v = 127 - (i - q) * 254 // (2 * q)
        else:
            v = -127 + (i - 3 * q) * 127 // q
        out.append(v & 0xFF)
    return bytes(out)


def cell(sample, period, effect=0, param=0):
    return struct.pack(
        ">I",
        ((sample & 0xF0) << 24) | (period << 16) | ((sample & 0x0F) << 12) | (effect << 8) | param,
    )


def pattern(figure):
    rows = bytearray()
    for row in range(ROWS):
        for ch in range(CHANNELS):
            # One note every 8 rows per channel, staggered by channel.
            if row % 8 == ch * 2:
                rows += cell(1, figure[(row // 8 + ch) % len(figure)])
            else:
                rows += cell(0, 0)
    return bytes(rows)


def header():
    out = bytearray()
    out += b"Test Song".ljust(20, b"\0")
    # Sample 1; the other 30 slots are empty. Length and loop are in words.
    out += b"triangle".ljust(22, b"\0")
    out += struct.pack(">HBBHH", SAMPLE_BYTES // 2, 0, 64, 0, SAMPLE_BYTES // 2)
    out += (b"\0" * 22 + struct.pack(">HBBHH", 0, 0, 0, 0, 1)) * 30
    out += bytes([PATTERNS, 127])
    out += bytes(range(PATTERNS)) + bytes(128 - PATTERNS)
    out += b"M.K."
    return bytes(out)


def chunk(tag, payload):
    # IFF-style chunk whose size field counts its own 8-byte header.
    return tag + struct.pack(">I", 8 + len(payload)) + payload


def info():
    payload = b"Test Song".ljust(32, b"\0")
    payload += bytes(8)
    payload += struct.pack(">HBB", 125, 1, 0)  # start tempo, flags (bit 0: Fxx >= 0x20 is a tempo), pad
    return chunk(b"INFO", payload)


def build():
    module = header() + pattern([C2, E2, G2, C3]) + pattern([C3, G2, E2, C2]) + triangle()
    chunks = chunk(b"VERS", b"PT4\0") + info() + chunk(b"PTDT", module)
    return b"FORM" + struct.pack(">I", 4 + len(chunks)) + b"MODL" + chunks


if __name__ == "__main__":
    OUT.write_bytes(build())
    print(f"wrote {OUT} ({OUT.stat().st_size} bytes)")

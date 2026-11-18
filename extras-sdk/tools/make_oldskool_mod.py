#!/usr/bin/env python3
"""Writes examples/oldskool.mod: a short original 4-channel chiptune for the
example extra. Every sample is synthesised here, so the module is free to use.

Channels: 0 kick/snare, 1 bass, 2 arpeggio/lead, 3 hi-hat.
Chords Am - F - C - G, one per 16 rows, speed 6 at 125 BPM.
"""
import math
import os
import random
import struct

random.seed(1987)
AMIGA_PAL_CLOCK = 3546895
RATE_C3 = AMIGA_PAL_CLOCK / 214  # sample rate when played at C-3

PERIODS = [856, 808, 762, 720, 678, 640, 604, 570, 538, 508, 480, 453,
           428, 404, 381, 360, 339, 320, 302, 285, 269, 254, 240, 226,
           214, 202, 190, 180, 170, 160, 151, 143, 135, 127, 120, 113]
NAMES = ["C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-"]


def period(note):  # "A-1" .. "B-3"
    return PERIODS[(int(note[2]) - 1) * 12 + NAMES.index(note[:2])]


def to8(values):
    return bytes((max(-128, min(127, int(round(v)))) & 255) for v in values)


def even(data):
    return data if len(data) % 2 == 0 else data + b"\0"


# ---------------------------------------------------------------- samples
def kick():
    n = int(RATE_C3 * 0.22)
    out, phase = [], 0.0
    for i in range(n):
        t = i / RATE_C3
        f = 45 + 140 * math.exp(-t * 28)
        phase += 2 * math.pi * f / RATE_C3
        out.append(127 * math.sin(phase) * math.exp(-t * 9))
    return even(to8(out))


def snare():
    n = int(RATE_C3 * 0.18)
    out = []
    for i in range(n):
        t = i / RATE_C3
        tone = math.sin(2 * math.pi * 190 * t) * math.exp(-t * 30)
        noise = (random.random() * 2 - 1) * math.exp(-t * 16)
        out.append(110 * (0.45 * tone + 0.75 * noise))
    return even(to8(out))


def hihat():
    n = int(RATE_C3 * 0.05)
    out, prev = [], 0.0
    for i in range(n):
        x = random.random() * 2 - 1
        hp = x - prev  # crude high-pass
        prev = x
        out.append(70 * hp * math.exp(-i / RATE_C3 * 70))
    return even(to8(out))


def square(length=32, duty=0.5):
    return to8(100 if i < length * duty else -100 for i in range(length))


def saw(length=32):
    return to8(-110 + 220 * i / (length - 1) for i in range(length))


SAMPLES = [  # name, data, volume, loop(start, length) in bytes or None
    ("kick", kick(), 64, None),
    ("snare", snare(), 56, None),
    ("hihat", hihat(), 40, None),
    ("bass sq", square(32, 0.5), 52, (0, 32)),
    ("lead saw", saw(32), 40, (0, 32)),
    ("pulse 25", square(32, 0.25), 38, (0, 32)),
]

# ---------------------------------------------------------------- patterns
EMPTY = (0, 0, 0, 0)


def cell(note=None, sample=0, fx=0, param=0):
    return (period(note) if note else 0, sample, fx, param)


CHORDS = [("A-", "minor"), ("F-", "major"), ("C-", "major"), ("G-", "major")]
ARP = {"minor": 0x37, "major": 0x47}
MELODY = ["E-3", None, "C-3", None, "D-3", "E-3", None, "G-3",
          "A-3", None, "G-3", "E-3", None, "D-3", "C-3", None]


def build_pattern(with_arp, with_melody):
    rows = [[EMPTY] * 4 for _ in range(64)]
    for r in range(64):
        bar, step = divmod(r, 16)
        root, kind = CHORDS[bar]
        # drums
        if step in (0, 6, 10):
            rows[r][0] = cell("C-3", 1)
        elif step in (4, 12):
            rows[r][0] = cell("C-3", 2)
        if step % 2 == 0 and step not in (4, 12):
            rows[r][3] = cell("C-3", 3, 0xC, 28 if step % 4 else 40)
        # bass: root on 8ths, octave jump on the off beats
        if step % 2 == 0:
            octave = "1" if step % 4 == 0 else "2"
            rows[r][1] = cell(root + octave, 4)
        # arpeggio or melody
        if with_melody:
            note = MELODY[step]
            if note:
                rows[r][2] = cell(note, 5, 0x4, 0x34 if step % 4 else 0)
        elif with_arp and step % 4 == 0:
            rows[r][2] = cell(root + "3", 6, 0x0, ARP[kind])
        elif with_arp:
            rows[r][2] = (0, 0, 0x0, ARP[kind])  # keep the arpeggio running
    return rows


PATTERNS = [
    build_pattern(False, False),  # 0: drums + bass
    build_pattern(True, False),   # 1: + arpeggio
    build_pattern(False, True),   # 2: melody
]
ORDER = [0, 1, 1, 2, 1, 2]


def pack_cell(c):
    p, s, fx, param = c
    return bytes([(s & 0xF0) | ((p >> 8) & 0x0F), p & 0xFF, ((s & 0x0F) << 4) | (fx & 0x0F), param])


def write_mod(path):
    out = bytearray(b"oldskool chip".ljust(20, b"\0"))
    for i in range(31):
        if i < len(SAMPLES):
            name, data, vol, loop = SAMPLES[i]
            ls, ll = (loop if loop else (0, 2))
            out += struct.pack(">22sHBBHH", name.encode().ljust(22, b"\0"), len(data) // 2, 0, vol,
                               ls // 2, ll // 2)
        else:
            out += struct.pack(">22sHBBHH", b"", 0, 0, 0, 0, 1)
    out += bytes([len(ORDER), 127]) + bytes(ORDER + [0] * (128 - len(ORDER))) + b"M.K."
    for pat in PATTERNS:
        for row in pat:
            for c in row:
                out += pack_cell(c)
    for _, data, _, _ in SAMPLES:
        out += data
    with open(path, "wb") as f:
        f.write(out)
    print(f"{path}: {len(out)} bytes, {len(PATTERNS)} patterns, {len(ORDER)} positions")


if __name__ == "__main__":
    here = os.path.dirname(os.path.abspath(__file__))
    write_mod(os.path.join(here, "..", "extras", "oldskool.mod"))

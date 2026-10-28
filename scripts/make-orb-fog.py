#!/usr/bin/env python3
"""The Orb theme's fog (modules/arcade/views/orb/fog.png), made here.

The Arcade screen's Orb look (OrbLook.qml) lays three layers of fog behind
its panels, each this picture drifting slowly (orb/fog.frag): three noises
in its red, green and blue, each the sum of a few octaves of value noise
(fbm) that tiles, so that the picture repeats without a seam. Drawn once
here rather than worked out for every pixel of every frame, which costs a
Raspberry Pi's GPU more than the rest of the screen. Run it again after
changing the numbers below; it needs Python 3 alone, and writes the file in
place.

    python3 scripts/make-orb-fog.py
"""
import os
import random
import struct
import zlib

SIZE = 256
# Cells across the picture of the first octave, doubled at each next one.
CELLS = 4
OCTAVES = 5
HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "modules", "arcade", "views", "orb", "fog.png")


def lattice(cells, rng):
    return [[rng.random() for _ in range(cells)] for _ in range(cells)]


def smooth(t):
    return t * t * (3 - 2 * t)


def fbm(seed):
    """One tiling noise, SIZE by SIZE, its values 0 to 1."""
    rng = random.Random(seed)
    octaves = [(CELLS << o, 0.5 ** o, lattice(CELLS << o, rng)) for o in range(OCTAVES)]
    out = []
    for y in range(SIZE):
        row = []
        for x in range(SIZE):
            value = 0.0
            for cells, weight, grid in octaves:
                fx, fy = x * cells / SIZE, y * cells / SIZE
                ix, iy = int(fx), int(fy)
                tx, ty = smooth(fx - ix), smooth(fy - iy)
                # Round the picture's edge to its other side: the lattice
                # tiles, and the noise with it.
                x0, x1, y0, y1 = ix % cells, (ix + 1) % cells, iy % cells, (iy + 1) % cells
                top = grid[y0][x0] + (grid[y0][x1] - grid[y0][x0]) * tx
                bottom = grid[y1][x0] + (grid[y1][x1] - grid[y1][x0]) * tx
                value += weight * (top + (bottom - top) * ty)
            row.append(value)
        out.append(row)
    # Over the octaves' weights: mostly round the middle, the few highs and
    # lows the fog's thick and clear parts (fog.frag's threshold).
    total = sum(weight for _, weight, _ in octaves)
    return [[v / total for v in r] for r in out]


def png(path, channels):
    raw = bytearray()
    for y in range(SIZE):
        raw.append(0)
        for x in range(SIZE):
            raw.extend(round(255 * c[y][x]) for c in channels)

    def chunk(kind, data):
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    header = struct.pack(">IIBBBBB", SIZE, SIZE, 8, 2, 0, 0, 0)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(bytes(raw), 9))
                + chunk(b"IEND", b""))


if __name__ == "__main__":
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    png(OUT, [fbm(1), fbm(2), fbm(3)])
    print(os.path.normpath(OUT))

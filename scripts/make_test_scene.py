"""Draws tests/overlay/test_scene.png: original art for the overlay test host (a stand-in for a game frame).
Run: python scripts/make_test_scene.py. Licence: GPL-2.0-or-later like the rest of the launcher."""
import math, os, struct, zlib

W, H = 960, 540

def lerp(a, b, t): return a + (b - a) * t

def px(x, y):
    t = y / H
    # dusk sky
    r, g, b = lerp(30, 250, t ** 1.6), lerp(40, 150, t ** 1.4), lerp(110, 120, t)
    # sun
    d = math.hypot(x - 700, y - 330)
    if d < 70: r, g, b = 255, 226, 140
    elif d < 120:
        k = (120 - d) / 50; r, g, b = lerp(r, 255, k * .5), lerp(g, 200, k * .5), lerp(b, 120, k * .5)
    # far hills
    hill = 360 + 30 * math.sin(x / 90.0) + 18 * math.sin(x / 37.0 + 1)
    if y > hill: r, g, b = 56, 44, 92
    # city blocks
    for i in range(24):
        bx = 20 + i * 40; bh = 60 + ((i * 53) % 7) * 22; bw = 30 + (i * 17) % 10
        if bx <= x < bx + bw and y > 440 - bh:
            r, g, b = 22, 24, 48
            if (x - bx) % 8 in (3, 4) and (y % 12) in (3, 4, 5) and (i + y // 12) % 3: r, g, b = 255, 214, 96
    # ground
    if y > 440:
        r, g, b = 18, 22, 44
        if (y - 440) % 24 < 2 or (x + (y - 440) * 2) % 64 < 2: r, g, b = 40, 70, 120
    return int(r), int(g), int(b)

raw = bytearray()
for y in range(H):
    raw.append(0)
    for x in range(W):
        raw += bytes(px(x, y)) + b"\xff"

def chunk(t, d): return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 6, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b"")
open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tests", "overlay", "test_scene.png"), "wb").write(png)

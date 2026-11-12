"""Draws the two original-Xbox shoulder buttons (Black, White) that the Kenney set does not have.
Original art for this project (GPL-2.0-or-later). Run: python scripts/make_glyphs.py  -> assets/glyphs/ours_xbox_{black,white}.png
Plain round buttons in the same 64x64 style as Kenney's Xbox buttons; the label ("Black"/"White") is drawn by the launcher."""
import math, struct, zlib, os

N, SS = 64, 4

def disc(cx, cy, r, x, y):  # coverage of the pixel by a disc, via supersampling
    n = 0
    for j in range(SS):
        for i in range(SS):
            px, py = x + (i + .5) / SS, y + (j + .5) / SS
            if math.hypot(px - cx, py - cy) <= r: n += 1
    return n / (SS * SS)

def render(fill, rim, mark):
    out = bytearray()
    for y in range(N):
        out.append(0)
        for x in range(N):
            c = (0, 0, 0, 0.0)
            layers = [(31.0, rim), (27.0, fill)]
            # rim disc, fill disc, then a ring mark (outer 17, inner 13)
            a_rim = disc(32, 32, 31, x, y); a_fill = disc(32, 32, 27, x, y)
            a_mark = max(0.0, disc(32, 32, 17, x, y) - disc(32, 32, 13, x, y))
            px = [0.0, 0.0, 0.0]; a = 0.0
            for cov, col in ((a_rim, rim), (a_fill, fill), (a_mark, mark)):
                na = cov + a * (1 - cov)
                if na > 0:
                    px = [(col[k] * cov + px[k] * a * (1 - cov)) / na for k in range(3)]
                a = na
            out += bytes([round(px[0]), round(px[1]), round(px[2]), round(a * 255)])
    return bytes(out)

def png(path, raw):
    def chunk(t, d): return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    data = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", N, N, 8, 6, 0, 0, 0)) + \
        chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    open(path, "wb").write(data)

here = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "assets", "glyphs")
WHITE, DARK = (255, 255, 255), (34, 36, 44)
png(os.path.join(here, "ours_xbox_black.png"), render(DARK, WHITE, WHITE))
png(os.path.join(here, "ours_xbox_white.png"), render(WHITE, DARK, DARK))

"""WinFace brand art, all from one drawing: the minimal Face ID-style glyph (rounded corner brackets + a simple face)
in white on a dark rounded square.

  assets/tile.bmp          192x192 32-bit BMP, premultiplied alpha - the lock screen's "Sign-in options" icon
  app/WinFace/winface.ico  app icon (16..256 px)
  app/WinFace/winface.png  256 px, used inside the app

Run: .venv\\Scripts\\python bench\\make_tile.py   (needs Pillow). assets/tile.svg is the same design as a vector.
"""
import struct
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]


def draw(size=1024):
    S = size
    k = S / 1024
    im = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle([40 * k, 40 * k, S - 40 * k, S - 40 * k], radius=220 * k, fill=(24, 24, 28, 255))

    def dot(p, w):
        d.ellipse([p[0] - w / 2, p[1] - w / 2, p[0] + w / 2, p[1] + w / 2], fill="white")

    c, w, h, r, l = S / 2, 58 * k, 300 * k, 95 * k, 70 * k
    for sx in (-1, 1):
        for sy in (-1, 1):
            ax, ay = c + sx * (h - r), c + sy * (h - r)
            R = r + w / 2   # PIL strokes arcs inward
            start = {(-1, -1): 180, (1, -1): 270, (1, 1): 0, (-1, 1): 90}[(sx, sy)]
            d.arc([ax - R, ay - R, ax + R, ay + R], start, start + 90, fill="white", width=round(w))
            d.line([ax - sx * l, c + sy * h, ax, c + sy * h], fill="white", width=round(w))
            d.line([c + sx * h, ay, c + sx * h, ay - sy * l], fill="white", width=round(w))
            dot((ax - sx * l, c + sy * h), w)
            dot((c + sx * h, ay - sy * l), w)
    ew = 50 * k
    for ex in (c - 105 * k, c + 105 * k):   # eyes
        d.line([ex, c - 110 * k, ex, c - 45 * k], fill="white", width=round(ew))
        dot((ex, c - 110 * k), ew)
        dot((ex, c - 45 * k), ew)
    nose = [(c + 8 * k, c - 110 * k), (c + 8 * k, c + 40 * k), (c - 30 * k, c + 40 * k)]
    d.line(nose, fill="white", width=round(ew), joint="curve")
    for p in nose:
        dot(p, ew)
    d.arc([c - 110 * k - ew / 2, c - 10 * k - ew / 2, c + 110 * k + ew / 2, c + 150 * k + ew / 2], 35, 145,
          fill="white", width=round(ew))   # smile
    return im


def save_bmp32(im, path):
    """Bottom-up 32-bit BI_RGB BMP with premultiplied alpha (what LoadImage + LogonUI expect for a tile)."""
    w, h = im.size
    rows = []
    for y in range(h - 1, -1, -1):
        row = bytearray()
        for x in range(w):
            r, g, b, a = im.getpixel((x, y))
            row += bytes((b * a // 255, g * a // 255, r * a // 255, a))
        rows.append(bytes(row))
    pixels = b"".join(rows)
    header = struct.pack("<2sIHHI", b"BM", 14 + 40 + len(pixels), 0, 0, 14 + 40)
    info = struct.pack("<IiiHHIIiiII", 40, w, h, 1, 32, 0, len(pixels), 3780, 3780, 0, 0)
    path.write_bytes(header + info + pixels)


big = draw(1024)
save_bmp32(big.resize((192, 192), Image.LANCZOS), ROOT / "assets" / "tile.bmp")
icon = big.resize((256, 256), Image.LANCZOS)
icon.save(ROOT / "app" / "WinFace" / "winface.png")
icon.save(ROOT / "app" / "WinFace" / "winface.ico", sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
print("tile.bmp, winface.ico, winface.png written")

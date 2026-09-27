"""Render assets/tile.svg's smiling face -> assets/tile.bmp (24-bit, what LogonUI needs) with Pillow.
Drawn 8x larger and downsampled for smooth edges; keeps the same shapes/colours as tile.svg."""
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter

ROOT = Path(__file__).resolve().parents[1]
S, K = 192, 8
W = S * K
img = Image.new("RGB", (W, W), (16, 20, 28))


def radial(size, center, radius, stops):
    """Radial gradient disc as an RGBA image."""
    g = Image.new("RGBA", (size, size))
    px = g.load()
    cx, cy = center
    for y in range(size):
        for x in range(size):
            t = min(1.0, ((x - cx) ** 2 + (y - cy) ** 2) ** 0.5 / radius)
            for (t0, c0), (t1, c1) in zip(stops, stops[1:]):
                if t0 <= t <= t1:
                    f = (t - t0) / (t1 - t0)
                    px[x, y] = tuple(int(a + (b - a) * f) for a, b in zip(c0, c1))
                    break
    return g


# face disc with the svg's gradient (computed small, scaled up - it is smooth anyway)
small = radial(192, (0.42 * 192, 0.36 * 192), 0.70 * 192,
               [(0, (255, 230, 128, 255)), (0.55, (255, 201, 60, 255)), (1, (245, 163, 0, 255))]).resize((W, W), Image.BICUBIC)
mask = Image.new("L", (W, W), 0)
ImageDraw.Draw(mask).ellipse([(96 - 72) * K, (96 - 72) * K, (96 + 72) * K, (96 + 72) * K], fill=255)
img.paste(small, (0, 0), mask)

over = Image.new("RGBA", (W, W), (0, 0, 0, 0))
d = ImageDraw.Draw(over)
E = lambda cx, cy, rx, ry: [(cx - rx) * K, (cy - ry) * K, (cx + rx) * K, (cy + ry) * K]
d.ellipse(E(72, 66, 24, 14), fill=(255, 255, 255, 72))                       # shine
cheeks = Image.new("RGBA", (W, W), (0, 0, 0, 0))
dc = ImageDraw.Draw(cheeks)
dc.ellipse(E(60, 112, 14, 9), fill=(255, 138, 101, 120))
dc.ellipse(E(132, 112, 14, 9), fill=(255, 138, 101, 120))
cheeks = cheeks.filter(ImageFilter.GaussianBlur(5 * K))
over = Image.alpha_composite(over, cheeks)
d = ImageDraw.Draw(over)
brown = (59, 42, 20, 255)
d.ellipse(E(72, 84, 9, 13), fill=brown)                                      # eyes
d.ellipse(E(120, 84, 9, 13), fill=brown)
d.ellipse(E(75, 79, 3, 3), fill=(255, 255, 255, 255))                        # eye sparkles
d.ellipse(E(123, 79, 3, 3), fill=(255, 255, 255, 255))
# smile: quadratic curve M60 112 Q96 150 132 112, stroked with round caps
# stamp dense round dots along the curve = perfectly smooth thick stroke with round caps
r = 4.5 * K
for i in range(1201):
    t = i / 1200
    x = ((1 - t) ** 2 * 60 + 2 * (1 - t) * t * 96 + t ** 2 * 132) * K
    y = ((1 - t) ** 2 * 112 + 2 * (1 - t) * t * 150 + t ** 2 * 112) * K
    d.ellipse([x - r, y - r, x + r, y + r], fill=brown)

img = Image.alpha_composite(img.convert("RGBA"), over).convert("RGB").resize((S, S), Image.LANCZOS)
img.save(ROOT / "assets" / "tile.bmp")
img.save(ROOT / "assets" / "tile_preview.png")
print("tile.bmp written")

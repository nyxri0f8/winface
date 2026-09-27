"""README banner: assets/banner.jpg (1600x640). Same minimal Face ID-style glyph as the app icon and the lock screen.
Run: .venv\\Scripts\\python bench\\make_banner.py   (needs Pillow + the Segoe UI fonts that ship with Windows)"""
import math
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter, ImageFont

from make_tile import draw as draw_glyph

ROOT = Path(__file__).resolve().parents[1]
W, H, K = 1600, 640, 2          # drawn at 2x, downsampled for smooth edges
FONTS = Path(r"C:\Windows\Fonts")


def font(name, size):
    for n in (name, "segoeui.ttf"):
        p = FONTS / n
        if p.exists():
            return ImageFont.truetype(str(p), size * K)
    return ImageFont.load_default()


img = Image.new("RGB", (W * K, H * K), (14, 14, 17))

# soft blue light behind the glyph, very subtle
glow = Image.new("L", (W * K, H * K), 0)
g = ImageDraw.Draw(glow)
cx, cy = 430 * K, 320 * K
for r in range(460 * K, 0, -6 * K):
    g.ellipse([cx - r, cy - r, cx + r, cy + r], fill=int(70 * (1 - r / (460 * K)) ** 2.2))
glow = glow.filter(ImageFilter.GaussianBlur(40 * K))
img.paste(Image.new("RGB", img.size, (10, 132, 255)), (0, 0), glow)

d = ImageDraw.Draw(img)
# faint dot grid on the right, fading out
for y in range(60 * K, H * K, 34 * K):
    for x in range(760 * K, W * K, 34 * K):
        a = max(0.0, 1 - abs(y - H * K / 2) / (H * K / 2)) * max(0.0, 1 - (x - 760 * K) / (900 * K))
        if a > 0.05:
            c = int(14 + 22 * a)
            d.ellipse([x - 2 * K, y - 2 * K, x + 2 * K, y + 2 * K], fill=(c, c, c + 4))

# the glyph (app icon artwork) with a progress ring around it, like the setup screens
gs = 380 * K
glyph = draw_glyph(1024).resize((gs, gs), Image.LANCZOS)
img.paste(glyph, (cx - gs // 2, cy - gs // 2), glyph)
R = 255 * K
d.ellipse([cx - R, cy - R, cx + R, cy + R], outline=(46, 46, 52), width=4 * K)
d.arc([cx - R, cy - R, cx + R, cy + R], -90, 180, fill=(10, 132, 255), width=6 * K)

# wordmark + tagline + three quiet chips
x0 = 800 * K
d.text((x0, 168 * K), "WinFace", font=font("seguisb.ttf", 112), fill=(245, 245, 247))
d.text((x0 + 4 * K, 318 * K), "Face unlock for Windows,", font=font("segoeui.ttf", 38), fill=(200, 200, 208))
d.text((x0 + 4 * K, 366 * K), "with the webcam you already have.", font=font("segoeui.ttf", 38), fill=(200, 200, 208))
cx2 = x0 + 4 * K
f = font("seguisb.ttf", 22)
for label in ("On-device", "TPM 2.0", "Anti-spoofing", "Open source"):
    tw = d.textlength(label, font=f)
    box = [cx2, 450 * K, cx2 + tw + 40 * K, 498 * K]
    d.rounded_rectangle(box, radius=24 * K, fill=(30, 30, 35), outline=(52, 52, 60), width=2 * K)
    d.text((cx2 + 20 * K, 460 * K), label, font=f, fill=(215, 215, 222))
    cx2 = box[2] + 14 * K

img = img.resize((W, H), Image.LANCZOS)
img.save(ROOT / "assets" / "banner.jpg", quality=92, optimize=True, progressive=True)
print("assets/banner.jpg", img.size)

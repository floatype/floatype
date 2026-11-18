"""Renders floatype's menu bar icons as template images: a filled rounded
square with the mode's character cut out (outline only for direct input).

Run from src/ on macOS (Hiragino Sans and tiffutil come with the system):
  uv run --with pillow python floatype/tools/make_menu_icons.py
It writes data/images/mac/<mode>.tiff with 1x and 2x images, and a preview of
light and dark menu bars to /tmp/floatype-menu-icons/preview.png.
"""
import subprocess
import tempfile
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

FONT = "/System/Library/Fonts/ヒラギノ角ゴシック W6.ttc"
# (character, filled, horizontal scale): half-width ASCII is drawn narrower so
# it differs from full-width; half-width katakana is narrow in the font.
MODES = {
    "hiragana": ("あ", True),
    "full_katakana": ("ア", True),
    "half_katakana": ("ｱ", True),
    "full_ascii": ("Ａ", True),
    "half_ascii": ("A", True, 0.68),
    "direct": ("A", False),
}
SCALE = 16  # supersampling factor

def render(char, filled, size, xscale=1.0):
    big = size * SCALE
    img = Image.new("L", (big, big), 0)
    d = ImageDraw.Draw(img)
    inset = big * 0.06
    box = (inset, inset, big - inset, big - inset)
    radius = big * 0.22
    if filled:
        d.rounded_rectangle(box, radius=radius, fill=255)
    else:
        d.rounded_rectangle(box, radius=radius, outline=255, width=int(big * 0.075))
    font = ImageFont.truetype(FONT, int(big * 0.62))
    glyph = Image.new("L", (big, big), 0)
    g = ImageDraw.Draw(glyph)
    l, t, r, b = g.textbbox((0, 0), char, font=font)
    g.text(((big - (r - l)) / 2 - l, (big - (b - t)) / 2 - t), char, font=font, fill=255)
    if xscale != 1.0:
        narrow = glyph.resize((int(big * xscale), big), Image.LANCZOS)
        glyph = Image.new("L", (big, big), 0)
        glyph.paste(narrow, ((big - narrow.width) // 2, 0))
    if filled:
        img.paste(0, mask=glyph)
    else:
        img.paste(255, mask=glyph)
    alpha = img.resize((size, size), Image.LANCZOS)
    out = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    out.putalpha(alpha)
    return out

out = Path(tempfile.gettempdir()) / "floatype-menu-icons"
out.mkdir(exist_ok=True)
for name, (char, filled, *rest) in MODES.items():
    render(char, filled, 16, *rest).save(out / f"{name}.png")
    render(char, filled, 32, *rest).save(out / f"{name}@2x.png")
    subprocess.run(["tiffutil", "-cathidpicheck", out / f"{name}.png",
                    out / f"{name}@2x.png", "-out",
                    f"data/images/mac/{name}.tiff"], check=True)
# A preview on light and dark menu bars, enlarged.
prev = Image.new("RGBA", (6 * 40 + 8, 2 * 40 + 8), (0, 0, 0, 0))
for row, (bg, fg) in enumerate([((236, 236, 236, 255), (30, 30, 30)), ((40, 40, 40, 255), (235, 235, 235))]):
    strip = Image.new("RGBA", (prev.width, 40), bg)
    for i, name in enumerate(MODES):
        icon = Image.open(out / f"{name}@2x.png")
        tinted = Image.new("RGBA", icon.size, fg + (0,))
        tinted.putalpha(icon.getchannel("A"))
        strip.alpha_composite(tinted, (8 + i * 40, 4))
    prev.paste(strip, (0, row * 40 + 4))
prev.resize((prev.width * 3, prev.height * 3), Image.NEAREST).save(out / "preview.png")

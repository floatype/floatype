"""Renders floatype's menu bar icons as template images: the floatype F mark
with the input mode's character beside it, both cut out of a filled rounded
rectangle of the size and corners of macOS's own input sources (direct input
has no badge).

macOS draws its own icons (ABC's A, the Japanese input's あ) 22x16 points in
the menu bar and in the input menu: measured on a screenshot of the menu,
where they are 44x32 pixels at 2x. The TIFFs inside
/System/Library/Input Methods/JapaneseIM-RomajiTyping.app are 16x16, which is
not what shows; a 16x16 icon looks small beside them.

Run from src/ on macOS (Hiragino Maru Gothic and tiffutil come with the system):
  uv run --with pillow python floatype/tools/make_menu_icons.py
It writes data/images/mac/<mode>.tiff with 22x16 and 44x32 images, and a
preview of light and dark menu bars to /tmp/floatype-menu-icons/preview.png.
"""
import subprocess
import tempfile
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

FONT = "/System/Library/Fonts/ヒラギノ丸ゴ ProN W4.ttc"
WIDTH, HEIGHT = 22, 16  # The size macOS draws its own input sources at.
SS = 16  # Supersampling factor.
# The floatype F: a polyline stroked with round caps and joins, traced from the
# 1080px logo.
F_POINTS = [(774, 277), (306, 277), (604, 575), (306, 575), (306, 803)]
F_WIDTH = 163
F_BOX = (225, 196, 855, 884)
# (character, horizontal scale, framed). Half-width ASCII is drawn narrower so
# that it differs from full-width.
MODES = {
    # The Japanese mode is floatype itself: the F alone, in the middle.
    "hiragana": ("", 1.0, True),
    "full_katakana": ("ア", 1.0, True),
    "half_katakana": ("ｱ", 1.0, True),
    "full_ascii": ("Ａ", 1.0, True),
    "half_ascii": ("A", 0.7, True),
    "direct": ("A", 0.7, False),
}


def stroke(d, points, width):
    r = width / 2
    for (x1, y1), (x2, y2) in zip(points, points[1:]):
        dx, dy = x2 - x1, y2 - y1
        n = (dx * dx + dy * dy) ** 0.5
        ox, oy = -dy / n * r, dx / n * r
        d.polygon([(x1 + ox, y1 + oy), (x2 + ox, y2 + oy),
                   (x2 - ox, y2 - oy), (x1 - ox, y1 - oy)], fill=255)
    for x, y in points:
        d.ellipse((x - r, y - r, x + r, y + r), fill=255)


def draw_f(d, x, y, height, weight):
    s = height / (F_BOX[3] - F_BOX[1])
    stroke(d, [((px - F_BOX[0]) * s + x, (py - F_BOX[1]) * s + y)
               for px, py in F_POINTS], F_WIDTH * s * weight)


def draw_char(img, char, cx, cy, height, xscale):
    font = ImageFont.truetype(FONT, int(height * 1.12))
    size = int(height * 4.4)
    glyph = Image.new("L", (size, size), 0)
    g = ImageDraw.Draw(glyph)
    bold = max(1, int(height * 0.02))  # A little heavier than W4, as macOS's.
    l, t, r, b = g.textbbox((0, 0), char, font=font, stroke_width=bold)
    g.text((size / 2 - (l + r) / 2, size / 2 - (t + b) / 2), char, font=font,
           fill=255, stroke_width=bold, stroke_fill=255)
    if xscale != 1.0:
        glyph = glyph.resize((int(size * xscale), size), Image.LANCZOS)
    img.paste(255, (int(cx - glyph.width / 2), int(cy - glyph.height / 2)),
              mask=glyph)


def render(char, xscale, framed):
    w, h = WIDTH * SS, HEIGHT * SS
    marks = Image.new("L", (w, h), 0)
    if char:
        # The F on the left, the character on the right.
        draw_f(ImageDraw.Draw(marks), w * 0.17, h * 0.27, h * 0.46,
               weight=0.60)
        draw_char(marks, char, w * 0.68, h * 0.50, h * 0.48, xscale)
    else:
        f_height = h * 0.56
        f_width = f_height * (F_BOX[2] - F_BOX[0]) / (F_BOX[3] - F_BOX[1])
        draw_f(ImageDraw.Draw(marks), (w - f_width) / 2, (h - f_height) / 2,
               f_height, weight=0.60)
    if not framed:
        return marks
    # A filled badge with the marks cut out, like macOS's own input sources:
    # it fills the canvas, and its corners are theirs.
    img = Image.new("L", (w, h), 0)
    ImageDraw.Draw(img).rounded_rectangle((0, 0, w - 1, h - 1),
                                          radius=h * 0.30, fill=255)
    img.paste(0, (0, 0), mask=marks)
    return img


def template(alpha, scale):
    alpha = alpha.resize((WIDTH * scale, HEIGHT * scale), Image.LANCZOS)
    out = Image.new("RGBA", alpha.size, (0, 0, 0, 0))
    out.putalpha(alpha)
    return out


def main():
    out = Path(tempfile.gettempdir()) / "floatype-menu-icons"
    out.mkdir(exist_ok=True)
    for name, (char, xscale, framed) in MODES.items():
        big = render(char, xscale, framed)
        template(big, 1).save(out / f"{name}.png")
        template(big, 2).save(out / f"{name}@2x.png")
        subprocess.run(["tiffutil", "-cathidpicheck", out / f"{name}.png",
                        out / f"{name}@2x.png", "-out",
                        f"data/images/mac/{name}.tiff"], check=True)

    # A preview on light and dark menu bars.
    cell_w, cell_h = WIDTH * 2 + 16, HEIGHT * 2 + 12
    preview = Image.new("RGBA", (len(MODES) * cell_w + 8, 2 * cell_h))
    for row, (bg, fg) in enumerate([((236, 236, 236), (30, 30, 30)),
                                    ((40, 40, 40), (235, 235, 235))]):
        strip = Image.new("RGBA", (preview.width, cell_h), bg + (255,))
        for i, name in enumerate(MODES):
            icon = Image.open(out / f"{name}@2x.png")
            tinted = Image.new("RGBA", icon.size, fg + (255,))
            tinted.putalpha(icon.getchannel("A"))
            strip.alpha_composite(tinted, (8 + i * cell_w, 6))
        preview.paste(strip, (0, row * cell_h))
    preview.save(out / "preview.png")


if __name__ == "__main__":
    main()

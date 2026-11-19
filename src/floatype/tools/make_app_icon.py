"""Renders floatype's app icon: the white floatype F mark on a black rounded
square, on the macOS icon grid (an 824px body on a 1024px canvas, with a soft
shadow below).

Run from src/ on macOS (iconutil comes with the system):
  uv run --with pillow python floatype/tools/make_app_icon.py
It writes data/images/mac/product_icon.iconset/*.png and product_icon.icns,
and data/images/product_icon_32bpp-128.png for the about dialog.
"""
import subprocess
import tempfile
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter

from make_menu_icons import F_BOX, draw_f

SS = 4  # Supersampling factor.
TOP, BOTTOM = (40, 40, 40), (0, 0, 0)  # A slight light from above.
ICONSET = Path("data/images/mac/product_icon.iconset")


def render(size):
    w = 1024 * SS
    img = Image.new("RGBA", (w, w), (0, 0, 0, 0))
    x0, body, radius = 100 * SS, 824 * SS, 185 * SS
    shadow = Image.new("L", (w, w), 0)
    ImageDraw.Draw(shadow).rounded_rectangle(
        (x0, x0 + 12 * SS, x0 + body, x0 + body + 12 * SS), radius, fill=90)
    img.paste((0, 0, 0, 255), (0, 0),
              shadow.filter(ImageFilter.GaussianBlur(14 * SS)))
    gradient = Image.new("RGBA", (1, body))
    for y in range(body):
        t = y / (body - 1)
        gradient.putpixel((0, y), tuple(
            int(a + (b - a) * t) for a, b in zip(TOP, BOTTOM)) + (255,))
    mask = Image.new("L", (body, body), 0)
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, body - 1, body - 1), radius,
                                           fill=255)
    img.paste(gradient.resize((body, body)), (x0, x0), mask)
    # The F at the logo's proportions, centred as in the logo.
    height = body * (F_BOX[3] - F_BOX[1]) / 1080
    width = height * (F_BOX[2] - F_BOX[0]) / (F_BOX[3] - F_BOX[1])
    mark = Image.new("L", (w, w), 0)
    draw_f(ImageDraw.Draw(mark), (w - width) / 2, (w - height) / 2, height,
           1.0)
    img.paste((255, 255, 255, 255), (0, 0), mark)
    return img.resize((size, size), Image.LANCZOS)


def main():
    big = render(1024)
    # The checked-in set says "at2x" (Bazel takes no "@" in file names);
    # iconutil wants "@2x", so it reads a copy.
    with tempfile.TemporaryDirectory() as tmp:
        iconset = Path(tmp) / "product_icon.iconset"
        iconset.mkdir()
        for points in (16, 32, 128, 256, 512):
            for scale, suffix in ((1, ""), (2, "at2x")):
                image = big.resize((points * scale, points * scale),
                                   Image.LANCZOS)
                image.save(ICONSET / f"icon_{points}x{points}{suffix}.png")
                image.save(iconset / f"icon_{points}x{points}"
                           f"{'@2x' if scale == 2 else ''}.png")
        subprocess.run(["iconutil", "-c", "icns", iconset, "-o",
                        "data/images/mac/product_icon.icns"], check=True)
    big.resize((128, 128), Image.LANCZOS).save(
        "data/images/product_icon_32bpp-128.png")


if __name__ == "__main__":
    main()

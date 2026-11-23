#!/usr/bin/env python3
"""Render recorded firmware UI operations with the installed GLCD pixel font.

First run tests/dashboard_preview.cpp with .build/ui-preview as its argument.
Requires Pillow. This produces software previews, not hardware screenshots.
"""
import argparse
from pathlib import Path
import re

from PIL import Image, ImageDraw, ImageFont

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--ops", type=Path, default=root / ".build/ui-preview")
parser.add_argument("--output", type=Path, default=root / "previews")
parser.add_argument("--font", type=Path, default=Path(
    "/home/seeed/Arduino/libraries/Seeed_GFX2/src/font/Font_GLCD.h"))
args = parser.parse_args()
data = args.font.read_text().split("glcd_font[] PROGMEM = {", 1)[1].split("};", 1)[0]
data = re.sub(r"//[^\n]*", "", data)
font = bytes(int(value, 16) for value in re.findall(r"0x([0-9a-fA-F]+)", data))
assert len(font) == 1280
args.output.mkdir(parents=True, exist_ok=True)


def color(rgb565):
    return ((rgb565 >> 11) * 255 // 31,
            ((rgb565 >> 5) & 63) * 255 // 63,
            (rgb565 & 31) * 255 // 31)


for ops in sorted(args.ops.glob("*.ops")):
    screen = Image.new("RGB", (135, 240))
    draw = ImageDraw.Draw(screen)
    for line in ops.read_text().splitlines():
        kind, payload = line.split(" ", 1)
        if kind == "text":
            xx, yy, ink, size, value = payload.split(" ", 4)
            xx, yy, size = int(xx), int(yy), int(size)
            for character in value:
                glyph = font[ord(character)*5:ord(character)*5+5]
                for x, column in enumerate(glyph):
                    for y in range(8):
                        if column & (1 << y):
                            left, top = xx + x*size, yy + y*size
                            draw.rectangle((left, top, left+size-1, top+size-1),
                                           fill=color(int(ink)))
                xx += 6*size
            continue
        *values, ink = (int(value) for value in payload.split())
        ink = color(ink)
        if kind in ("rect", "round"):
            x, y, w, h, *radius = values
            box = (x, y, x+w-1, y+h-1)
            if radius:
                draw.rounded_rectangle(box, radius[0], fill=ink)
            else:
                draw.rectangle(box, fill=ink)
        elif kind == "circle":
            x, y, r = values
            draw.ellipse((x-r, y-r, x+r, y+r), fill=ink)
        elif kind == "line":
            draw.line(values, fill=ink)
        elif kind == "triangle":
            draw.polygon(list(zip(values[::2], values[1::2])), fill=ink)
        else:
            raise ValueError(kind)
    screen.save(args.output / f"{ops.stem}.png")

samples = ("ready", "forward", "reverse", "locked", "wait", "fault")
sheet = Image.new("RGB", (906, 1056), "#0e1521")
draw = ImageDraw.Draw(sheet)
label_font = ImageFont.truetype("DejaVuSans.ttf", 15)
for index, name in enumerate(samples):
    left = 30 + (index % 3)*292
    top = 18 + (index // 3)*520
    draw.text((left, top), name.upper(), fill="#a6bac9", font=label_font)
    image = Image.open(args.output / f"{name}.png").resize((270, 480), Image.Resampling.NEAREST)
    sheet.paste(image, (left, top+25))
sheet.save(args.output / "dashboard.png")
print(f"Software preview: {args.output / 'dashboard.png'}")

#!/usr/bin/env python3
"""Pre-renders the digit-style fonts into resources/fonts/<name>.bin for src/c/main.c.

Each font is rendered at the few sizes the face uses (one "group" per screen area), with
4-level anti-aliasing (2 bits per pixel), so the watch only has to copy pixels: no font
engine, and positions are exact. Every glyph bitmap is the group's height H, from the top
of the reference glyph ("8" for digits, "M" for letters) down to the baseline. A group's
size is fixed by its widest possible text, so it never changes with the value shown.

Fonts (SIL Open Font License 1.1, from https://github.com/google/fonts/tree/main/ofl):
    oxanium/Oxanium[wght].ttf, chakrapetch/ChakraPetch-Bold.ttf, orbitron/Orbitron[wght].ttf
Usage (needs Pillow):
    python3 tools/gen_fonts.py Oxanium.ttf ChakraPetch-Bold.ttf Orbitron.ttf

File format (all offsets absolute, little-endian):
    "LF" 1 <groups>, then per group: H, count, table offset (2 bytes);
    each table entry: char, width, advance, left bearing (signed), bitmap offset (2 bytes);
    bitmaps: H rows of ceil(width * 2 / 8) bytes, 2 bits per pixel, first pixel in the high bits.
"""
import struct
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

SUPER = 4  # supersampling for the anti-aliasing
MIN_SQUEEZE = 0.8
DIGITS = "0123456789"
WEEKDAYS = ["SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"]
# The groups, in the order main.c expects (FontGroup): characters, reference glyph, height,
# and the widest texts (their ink) with the room they get. Too wide, a group is first narrowed
# (down to MIN_SQUEEZE of its width), then made smaller.
GROUPS = [
    ("time", DIGITS + ":", "8", 70, [(d, 41) for d in DIGITS]),
    ("weekday", "ADEFHIMNORSTUW", "M", 24, [(w, 64) for w in WEEKDAYS]),
    ("date", DIGITS + "-", "8", 26, [("00", 30), ("00-00", 110)]),
    ("range", DIGITS, "8", 30, [("88", 34)]),  # (100 and up runs further left)
    # (Below zero or from 100 up, the watch leaves out the degree mark to keep this size.)
    ("right", DIGITS + "-°", "8", 36, [("88°", 66), ("-88", 66), ("188", 66)]),
    ("label", "BCDEFGHLMSTU", "M", 10, [("MUTE", 36), ("FULL", 36), ("CHG", 37), ("DST", 35)]),
]
FONTS = {  # output name -> variable-font weight (None: a static font)
    "oxanium": 700, "chakrapetch": None, "orbitron": 700,
}


def load(path, size, weight):
    font = ImageFont.truetype(str(path), size)
    if weight is not None:
        axes = font.get_variation_axes()
        font.set_variation_by_axes([weight if (a["name"] in (b"Weight", "Weight")) else a["default"]
                                    for a in axes])
    return font


def render_group(path, weight, chars, ref, height, fits):
    # Size: the reference glyph `height` px tall, then smaller while a sample is too wide.
    probe = load(path, 1000, weight)
    ref_h = probe.getbbox(ref, anchor="ls")
    ref_h = -ref_h[1]  # height above the baseline
    size = 1000 * height * SUPER / ref_h
    font = load(path, round(size), weight)
    ratio = max((font.getbbox(t)[2] - font.getbbox(t)[0]) / SUPER / room for t, room in fits)
    sx = 1.0
    if ratio > 1:
        sx = max(1 / ratio, MIN_SQUEEZE)
        if ratio * sx > 1:
            size /= ratio * sx
            font = load(path, round(size), weight)
    top = -font.getbbox(ref, anchor="ls")[1]          # reference top, above the baseline
    h = round(top / SUPER)
    glyphs = []
    for ch in chars:
        adv = font.getlength(ch)
        pad = SUPER * 8
        img = Image.new("L", (int(adv) + 2 * pad, h * SUPER), 0)
        ImageDraw.Draw(img).text((pad, h * SUPER), ch, font=font, fill=255, anchor="ls")
        if ch == ":" and height == 70:
            # A clock's colon is centred on the digits, not sitting on the baseline.
            box = img.getbbox()
            if box:
                ink = img.crop(box)
                img = Image.new("L", img.size, 0)
                img.paste(ink, (box[0], (img.height - ink.height) // 2))
        if sx != 1.0:
            img = img.resize((round(img.width * sx), img.height), Image.LANCZOS)
        box = img.getbbox() or (pad, 0, pad + 1, 1)
        x0 = box[0] - box[0] % SUPER
        x1 = box[2] + (-box[2]) % SUPER
        small = img.crop((x0, 0, x1, h * SUPER)).reduce(SUPER)  # box-average down-sampling
        w = small.width
        levels = [min(3, (v + 42) // 85) for v in small.tobytes()]
        rows = []
        for y in range(h):
            row = bytearray((w * 2 + 7) // 8)
            for x in range(w):
                row[x >> 2] |= levels[y * w + x] << (6 - 2 * (x & 3))
            rows.append(bytes(row))
        lsb = round((x0 - pad * sx) / SUPER)
        glyphs.append((ch, w, round(adv * sx / SUPER), lsb, b"".join(rows)))
    return h, glyphs


def build(path, weight):
    groups = [render_group(path, weight, chars, ref, height, fits)
              for _, chars, ref, height, fits in GROUPS]
    header = bytearray(b"LF\x01" + bytes([len(groups)]))
    pos = len(header) + 4 * len(groups)
    tables, bitmaps = [], bytearray()
    table_pos = []
    for h, glyphs in groups:
        table_pos.append(pos)
        pos += 6 * len(glyphs)
    data_start = pos
    for (h, glyphs), tpos in zip(groups, table_pos):
        header += struct.pack("<BBH", h, len(glyphs), tpos)
        table = bytearray()
        for ch, w, adv, lsb, bits in glyphs:
            table += struct.pack("<BBBbH", ord(ch) if ord(ch) < 256 else 0xB0, w, adv, lsb,
                                 data_start + len(bitmaps))
            bitmaps += bits
        tables.append(table)
    blob = bytes(header) + b"".join(tables) + bytes(bitmaps)
    assert len(blob) < 65536, len(blob)
    return blob, [(name, h) for (name, *_), (h, _) in zip(GROUPS, groups)]


def main():
    out = Path(__file__).resolve().parent.parent / "resources" / "fonts"
    out.mkdir(parents=True, exist_ok=True)
    for arg in sys.argv[1:]:
        key = Path(arg).name.lower().split("[")[0].split("-")[0].replace(".ttf", "")
        name = next(n for n in FONTS if n.startswith(key[:6]))
        blob, sizes = build(arg, FONTS[name])
        (out / f"{name}.bin").write_bytes(blob)
        print(f"{name}.bin: {len(blob)} bytes, heights {sizes}")


if __name__ == "__main__":
    main()

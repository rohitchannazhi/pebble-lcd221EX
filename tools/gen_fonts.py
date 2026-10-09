#!/usr/bin/env python3
"""Pre-renders the digit-style fonts into resources/fonts/<name>.bin for src/c/main.c.

Each font is rendered at the few sizes the face uses (one "group" per screen area), with
4-level anti-aliasing (2 bits per pixel), so the watch only has to copy pixels: no font
engine, and positions are exact. Every glyph bitmap is the group's height H, from the top
of the reference glyph ("8" for digits, "M" for letters) down to the baseline. A group's
size is fixed by its widest possible text, so it never changes with the value shown.

Fonts (SIL Open Font License 1.1, from https://github.com/google/fonts/tree/main/ofl):
    sairaextracondensed/SairaExtraCondensed-Bold.ttf, handjet/Handjet[ELGR,ELSH,wght].ttf,
    iceberg/Iceberg-Regular.ttf, bigshouldersstencil/BigShouldersStencil[opsz,wght].ttf,
    and oxanium/Oxanium[wght].ttf, which only lends its letters (see LABEL_FONT);
    chakrapetch/ChakraPetch-SemiBold.ttf for the bezels' text (bezel.bin, see BEZEL_GROUPS).
Usage (needs Pillow; give all six, as they borrow from each other):
    python3 tools/gen_fonts.py SairaExtraCondensed-Bold.ttf Handjet*.ttf Iceberg-Regular.ttf \
        BigShouldersStencil*.ttf Oxanium*.ttf ChakraPetch-SemiBold.ttf

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
    # (Also the day of the month beside it, at the same size: "MON 05" must fit before the indicators.)
    ("weekday", "ADEFHIMNORSTUW" + DIGITS, "M", 24, [(w, 64) for w in WEEKDAYS] + [("WED30", 92)]),
    ("date", DIGITS + "-", "8", 26, [("00", 30), ("00-00", 110)]),
    ("range", DIGITS + "-", "8", 30, [("88", 34)]),  # (100 and up, or below zero, runs further left)
    # (Below zero or from 100 up, the watch leaves out the degree mark to keep this size.)
    ("right", DIGITS + "-°", "8", 36, [("88°", 66), ("-88", 66), ("188", 66)]),
    ("label", "BCDEFGHILMOPSTU", "M", 10, [("MUTE", 36), ("FULL", 36), ("CHG", 37), ("DST", 35)]),
]
# Output name -> (the start of its file's name, its variable-font axis settings, or None).
# The four digit styles are condensed: their digits fill the 7-segment digits' cells without
# being squeezed or made smaller.
FONTS = {
    "saira": ("sairaextracondensed", None),
    "handjet": ("handjet", {"Weight": 600}),
    "iceberg": ("iceberg", None),
    "stencil": ("bigshouldersstencil", {"Weight": 800}),
    "oxanium": ("oxanium", {"Weight": 700}),  # only lends its letters, below
    "chakrapetch": ("chakrapetch", None),      # the bezels' font
}
OUTPUT = ["saira", "handjet", "iceberg", "stencil"]  # in RESOURCE_ID order (package.json)
# The small indicator labels: condensed letters are hard to read at 10 px, so they all come
# from Oxanium.
LABEL_FONT = {name: "oxanium" for name in OUTPUT}
# Characters taken from another font: Iceberg's degree mark sits above its digits, where the
# glyphs are cut off.
CHAR_FONT = {"iceberg": {"\u00b0": "saira"}}
# bezel.bin: the top and bottom bezels' text (main.c's BezelGroup), capitals 13 (and 11) px tall,
# with every character an upper-cased custom text may use (anything else falls back to the
# system font). Its glyphs also reach below the baseline (commas, brackets).
BEZEL_FONT = "chakrapetch"
BEZEL_CHARS = "".join(chr(c) for c in range(0x20, 0x60))
# (The second size is for texts too long for 13 px.)
BEZEL_GROUPS = [("normal", BEZEL_CHARS, "H", 13), ("small", BEZEL_CHARS, "H", 11)]


def load(path, size, axes):
    font = ImageFont.truetype(str(path), size)
    if axes:
        names = [a["name"].decode() if isinstance(a["name"], bytes) else a["name"]
                 for a in font.get_variation_axes()]
        font.set_variation_by_axes([axes.get(n, a["default"])
                                    for n, a in zip(names, font.get_variation_axes())])
    return font


def render_group(path, axes, chars, ref, height, fits, below=False):
    # Size: the reference glyph `height` px tall, then smaller while a sample is too wide.
    probe = load(path, 1000, axes)
    ref_h = probe.getbbox(ref, anchor="ls")
    ref_h = -ref_h[1]  # height above the baseline
    size = 1000 * height * SUPER / ref_h
    font = load(path, round(size), axes)
    ratio = max((font.getbbox(t)[2] - font.getbbox(t)[0]) / SUPER / room for t, room in fits)
    sx = 1.0
    if ratio > 1:
        sx = max(1 / ratio, MIN_SQUEEZE)
        if ratio * sx > 1:
            size /= ratio * sx
            font = load(path, round(size), axes)
    top = -font.getbbox(ref, anchor="ls")[1]          # reference top, above the baseline
    h = round(top / SUPER)
    # Rows below the baseline (`below`: as many as the deepest character needs), then the height.
    d = -(-max(font.getbbox(ch, anchor="ls")[3] for ch in chars) // SUPER) if below else 0
    h_all = h + max(d, 0)
    glyphs = []
    for ch in chars:
        adv = font.getlength(ch)
        pad = SUPER * 8
        img = Image.new("L", (int(adv) + 2 * pad, h_all * SUPER), 0)
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
        small = img.crop((x0, 0, x1, h_all * SUPER)).reduce(SUPER)  # box-average down-sampling
        w = small.width
        levels = [min(3, (v + 42) // 85) for v in small.tobytes()]
        rows = []
        for y in range(h_all):
            row = bytearray((w * 2 + 7) // 8)
            for x in range(w):
                row[x >> 2] |= levels[y * w + x] << (6 - 2 * (x & 3))
            rows.append(bytes(row))
        lsb = round((x0 - pad * sx) / SUPER)
        glyphs.append((ch, w, round(adv * sx / SUPER), lsb, b"".join(rows)))
    return h_all, glyphs


def build(name, paths):
    groups = []
    for group, chars, ref, height, fits in GROUPS:
        src = LABEL_FONT.get(name, name) if group == "label" else name
        h, glyphs = render_group(paths[src], FONTS[src][1], chars, ref, height, fits)
        for ch, donor in CHAR_FONT.get(name, {}).items():
            if ch not in chars:
                continue
            dh, dglyphs = render_group(paths[donor], FONTS[donor][1], chars, ref, height, fits)
            glyph = next(gl for gl in dglyphs if gl[0] == ch)
            row = (glyph[1] * 2 + 7) // 8
            bits = glyph[4][(dh - h) * row:] if dh >= h else bytes((h - dh) * row) + glyph[4]
            glyphs = [glyph[:4] + (bits,) if gl[0] == ch else gl for gl in glyphs]
        groups.append((h, glyphs))
    return pack(groups), [(name, h) for (name, *_), (h, _) in zip(GROUPS, groups)]


def build_bezel(paths):
    groups = [render_group(paths[BEZEL_FONT], FONTS[BEZEL_FONT][1], chars, ref, height,
                           [("H", 1000)], below=True)
              for _, chars, ref, height in BEZEL_GROUPS]
    return pack(groups), [(name, h) for (name, *_), (h, _) in zip(BEZEL_GROUPS, groups)]


def pack(groups):
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
    return blob


def main():
    out = Path(__file__).resolve().parent.parent / "resources" / "fonts"
    out.mkdir(parents=True, exist_ok=True)
    paths = {}
    for arg in sys.argv[1:]:
        key = Path(arg).name.lower()
        paths[next(n for n, (start, _) in FONTS.items() if key.startswith(start))] = arg
    for name in OUTPUT:
        blob, sizes = build(name, paths)
        (out / f"{name}.bin").write_bytes(blob)
        print(f"{name}.bin: {len(blob)} bytes, heights {sizes}")
    blob, sizes = build_bezel(paths)
    (out / "bezel.bin").write_bytes(blob)
    print(f"bezel.bin: {len(blob)} bytes, heights (with the rows below the baseline) {sizes}")


if __name__ == "__main__":
    main()

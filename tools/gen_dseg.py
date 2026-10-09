#!/usr/bin/env python3
"""Writes src/c/segments_dseg.h: the segment outlines of the "Modern" digit styles, from the
DSEG fonts by keshikan (SIL Open Font License 1.1, https://github.com/keshikan/DSEG; the
outlines are a Modified Version, so they don't use the Reserved Font Name "DSEG").

- DSEG7 Modern Light and Bold: the 7 segments (A to G) of the "8", like segments.h's.
- DSEG14 Modern Light and Bold: the 14 segments of the all-segments glyph "~", and for each
  letter A to Z a mask of the segments it lights.

Coordinates are 0..1000 across each glyph's box, y down. Usage (needs fontTools):
    python3 tools/gen_dseg.py fonts-DSEG_v046/      # the unzipped DSEG release
"""
import sys
from pathlib import Path
from fontTools.ttLib import TTFont

ROOT = Path(__file__).resolve().parent.parent


def contours(font, ch):
    glyf = font['glyf']
    g = glyf[font.getBestCmap()[ord(ch)]]
    coords, ends, _ = g.getCoordinates(glyf)
    out, start = [], 0
    for e in ends:
        out.append([tuple(coords[i]) for i in range(start, e + 1)])
        start = e + 1
    return out


def normalizer(polys):
    xs = [p[0] for c in polys for p in c]
    ys = [p[1] for c in polys for p in c]
    x0, x1, y0, y1 = min(xs), max(xs), min(ys), max(ys)
    return lambda p: (round((p[0] - x0) * 1000 / (x1 - x0)), round((y1 - p[1]) * 1000 / (y1 - y0)))


def centroid(c):
    return (sum(p[0] for p in c) / len(c), sum(p[1] for p in c) / len(c))


def seven(path):
    polys = contours(TTFont(path), '8')
    norm = normalizer(polys)
    polys = [[norm(p) for p in c] for c in polys]
    named = {}
    for c in polys:
        cx, cy = centroid(c)
        horiz = max(p[0] for p in c) - min(p[0] for p in c) > max(p[1] for p in c) - min(p[1] for p in c)
        if horiz:
            k = 'A' if cy < 300 else ('D' if cy > 700 else 'G')
        else:
            k = ('F' if cx < 500 else 'B') if cy < 500 else ('E' if cx < 500 else 'C')
        assert k not in named, (k, path)
        named[k] = c
    assert len(named) == 7
    return [named[k] for k in 'ABCDEFG']


def fourteen(path):
    font = TTFont(path)
    full = contours(font, '~')
    norm = normalizer(full)
    segs = [[norm(p) for p in c] for c in full]
    keys = [centroid(c) for c in full]
    masks = []
    for ch in (chr(c) for c in range(ord('A'), ord('Z') + 1)):
        mask = 0
        for c in contours(font, ch):  # each of the letter's contours is one of the segments
            cx, cy = centroid(c)
            k = min(range(len(keys)), key=lambda i: (keys[i][0] - cx) ** 2 + (keys[i][1] - cy) ** 2)
            assert abs(keys[k][0] - cx) + abs(keys[k][1] - cy) < 2 and len(c) == len(full[k]), (path, ch)
            mask |= 1 << k
        masks.append(mask)
    return segs, masks


def emit(o, name, polys):
    for i, c in enumerate(polys):
        o.write('static const int16_t %s_%d[] = { %s };\n' % (name, i, ', '.join('%d, %d' % p for p in c)))
    o.write('static const int16_t *const %s[%d] = { %s };\n'
            % (name, len(polys), ', '.join('%s_%d' % (name, i) for i in range(len(polys)))))
    o.write('static const uint8_t %s_LEN[%d] = { %s };\n' % (name, len(polys), ', '.join(str(len(c)) for c in polys)))


def main():
    d = Path(sys.argv[1])
    with open(ROOT / 'src/c/segments_dseg.h', 'w') as o:
        o.write('// Made by tools/gen_dseg.py from the DSEG fonts by keshikan (SIL OFL 1.1): do not edit.\n')
        o.write('#pragma once\n')
        for w in ('Light', 'Bold'):
            emit(o, 'SEG7_MODERN_' + w.upper(), seven(d / 'DSEG7-Modern' / f'DSEG7Modern-{w}.ttf'))
            segs, masks = fourteen(d / 'DSEG14-Modern' / f'DSEG14Modern-{w}.ttf')
            emit(o, 'SEG14_MODERN_' + w.upper(), segs)
            o.write('static const uint16_t SEG14_MODERN_%s_LETTERS[26] = { %s };  // A to Z\n'
                    % (w.upper(), ', '.join('0x%04X' % m for m in masks)))
    print('wrote src/c/segments_dseg.h')


if __name__ == '__main__':
    main()

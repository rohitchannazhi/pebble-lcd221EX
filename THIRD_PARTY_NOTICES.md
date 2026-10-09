# Third-party notices

## 7-Segment font (segment outlines)

The digit shapes on the watch face are the segment outlines of the **7-Segment**
font by **Jan Bobrowski** ("~jb"), <https://torinak.com/font/7-segment>, version 3.0.

- Licence: **SIL Open Font License 1.1** (full text in [`LICENSES/OFL-1.1.txt`](LICENSES/OFL-1.1.txt)).
- How it is used: `tools/gen_segments.py` reads the font, straightens its slight italic
  and writes the seven segment outlines as polygons into `src/c/segments.h`. The font
  file itself is not included in this repository or in the watch app; only these
  derived outlines are. Because that makes `segments.h` a modified derivative of the
  font, it stays under the same licence. The outlines are not sold on their own.
- The font file carries no copyright line or Reserved Font Name.

## DSEG (the Modern segment outlines)

The "Modern Light" and "Modern Bold" digit styles (and Hollow segments' weekday row) use the
segment outlines of **DSEG7 Modern** and **DSEG14 Modern** (Light and Bold) by **keshikan**,
<https://github.com/keshikan/DSEG>, version 0.46: Copyright (c) 2017, keshikan
(http://www.keshikan.net), with Reserved Font Name "DSEG".

- Licence: **SIL Open Font License 1.1** (full text in [`LICENSES/OFL-1.1.txt`](LICENSES/OFL-1.1.txt)).
- How it is used: `tools/gen_dseg.py` reads the fonts and writes the segments of the "8" (DSEG7)
  and of the all-segments glyph, with each letter's segments (DSEG14), as polygons into
  `src/c/segments_dseg.h`. The font files are not included. The outlines are a Modified Version,
  so they are offered as "Modern" and not under the reserved name; they stay under the same
  licence and are not sold on their own.

## Iceberg, Saira, Chakra Petch and Oxanium (pre-rendered fonts)

The "Digit style" font and the bezels' font are pre-rendered by `tools/gen_fonts.py` into the bitmap files in
`resources/fonts/` (only the characters the face uses, at its sizes, some slightly narrowed).
The font files themselves are not included.

- **Iceberg**: Copyright (c) 2011, Cyreal (www.cyreal.org), with Reserved Font Name "Iceberg". The
  rendered version is a Modified Version, so it is offered as "Angular (based on Iceberg)" and not
  under the reserved name. Its degree mark is Saira's.
- **Saira** (Saira Extra Condensed Bold): Copyright 2016 The Saira Project Authors
  (omnibus.type@gmail.com), with Reserved Font Name "Saira". Only Angular's degree mark.
- **Chakra Petch** (SemiBold): Copyright 2018 The Chakra Petch Project Authors
  (https://github.com/m4rc1e/Chakra-Petch.git). The top and bottom bezels' text (`bezel.bin`).
- **Oxanium**: Copyright 2019 The Oxanium Project Authors (https://github.com/sevmeyer/oxanium).
  Only Angular's small indicator labels, which read better in its letters at that size.
- Licence: **SIL Open Font License 1.1** (full text in [`LICENSES/OFL-1.1.txt`](LICENSES/OFL-1.1.txt));
  the rendered files stay under the same licence.

## Clay (settings page library)

The phone-side settings page is built with **pebble-clay**, Copyright (c) 2016 Pebble
Technology, MIT licence (full text in [`LICENSES/pebble-clay-MIT.txt`](LICENSES/pebble-clay-MIT.txt)).

## Weather data

Temperatures come from the **Open-Meteo** API, <https://open-meteo.com>, which is
free for non-commercial use and licensed CC BY 4.0. Weather data by Open-Meteo.com.

## Trademarks

Casio and W-221H are trademarks of Casio Computer Co., Ltd. Pebble is a trademark of
its owners. This watch face is an independent, unofficial tribute to the look of the
Casio W-221H and is not affiliated with or endorsed by Casio or Pebble.

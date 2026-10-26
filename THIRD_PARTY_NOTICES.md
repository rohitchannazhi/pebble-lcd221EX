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

The "Modern Light", "Modern Bold" and "Classic Bold" digit styles (and Hollow segments' weekday row)
use the segment outlines of **DSEG7 Modern** and **DSEG14 Modern** (Light and Bold) and **DSEG7
Classic** (Bold) by **keshikan**,
<https://github.com/keshikan/DSEG>, version 0.46: Copyright (c) 2017, keshikan
(http://www.keshikan.net), with Reserved Font Name "DSEG".

- Licence: **SIL Open Font License 1.1** (full text in [`LICENSES/OFL-1.1.txt`](LICENSES/OFL-1.1.txt)).
- How it is used: `tools/gen_dseg.py` reads the fonts and writes the segments of the "8" (DSEG7)
  and of the all-segments glyph, with each letter's segments (DSEG14), as polygons into
  `src/c/segments_dseg.h`. The font files are not included. The outlines are a Modified Version,
  so they are offered as "Modern" and "Classic Bold" and not under the reserved name; they stay under the same
  licence and are not sold on their own.

## Iceberg, Bebas Neue, Quantico, Teko, Orbitron, Squada One, Chakra Petch, Barlow and Oxanium (pre-rendered fonts)

The "Digit style" font and the bezels' font are pre-rendered by `tools/gen_fonts.py` into the bitmap files in
`resources/fonts/` (only the characters the face uses, at its sizes, some slightly narrowed).
The font files themselves are not included.

- **Iceberg**: Copyright (c) 2011, Cyreal (www.cyreal.org), with Reserved Font Name "Iceberg". The
  rendered version is a Modified Version, so it is offered as "Angular (based on Iceberg)" and not
  under the reserved name.
- **Bebas Neue**: Copyright (c) 2010 by Dharma Type (no Reserved Font Name). The "Bebas Neue" digit
  style (`bebas.bin`).
- **Quantico** (Bold): Copyright (c) 2011 Matthew Desmond (http://www.madtype.com), with Reserved
  Font Name "Quantico". The rendered version is a Modified Version, offered as "Tactical (based on
  Quantico)" (`quantico.bin`).
- **Teko** (SemiBold): Copyright 2023 The Teko Project Authors (https://www.github.com/googlefonts/teko)
  (no Reserved Font Name). The "Teko" digit style (`teko.bin`).
- **Orbitron** (Bold): Copyright 2018 The Orbitron Project Authors
  (https://github.com/theleagueof/orbitron), with Reserved Font Name "Orbitron". Offered as
  "Orbital (based on Orbitron)" (`orbitron.bin`).
- **Squada One**: Copyright (c) 2011, Admix Designs (http://www.admixdesigns.com/), with Reserved
  Font Name "Squada". Offered as "Squared (based on Squada One)" (`squada.bin`).
- **Chakra Petch** (SemiBold): Copyright 2018 The Chakra Petch Project Authors
  (https://github.com/m4rc1e/Chakra-Petch.git). The top and bottom bezels' text (`bezel.bin`).
- **Barlow** (Semi Condensed Bold): Copyright 2017 The Barlow Project Authors
  (https://github.com/jpt/barlow) (no Reserved Font Name). The indicator labels of the styles without
  a font of their own (`bezel.bin`'s label size).
- **Oxanium**: Copyright 2019 The Oxanium Project Authors (https://github.com/sevmeyer/oxanium).
  Only the digit style fonts' small indicator labels, which read better in its letters at that size.
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

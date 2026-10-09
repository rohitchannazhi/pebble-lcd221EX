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

## Saira, Handjet, Iceberg, Big Shoulders Stencil, Chakra Petch and Oxanium (pre-rendered fonts)

The "Digit style" fonts are pre-rendered by `tools/gen_fonts.py` into the bitmap files in
`resources/fonts/` (only the characters the face uses, at its sizes, some slightly narrowed).
The font files themselves are not included.

- **Saira** (Saira Extra Condensed Bold): Copyright 2016 The Saira Project Authors
  (omnibus.type@gmail.com), with Reserved Font Name "Saira". The rendered version is a Modified
  Version, so it is offered as "Condensed (based on Saira)" and not under the reserved name.
- **Handjet**: Copyright 2018 The Handjet Project Authors (https://github.com/rosettatype/Handjet/).
- **Iceberg**: Copyright (c) 2011, Cyreal (www.cyreal.org), with Reserved Font Name "Iceberg". The
  rendered version is a Modified Version, so it is offered as "Angular (based on Iceberg)" and not
  under the reserved name. Its degree mark is Saira's.
- **Big Shoulders Stencil**: Copyright 2019 The Big Shoulders Project Authors
  (https://github.com/xotypeco/big_shoulders).
- **Chakra Petch** (SemiBold): Copyright 2018 The Chakra Petch Project Authors
  (https://github.com/m4rc1e/Chakra-Petch.git). The top and bottom bezels' text (`bezel.bin`).
- **Oxanium**: Copyright 2019 The Oxanium Project Authors (https://github.com/sevmeyer/oxanium).
  Not a digit style of its own: every style's small indicator labels use its letters, which read
  better at that size than the condensed fonts'.
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

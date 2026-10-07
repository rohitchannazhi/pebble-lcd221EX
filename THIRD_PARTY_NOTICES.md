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

## Oxanium, Chakra Petch and Orbitron (digit style fonts)

The "Digit style" fonts are pre-rendered by `tools/gen_fonts.py` into the bitmap files in
`resources/fonts/` (only the characters the face uses, at its sizes, some slightly narrowed).
The font files themselves are not included.

- **Oxanium**: Copyright 2019 The Oxanium Project Authors (https://github.com/sevmeyer/oxanium).
- **Chakra Petch**: Copyright 2018 The Chakra Petch Project Authors (https://github.com/m4rc1e/Chakra-Petch.git).
- **Orbitron**: Copyright 2018 The Orbitron Project Authors (https://github.com/theleagueof/orbitron),
  with Reserved Font Name "Orbitron". The rendered version is a Modified Version, so it is
  offered as "Wide (based on Orbitron)" and not under the reserved name. Its small indicator labels use
  Oxanium's letters, which read better at that size.
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

# LCD 221

A watch face for the **Pebble Time 2** that imitates the look of the Casio W-221H:
a white LCD with slanted 7-segment digits, a dot-matrix weekday and a 2x2 indicator
box, between a black top bezel and bottom bezel.

![LCD 221](docs/watchface.png)

Inverted colors, and 24-hour time with seconds instead of the temperature:

![Inverted colors](docs/watchface-inverted.png) ![Seconds](docs/watchface-seconds.png)

Version 1.4.0. Pebble Time 2 only (platform `emery`, 200x228 screen). Built and tested with Pebble SDK 4.33.1.

## What it shows

| Area | Content |
|---|---|
| Top bezel | A battery icon (filled to the level, with a bolt while charging) and the level, and a walking figure and the step count; or your own text instead of either (settings) |
| Weekday | Dot-matrix day name (SUN, MON, ...). While the date box shows the high and low, the letters are narrower and the day of the month follows them (`MON05`) |
| Indicator box | **BT** phone connected, **CHG** charging (it becomes **FULL** once the battery is full and the watch is still on the charger), **DST** daylight saving time is in effect in your time zone, **MUTE** Quiet Time on. Active labels are black (white when inverted); inactive ones use the same faint gray as the unlit segments |
| Time | Large 7-segment digits (slanted by default). A **P** lights up for PM in 12-hour mode |
| Date box | DD-MM or MM-DD, or today's low and high temperature side by side, each after a mark (tall arrows, triangles or LO / HI) with a divider between them (settings). |
| Right box | Temperature (°C or °F) or seconds |
| Bottom bezel | A **WR** badge (or a heart and your latest heart rate) and a text label |

Notes:

- Battery is reported by the watch in 10% steps, so it moves 100%, 90%, 80%, ...
- Steps come from Pebble Health. Heart rate is the latest reading the watch has; it shows `--` when there is none.
- Weather (the current temperature and today's high and low) is fetched by the phone from [Open-Meteo](https://open-meteo.com) using its location, at start-up and about every 30 minutes. To save battery, the watch only asks while the temperature can be on screen (not with seconds always on), the phone is connected and the last reading is older than 25 minutes, and the phone app waits at least 5 minutes between fetches (25 minutes when the face is merely opened again). It shows `--` if the data is more than 3 hours old.
- Unlit segments are drawn as a faint ghost, like a real LCD (can be switched off).
- Every part of the face can be given its own colour (see Settings).
- The Time 2's backlight is colour-capable. By default the watch face leaves it at your normal system colour, but it can tint it instead (amber like the original's LED, one of ten presets, or any of the 64 colours in the app's picker).

## Settings

Open the watch face's settings in the Pebble app. Nothing reaches the watch until you tap **Save**.
**Reset to defaults** (at the top of the page; tap it twice, so a stray tap does nothing) puts every option back to the value below (then tap Save).

| Group | Option | Choices (default first) |
|---|---|---|
| Time & date | Time format | **Follow watch** (its 12/24-hour setting), 24-hour, 12-hour |
| | Leading zero in the hour | On (07:05), Off (`7:05`, like the original). 24-hour format only; not shown while 12-hour is selected |
| | Leading zero in 12-hour time | Off. On shows 07:05 instead of 7:05; the P beside the hours makes way, and **PM** is shown in the indicator box in place of CHG / FULL (the battery icon shows a bolt while charging). Not shown while 24-hour is selected |
| | Date format | DD-MM, MM-DD, Min / max temperature (today's low then high, side by side, each after a mark (see below), for the day where your phone is, in place of the date; `--` until the first weather reading of the day. The day of the month moves up next to the weekday: `MON05`) |
| | Min / max marks | **Tall arrows** (↓ beside the low, ↑ beside the high), Triangles, LO / HI. Below zero the minus goes before the number (above the triangle with Triangles). Only shown with the min / max date format |
| Right box | Right box shows | Temperature, Seconds (redraws every second: uses more battery) |
| | Temperature unit | **Follow watch** (Fahrenheit when the watch uses imperial units, otherwise Celsius), Celsius, Fahrenheit (shown while the temperature can be on screen) |
| | Seconds ticking | **Always** (every second, the default), or after a wrist shake. Shown only while the right box shows seconds; the right box shows the temperature the rest of the time |
| | Seconds duration | 30 s. 5 to 120 in steps of 5: how long the seconds tick after a shake (shown only for "After a wrist shake") |
| Top bezel | Show battery level | On. When off, the left text is shown instead |
| | Left text | `30 DAY BATT` (up to 19 characters, capitals; only shown while the battery level is off) |
| | Show step count | On. When off, the right text is shown instead |
| | Right text | `WR 3ATM` (up to 19 characters, capitals; only shown while the step count is off) |
| Bottom bezel | Show heart rate | Off. When on, the WR badge is replaced by a heart and the latest heart rate |
| | Label | `PEBBLE` (up to 12 characters, capitals; empty for none) |
| Appearance | Case color | Applies with custom colors too. Black, Silver, Charcoal (dotted: dark gray dots on 25% of the black case's pixels), Charcoal (checkerboard: half the pixels dark gray). On the watch's screen the dots blend into a dark charcoal; with Inverted colors the charcoal case sets the black LCD apart |
| | Inverted colors | Off, On (light digits on a dark LCD, like a negative-display watch; the case keeps its color) |
| | Digit style | **7-segment** (the LCD look, with unlit segments), Oxanium, Chakra Petch, Wide (based on Orbitron). A font is used for every text and number on the LCD (weekday, day, indicator labels, time, date, min / max, temperature and seconds), upright, without unlit segments; the top and bottom bars keep their own text. Below zero or from 100 up, the right box leaves out the degree mark |
| | Divider lines | **Solid**, Segmented (dashes), Ruler ticks (a hairline with ticks), Corner brackets (no lines: brackets at the LCD's four corners and at the two boxes' corners beside the divider), HUD chamfer (the line splits into two 45° arms that meet the divider). The LCD window's top and bottom edges follow the same style (only Solid draws the plain LCD window edge band; Corner brackets leaves just the four corners). The bottom row is centred between the divider and the bottom of the LCD |
| | Indicators | **Grid** (the framed 2x2 box), Pills (one rounded pill each: filled when on, a faint dotted outline when off), Active only (just the labels that are on, right-aligned, two to a column), Icons (Bluetooth, a bolt for charging, a moon for Quiet Time, a sun for DST; PM in the bolt's place with the 12-hour leading zero) |
| | Slanted digits | On, Off, 7-segment only (leans the digits like the original; straight digits have sharper edges) |
| | Show unlit segments | On, Off |
| | Backlight color | **System default** (your watch's normal colour), Amber, Warm white, Red, Orange, Yellow, Green, Cyan, Blue, Purple, Pink, **Custom color...** (shows the app's own color picker, the watch's 64 colors) |
| Custom colors | Use custom colors | Off. When on, every part of the face except the case (which keeps its **Case color**) takes the color chosen below, and **Inverted colors** is ignored. The pickers start out as the normal black-on-white theme |
| | Bezels | Top bezel left text, top bezel right text, bottom bezel WR / HR badge, heart rate, label |
| | LCD panel | LCD window edge, LCD background, unlit segments and labels |
| | Weekday and indicators | Weekday, indicator box outline, BT, CHG / FULL, DST, MUTE |
| | Time | Hour digits, colon, minute digits, PM marker |
| | Date and right box | Date / min-max temperature, divider lines, temperature / seconds (including the degree mark) |
| Alerts | Vibrate on phone disconnect | Double pulse (None, Short, Long, Double, Triple, Heartbeat, SOS) |
| | Vibrate on phone reconnect | Short pulse (same patterns) |
| | Vibrate on the hour | Off. A double pulse at the top of every hour while the watch face is showing |

Custom colors: the watch has 64 colors (four levels per channel), so a picked color is rounded to the nearest one. Unlit segments and dots are drawn as a sparse dither of their color (sparser on a dark LCD), so they look paler than the swatch: pick a stronger color for a stronger ghost. The anti-aliased edges of slanted digits are blended between each digit's color and the LCD background, so they stay smooth with any combination. A digit or label with the same color as the LCD background is simply invisible.

There is no vibration during Quiet Time. When the two top-bezel texts are both long, the right
text takes the width it needs and the left one is cut with "..." to fit.

## Building and installing

You need the Pebble command-line tool and SDK (needs Python 3.10 or newer; Python 3.13 via `uv`
is what was used, because a system Python without `ensurepip` cannot create the SDK's virtualenv):

```sh
curl -LsSf https://astral.sh/uv/install.sh | sh          # installs uv
uv tool install pebble-tool --python 3.13
pebble sdk install latest                                # tested with SDK 4.33.1
```

Build (this also installs the JavaScript dependency, `pebble-clay`):

```sh
pebble build          # produces build/<folder name>.pbw
```

The digit-style fonts in `resources/fonts/*.bin` are pre-rendered and committed; only to change
them, re-run `python3 tools/gen_fonts.py Oxanium.ttf ChakraPetch-Bold.ttf Orbitron.ttf` (needs
Pillow; where to get the fonts is in the script).

Run it in the emulator, and take a screenshot:

```sh
pebble install --emulator emery
pebble screenshot --emulator emery --no-open screenshot.png
```

Install on the real watch, from this computer, over Wi-Fi:

1. In the Pebble phone app, switch on **Developer Connection** and note the phone's IP address
   (also shown in the phone's Wi-Fi settings). Keep the app open. The phone and computer must
   be on the same network.
2. Run:

   ```sh
   pebble install --phone <phone-ip>
   ```

   "Connection refused" means Developer Connection is off (it switches off when the app is
   closed or the phone sleeps).

Or copy `build/*.pbw` to the phone and open it with the Pebble app. Allow the location permission
(for weather) and the health permission (for steps and heart rate) when asked.

## Releasing

`pebble build` leaves `build/<folder name>.pbw`. The SDK adds a debugging source map to it, and that
map (and some SDK helper comments in it) contains the path of the SDK on the build computer, which
includes your user name. For a copy you are going to share or publish, run:

```sh
python3 tools/strip_pbw.py --deny YourRealName --deny yourhost   # writes build/release.pbw
```

It drops the map, then scans every remaining file for home-directory paths and for any word you
`--deny`, and exits with an error if it finds one. The name the app shows as its author comes from
`author` in `package.json`; the copyright holder is in `LICENSE`.

## What the emulator can't check

The emulator has no Bluetooth link to a phone app, no coloured backlight, no real steps or heart
rate, and it cannot open the settings page. Those need the real watch: the connect/disconnect
vibrations, the backlight colours, live heart rate, and every control on the settings page.

## How it works

| File | What it is |
|---|---|
| `src/c/main.c` | The watch app: drawing, settings, services |
| `src/c/segments.h` | The seven 7-segment outlines (generated, see below) |
| `src/pkjs/index.js` | Phone side: weather fetch (Open-Meteo) and the settings page |
| `src/pkjs/config.js` | The settings page layout, defaults and choices (Clay) |
| `src/pkjs/custom-clay.js` | Runs on the settings page: hides options that don't apply, Reset button |
| `tools/gen_segments.py` | Regenerates `segments.h` from the 7-Segment font |
| `tools/gen_menu_icon.py` | Redraws the 25x25 menu icon (needs Pillow) |
| `tools/strip_pbw.py` | Makes a release copy of the built app with the debugging map removed, and checks it for identifying text |
| `resources/images/menu_icon.png` | The menu icon: the watch face's thumbnail in the Pebble app's list |
| `package.json` | App metadata, permissions, and the app-message keys |

**Drawing.** Everything on the white LCD panel is rasterized straight into the framebuffer
(`draw_lcd()` in `main.c`), which allows what the normal drawing API can't:

- The digits are polygons (the segment outlines of the font, scaled to each digit's box). They are
  sheared by `LCD_SLANT` (7.5%, measured from a photo of the original) unless the "Slanted digits"
  setting is off, and, with `LCD_AA`, their slanted edges are anti-aliased using the display's dark-gray
  and light-gray shades. A pixel is only ever darkened, so neighbouring segments never eat into each
  other. Upright digits are drawn without smoothing: their edges are vertical and horizontal, so plain
  pixels are sharper than gray-fringed ones.
- Unlit segments and dots ("ghosts") are drawn with an ordered dither: a number of dots out of 16
  (`GHOST_DENSITY`, fewer in the inverted theme). Inactive indicator labels use the same colour.
- The indicator box labels are drawn with the system font and then squashed in the framebuffer to
  10 pixels tall, one letter at a time (the row dropped is from inside each letter, never from a
  horizontal bar, so every bar keeps the same thickness); "BT" is also widened.
- The weekday is a 5x5 dot matrix, upright like the original. The bezel text uses the system fonts.
- Colours come from one place, `apply_theme()`, which fills `s_col[]` with one colour per component
  (`ColorId`: `COL_CASE`, `COL_HOURS`, `COL_BT`, ...). Without custom colours that is the normal (black on
  white) or inverted (white on black) theme, with the case black, silver or charcoal (dark gray dots dithered over black). With "Use custom colors" it is
  whatever the settings page sent (`ColorSettings`, saved under its own persistent key, `COLORS_KEY`, so
  the layout of `Settings` did not change and nobody's saved settings are reset). Drawing functions take the
  colour of their component; the anti-aliasing shades are mixed from it and the LCD colour (`mix_color()`).

**Layout.** All positions are constants in the "Drawing" section of `main.c` (`LCD_*`, `BOX_*`,
`TIME_*`, `ROW3_*`, `RANGE_*`, `TEMP_X`, `BOTTOM_CAP`). There is one function per screen area
(`draw_time`, `draw_date`, `draw_temperature`, `draw_indicator_frame`, `draw_top_bezel`, ...).

**Data flow.** The phone sends the temperature as the `Temp` message, in tenths of a degree Celsius (the watch converts it to the unit shown); the watch asks for a refresh
with `RequestWeather`. Settings from the settings page arrive as messages named after the
`messageKey`s in `config.js`, are copied into the `Settings` struct and saved with `persist_write_data`.
Ticks come once a minute, or every second when seconds are shown (always, or for a chosen time after a wrist shake) and the face is in front (a notification or menu covering it drops back to one a minute). The step count is read on each minute tick, and heart rate redraws the face only when the reading changes.

## Changing things

**Look:** the layout constants above; `LCD_SLANT` (the lean when "Slanted digits" is on); `LCD_AA` (`false`
turns the smoothing of slanted digits off); `GHOST_DENSITY` and `GHOST_DENSITY_INVERTED` (higher =
brighter unlit segments; 8 is a checkerboard); `BT_WIDTHS`, `LABEL_H` and `LABEL_OFF_DENSITY_INVERTED`
for the indicator labels; and the colours themselves in `apply_theme()`.

**Adding a colour:** add an entry to `ColorId`, `COLOR_DEFAULTS` and `color_key()` in `main.c` (same
position in all three), the key to `messageKeys` in `package.json` (then `pebble clean`), a `colorItem(...)`
line to the "Custom colors" section of `config.js`, and use `s_col[COL_...]` where it is drawn. Each colour
costs 11 bytes in the settings message; the inbox is 1024 bytes (`app_message_open` in `init()`).

**Adding a setting** (all five steps are needed):

1. Add the item to `src/pkjs/config.js` with a `messageKey` and a `defaultValue`.
2. Add the same key to `messageKeys` in `package.json`, then run `pebble clean` (the key
   constants are generated at build time and are stale otherwise).
3. Add a field to the `Settings` struct in `main.c` (keep its groups in the order of the settings
   page), its default in `init()`, and read it in `inbox_handler()`. A full Save arrives as one app
   message: it is about 230 bytes with plain text and over 400 with emoji in the custom texts, and
   the watch's inbox is 512 bytes (`app_message_open` in `init()`), so raise that if you add much.
4. Increase `SETTINGS_KEY`. Saved settings from the old layout are then ignored (everyone's settings
   reset once), which is safer than misreading them. The same goes for `WEATHER_KEY` and `Weather`.
5. Use the value where it is drawn or acted on.

**Thumbnail:** the Pebble app's watchface list shows the app's menu icon (`menuIcon` in
`package.json`), including for sideloaded apps. Without one the entry has a blank thumbnail.
To change it, edit `tools/gen_menu_icon.py` and run it.

**Segment shapes:** `segments.h` is generated. To regenerate it:

```sh
curl -sLo /tmp/7segment.ttf https://torinak.com/font/7segment.ttf
python3 tools/gen_segments.py /tmp/7segment.ttf     # needs: pip install fonttools
```

**Checking a change without a watch:** build, install to the emulator and compare screenshots. Any
change that should not alter the picture (a refactor) can be verified by taking the same
screenshots before and after with the time, weather and settings forced to fixed values, and
comparing them pixel by pixel.

## Credits and licences

- The digit shapes are the segment outlines of the **7-Segment** font by **Jan Bobrowski**
  (<https://torinak.com/font/7-segment>), SIL Open Font License 1.1.
- This watch face was made with the help of **Claude**, an AI assistant from Anthropic, which wrote and tested
  much of the code. The design and the decisions are the author's.
- The settings page uses **pebble-clay** (MIT). Weather data is from **Open-Meteo** (CC BY 4.0).
- Casio and W-221H are trademarks of Casio Computer Co., Ltd.; this watch face is an unofficial
  tribute and is not affiliated with Casio or Pebble.

Full details and licence texts: [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md) and [`LICENSES/`](LICENSES/).

This project's own code is released under the [MIT licence](LICENSE). The third-party
parts above keep their own licences.

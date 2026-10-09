// LCD 221: a watch face for the Pebble Time 2 (emery, 200x228) that imitates
// the Casio W-221H: a white "LCD" with 7-segment digits, a dot-matrix weekday and
// the indicators, between a top and a bottom bezel.
//
// Everything on the LCD panel is rasterized straight into the framebuffer, which
// allows things the drawing API can't do: polygon segments snapped to whole pixels,
// anti-aliased font glyphs, and faint "ghost" segments for the unlit parts (a sparse
// ordered dither). Only the bezel text and indicator labels go through the regular
// text API.
//
// File map (search for the "----" section banners):
//   raster primitives  span/fill and the ghost dither
//   7-segment digits   polygon fill, digits (outlines: segments.h)
//   dot-matrix glyphs  weekday letters, PM marker, small symbols
//   data helpers       weather, steps, heart rate, theme, backlight
//   drawing            layout constants, text helpers, one function per screen area
//   services           ticks, battery, Bluetooth vibration, phone messages
//   lifecycle          init / deinit
//
// Settings arrive from the phone (src/pkjs/config.js) as app messages and are
// persisted; bump SETTINGS_KEY whenever the layout of existing Settings fields changes.

#include <pebble.h>
#include "segments.h"

// Persistent storage keys. Each must differ from the others (and from any older value of another).
#define SETTINGS_KEY 19  // bumped whenever Settings changes layout (older saves are then ignored)
#define SETTINGS_KEY_SHARED 18  // a build saved Settings here by mistake, over the colours: see init()
#define WEATHER_KEY 6  // bumped whenever Weather changes layout
#define COLORS_KEY 18  // ColorSettings, persisted apart from Settings so old saves stay valid
_Static_assert(SETTINGS_KEY != COLORS_KEY && SETTINGS_KEY != WEATHER_KEY && WEATHER_KEY != COLORS_KEY,
               "two kinds of saved data share a key");
#define WEATHER_MAX_AGE (3 * 60 * 60)
#define WEATHER_REFRESH_MIN 30

#define BACKLIGHT_SYSTEM 0xFFFFFFFFu  // the user's normal backlight colour
#define BACKLIGHT_CUSTOM 0xFFFFFFFEu  // use backlight_custom

// Vibration patterns selectable for phone connect/disconnect.
typedef enum {
  VIBE_NONE, VIBE_SHORT, VIBE_LONG, VIBE_DOUBLE, VIBE_TRIPLE, VIBE_HEARTBEAT, VIBE_SOS, VIBE_COUNT
} VibeChoice;

// Time format and temperature unit: follow the watch, or force one.
enum { FORMAT_AUTO = 0, FORMAT_24H = 1, FORMAT_12H = 2 };
enum { UNIT_AUTO = 0, UNIT_C = 1, UNIT_F = 2 };

// Digit styles: the 7-segment digits, or a font for every text and number on the LCD.
// (DIGITS_MATRIX: dot-matrix digits like the weekday's letters, drawn like the 7-segment ones.)
enum { DIGITS_SEGMENT = 0, DIGITS_SAIRA = 1, DIGITS_HANDJET = 2, DIGITS_ICEBERG = 3, DIGITS_STENCIL = 4,
       DIGITS_MATRIX = 5, DIGITS_COUNT };
// Divider line styles.
enum { LINES_SOLID = 0, LINES_SEGMENTED = 1, LINES_RULER = 2, LINES_BRACKETS = 3, LINES_HUD = 4, LINES_COUNT };

// The LCD window's top and bottom edges: like the dividers, or a style of their own.
enum { EDGE_MATCH = 0, EDGE_TAB = 1, EDGE_NOTCHED = 2, EDGE_COUNT };

// What marks today's low and high: tall arrows, triangles, or LO / HI.
enum { MARKS_TALL = 0, MARKS_TRIANGLES = 1, MARKS_LOHI = 2, MARKS_COUNT };

// Indicator styles: a pill per indicator, only the active ones, or icons.
enum { IND_PILLS = 0, IND_ACTIVE = 1, IND_ICONS = 2, IND_COUNT };

// Everything the settings page controls, grouped and ordered like the page itself
// (config.js). Saved with persist_write_data: bump SETTINGS_KEY when it changes.
typedef struct {
  // Time & date
  uint8_t time_format;         // FORMAT_AUTO follows the watch's 12/24-hour setting
  bool hour_no_zero;           // 24-hour time has no leading zero (7:05, like the original)
  bool pm_in_box;              // 12-hour time has a leading zero and PM moves into the indicators
  uint8_t range_marks;         // MARKS_TALL, MARKS_TRIANGLES or MARKS_LOHI: what marks the low and the high
  uint8_t temp_unit;           // UNIT_AUTO follows the watch's measurement system
  // Top bezel
  bool show_battery;           // top-left: battery level, else top_left
  char top_left[20];           // top-left text when the battery level is off, upper-cased
  bool show_steps;             // top-right: step count, else top_right
  char top_right[20];          // top-right text when the step count is off, upper-cased
  // Bottom bezel
  char bezel_label[16];        // printed on the bottom bezel, upper-cased
  // Appearance
  bool silver;                 // silver case instead of black
  bool inverted;               // light digits on a dark LCD
  uint8_t digit_style;         // DIGITS_SEGMENT, or one of the pre-rendered fonts
  uint8_t line_style;          // LINES_SOLID, ... (the dividers in the bottom part of the LCD)
  uint8_t edge_style;          // EDGE_MATCH (the divider style's edge) or one of the window edge styles
  uint8_t indicator_style;     // IND_PILLS, IND_ACTIVE or IND_ICONS
  bool ghosts;                 // show faint unlit segments
  uint32_t backlight;          // 0xRRGGBB tint, BACKLIGHT_SYSTEM, or BACKLIGHT_CUSTOM
  uint32_t backlight_custom;   // 0xRRGGBB, from the settings page's colour picker
  // Alerts
  uint8_t vibe_disconnect;     // VibeChoice played when the phone disconnects
  uint8_t vibe_connect;        // VibeChoice played when it reconnects
} Settings;

typedef struct {
  int16_t temp;                // tenths of a degree Celsius
  time_t updated;
  int16_t temp_min, temp_max;  // today's low and high, tenths of a degree Celsius
  uint8_t has_range;           // 1: temp_min/temp_max came with the last reading
} Weather;

// Every part of the face whose colour can be chosen on the settings page ("Custom colors").
// The order is the order of color_key() and COLOR_DEFAULTS below.
typedef enum {
  COL_CASE, COL_TOP_LEFT, COL_TOP_RIGHT, COL_BADGE, COL_HEART, COL_LABEL,  // case and what is printed on it
  COL_EDGE, COL_LCD, COL_UNLIT,                                            // the LCD window
  COL_WEEKDAY, COL_FRAME, COL_BT, COL_CHG, COL_SIG, COL_MUTE,              // top row (COL_FRAME, COL_SIG: unused)
  COL_HOURS, COL_COLON, COL_MINUTES, COL_PM,                               // the time
  COL_DST, COL_DATE, COL_RULES, COL_RIGHT,                                 // bottom row of the LCD
  COL_COUNT
} ColorId;

// The custom colours as chosen on the settings page. Own persistent key, so adding them
// did not change the layout of Settings. Colours are stored as GColor8 values (argb).
typedef struct {
  uint8_t enabled;             // 1: use argb[] instead of the black/silver and inverted themes
  uint8_t argb[COL_COUNT];
} ColorSettings;

// The normal theme (black ink on a white LCD, black case), which is what the settings page
// shows until a colour is changed.
static const uint8_t COLOR_DEFAULTS[COL_COUNT] = {
  [COL_CASE] = GColorBlackARGB8,
  [COL_TOP_LEFT] = GColorWhiteARGB8, [COL_TOP_RIGHT] = GColorWhiteARGB8,
  [COL_BADGE] = GColorWhiteARGB8, [COL_HEART] = GColorWhiteARGB8, [COL_LABEL] = GColorWhiteARGB8,
  [COL_EDGE] = GColorBlackARGB8, [COL_LCD] = GColorWhiteARGB8, [COL_UNLIT] = GColorLightGrayARGB8,
  [COL_WEEKDAY] = GColorBlackARGB8, [COL_FRAME] = GColorBlackARGB8, [COL_BT] = GColorBlackARGB8,
  [COL_CHG] = GColorBlackARGB8, [COL_SIG] = GColorBlackARGB8, [COL_MUTE] = GColorBlackARGB8,
  [COL_HOURS] = GColorBlackARGB8, [COL_COLON] = GColorBlackARGB8, [COL_MINUTES] = GColorBlackARGB8,
  [COL_PM] = GColorBlackARGB8,
  [COL_DST] = GColorBlackARGB8, [COL_DATE] = GColorBlackARGB8, [COL_RULES] = GColorBlackARGB8,
  [COL_RIGHT] = GColorBlackARGB8,
};

// The app-message key of each colour (the same order as ColorId). The SDK declares the
// MESSAGE_KEY_* names as variables, not constants, so this cannot be a static initializer.
static uint32_t color_key(int i) {
  const uint32_t keys[COL_COUNT] = {
    MESSAGE_KEY_ColCase, MESSAGE_KEY_ColTopLeft, MESSAGE_KEY_ColTopRight, MESSAGE_KEY_ColBadge,
    MESSAGE_KEY_ColHeart, MESSAGE_KEY_ColLabel, MESSAGE_KEY_ColEdge, MESSAGE_KEY_ColLcd,
    MESSAGE_KEY_ColUnlit, MESSAGE_KEY_ColWeekday, MESSAGE_KEY_ColFrame, MESSAGE_KEY_ColBt,
    MESSAGE_KEY_ColChg, MESSAGE_KEY_ColSig, MESSAGE_KEY_ColMute, MESSAGE_KEY_ColHours,
    MESSAGE_KEY_ColColon, MESSAGE_KEY_ColMinutes, MESSAGE_KEY_ColPm, MESSAGE_KEY_ColDst,
    MESSAGE_KEY_ColDate, MESSAGE_KEY_ColRules, MESSAGE_KEY_ColRight,
  };
  return keys[i];
}

// (COL_FRAME, the old indicator grid's outline, and COL_SIG, the hourly chime's indicator, are
// no longer drawn; they stay so saved colours keep their places.)

static Window *s_window;
static Layer *s_canvas;
static GBitmap *s_fb;

static Settings s_settings;
static ColorSettings s_colors;
static Weather s_weather;
static time_t s_weather_saved;       // when s_weather was last written to persistent storage
static bool s_quiet;                 // Quiet Time, read once per redraw
static struct tm s_now;
static BatteryChargeState s_battery;
static bool s_connected;
static int s_steps = -1;
static int s_hr = -1;  // latest heart rate in BPM, -1 if unavailable

// Theme (see apply_theme). Normal: black ink on a white LCD. Inverted: white ink
// on a black LCD. Custom colors: whatever the settings page says. Either way every
// component has its own colour in s_col[].
static GColor s_col[COL_COUNT];
static GColor s_lcd;                 // the LCD's background (= s_col[COL_LCD])
static GColor s_ghost;               // unlit segments and dots, drawn dithered (= s_col[COL_UNLIT])
static int s_ghost_density;          // dot density of s_ghost, out of DENSITY_FULL
static int s_label_off_density;      // dot density of inactive indicator labels (drawn in s_ghost)

// ---------------------------------------------------------------------------
// Framebuffer raster primitives

// Dithering: a shape is drawn as a dot pattern whose density is a number out of
// DENSITY_FULL (a pixel is drawn when its 4x4 Bayer threshold is below it), so
// 4 = 25%, 8 = 50% (checkerboard) and 16 = solid.
#define DENSITY_FULL 16
// Unlit segments and dots ("ghosts"). The inverted theme uses fewer dots, because
// light dots on black look brighter than dark dots on white.
#define GHOST_DENSITY 7
#define GHOST_DENSITY_INVERTED 4
// Inactive indicator labels are solid, except when inverted: dark gray is the
// darkest shade the display has, so they are drawn as dots instead.
#define LABEL_OFF_DENSITY_INVERTED 8
static const uint8_t BAYER4[4][4] = {
  { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 },
};

static void span(int y, int x0, int x1, GColor c, bool dither) {
  if (y < 0 || y >= PBL_DISPLAY_HEIGHT) return;
  GBitmapDataRowInfo row = gbitmap_get_data_row_info(s_fb, y);
  if (x0 < row.min_x) x0 = row.min_x;
  if (x1 > row.max_x) x1 = row.max_x;
  for (int x = x0; x <= x1; x++) {
    if (!dither || BAYER4[y & 3][x & 3] < s_ghost_density) row.data[x] = c.argb;
  }
}

static void fill(int x, int y, int w, int h, GColor c, bool dither) {
  for (int r = 0; r < h; r++) span(y + r, x, x + w - 1, c, dither);
}

// ---------------------------------------------------------------------------
// 7-segment digits

enum { SEG_A = 1, SEG_B = 2, SEG_C = 4, SEG_D = 8, SEG_E = 16, SEG_F = 32, SEG_G = 64 };
#define DIGIT_BLANK -1
#define DIGIT_MINUS 10

static const uint8_t DIGIT_SEGS[] = {
  SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F,          // 0
  SEG_B | SEG_C,                                          // 1
  SEG_A | SEG_B | SEG_G | SEG_E | SEG_D,                  // 2
  SEG_A | SEG_B | SEG_G | SEG_C | SEG_D,                  // 3
  SEG_F | SEG_G | SEG_B | SEG_C,                          // 4
  SEG_A | SEG_F | SEG_G | SEG_C | SEG_D,                  // 5
  SEG_A | SEG_F | SEG_G | SEG_E | SEG_C | SEG_D,          // 6
  SEG_A | SEG_B | SEG_C,                                  // 7
  0x7F,                                                   // 8
  SEG_A | SEG_B | SEG_C | SEG_D | SEG_F | SEG_G,          // 9
  SEG_G,                                                  // minus
};

static int floor_div(int32_t a, int32_t b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

// Maps a 0..1000 coordinate to whole pixels so that every bar of a digit
// has the same thickness (plain scaling gives bars of 8 and 9 pixels side by side).
// `knots` are the bar edges in font units and `px` where they land, in pixels.
static int snap_axis(int v, int size, const int16_t *knots, const int *px, int count) {
  int k = 0;
  while (k + 2 < count && v > knots[k + 1]) k++;
  int a = k == 0 ? 0 : px[k], b = px[k + 1];
  int ka = k == 0 ? 0 : knots[k], kb = knots[k + 1];
  int64_t at = a * 1000 + (kb == ka ? 0 : (int64_t)(v - ka) * (b - a) * 1000 / (kb - ka));
  return (int)((at + size / 2) / size);  // font units of the snapped pixel position
}

static void snap_poly(int w, int h, const int16_t *pts, int n, int16_t *out) {
  static const int16_t KX[] = { 0, 243, 756, 1000 };
  static const int16_t KY[] = { 0, 125, 438, 562, 875, 1000 };
  int tv = (244 * w + 500) / 1000, th = (125 * h + 500) / 1000;
  int tg = ((h - th) & 1) ? th - 1 : th;
  int px[4] = { 0, tv, w - tv, w };
  int py[6] = { 0, th, (h - tg) / 2, (h + tg) / 2, h - th, h };
  for (int i = 0; i < n; i++) {
    int xp = snap_axis(pts[2 * i], w, KX, px, 4);
    int yp = snap_axis(pts[2 * i + 1], h, KY, py, 6);
    out[2 * i] = (int16_t)xp;
    out[2 * i + 1] = (int16_t)yp;
  }
}

// The rows (0..h-1) a polygon given in 0..1000 coordinates can touch, with a row of margin:
// segments cover only 12-48% of a digit's height, so most rows need no scanning at all.
static void poly_rows(const int16_t *pts, int n, int h, int *first, int *last) {
  int lo = 1000, hi = 0;
  for (int i = 0; i < n; i++) {
    if (pts[2 * i + 1] < lo) lo = pts[2 * i + 1];
    if (pts[2 * i + 1] > hi) hi = pts[2 * i + 1];
  }
  *first = lo * h / 1000 - 1;
  if (*first < 0) *first = 0;
  *last = hi * h / 1000 + 1;
  if (*last > h - 1) *last = h - 1;
}

// Fills a polygon given in 0..1000 coordinates (up to 8 points), scaled into the box (x, y, w, h)
// with its bars snapped to whole pixels. A pixel is filled when its centre is inside (even-odd rule).
static void fill_poly(int x, int y, int w, int h, const int16_t *pts, int n, GColor c, bool dither) {
  int16_t snapped[16];
  snap_poly(w, h, pts, n, snapped);
  pts = snapped;
  int first, last;
  poly_rows(pts, n, h, &first, &last);
  for (int r = first; r <= last; r++) {
    int32_t yn = (int32_t)(2 * r + 1) * 1000 / (2 * h);
    int32_t xs[8];
    int count = 0;
    for (int i = 0; i < n && count < 8; i++) {
      int32_t x0 = pts[2 * i], y0 = pts[2 * i + 1];
      int32_t x1 = pts[2 * ((i + 1) % n)], y1 = pts[2 * ((i + 1) % n) + 1];
      if ((y0 <= yn) != (y1 <= yn)) xs[count++] = x0 + (x1 - x0) * (yn - y0) / (y1 - y0);
    }
    for (int i = 1; i < count; i++) {
      for (int j = i; j > 0 && xs[j - 1] > xs[j]; j--) {
        int32_t tmp = xs[j]; xs[j] = xs[j - 1]; xs[j - 1] = tmp;
      }
    }
    for (int i = 0; i + 1 < count; i += 2) {
      int c0 = -floor_div(1000 - 2 * xs[i] * w, 2000);   // ceil(px - 0.5)
      int c1 = floor_div(2 * xs[i + 1] * w - 1000, 2000);
      if (c0 <= c1) span(y + r, x + c0, x + c1, c, dither);
    }
  }
}

// `wa` thirds of colour a mixed with the rest of colour b (the display has 4 levels per
// channel). With black on white, wa = 1 gives light gray and wa = 2 gives dark gray.
static GColor mix_color(GColor a, GColor b, int wa) {
  GColor c = GColorBlack;
  c.r = (a.r * wa + b.r * (3 - wa) + 1) / 3;
  c.g = (a.g * wa + b.g * (3 - wa) + 1) / 3;
  c.b = (a.b * wa + b.b * (3 - wa) + 1) / 3;
  return c;
}

// A 7-segment digit built from the segment outlines of the "7-Segment" font by
// Jan Bobrowski (see segments.h), scaled to the box. Only the
// segments in `present` are drawn; unlit ones appear as ghosts (if enabled),
// drawn first so lit segments always win.
static void draw_segments(int x, int y, int w, int h, uint8_t on, uint8_t present, GColor ink) {
  for (int pass = s_settings.ghosts ? 0 : 1; pass <= 1; pass++) {
    uint8_t mask = present & (pass ? on : (uint8_t)~on);
    for (int i = 0; i < 7; i++) {
      if (!(mask & (1 << i))) continue;
      fill_poly(x, y, w, h, SEG_POLYS[i], SEG_POLY_LEN[i], pass ? ink : s_ghost, !pass);
    }
  }
}

static void draw_matrix_digit(int x, int y, int w, int h, int value, GColor ink, bool ghosts);

// A digit (or DIGIT_MINUS, or DIGIT_BLANK) in the box, in the 7-segment or dot-matrix style.
static void draw_digit(int x, int y, int w, int h, int value, GColor ink) {
  if (s_settings.digit_style == DIGITS_MATRIX) {
    draw_matrix_digit(x, y, w, h, value, ink, true);
    return;
  }
  uint8_t on = (value >= 0 && value <= DIGIT_MINUS) ? DIGIT_SEGS[value] : 0;
  draw_segments(x, y, w, h, on, 0x7F, ink);
}

// ---------------------------------------------------------------------------
// Dot-matrix glyphs ('#' = lit)

// Day-of-week letters: 5x5, matching the W-221H's dot-matrix day display.
static const char *day_glyph(char ch) {
  switch (ch) {
    case 'A': return ".###.#...#######...##...#";  // .###. #...# ##### #...# #...#
    case 'D': return "####.#...##...##...#####.";  // ####. #...# #...# #...# ####.
    case 'E': return "######....####.#....#####";  // ##### #.... ####. #.... #####
    case 'F': return "######....####.#....#....";  // ##### #.... ####. #.... #....
    case 'H': return "#...##...#######...##...#";  // #...# #...# ##### #...# #...#
    case 'I': return ".###...#....#....#...###.";  // .###. ..#.. ..#.. ..#.. .###.
    case 'M': return "#...###.###.#.##...##...#";  // #...# ##.## #.#.# #...# #...#
    case 'N': return "#...###..##.#.##..###...#";  // #...# ##..# #.#.# #..## #...#
    case 'O': return ".###.#...##...##...#.###.";  // .###. #...# #...# #...# .###.
    case 'R': return "####.#...#####.#..#.#...#";  // ####. #...# ####. #..#. #...#
    case 'S': return ".#####.....###.....#####.";  // .#### #.... .###. ....# ####.
    case 'T': return "#####..#....#....#....#..";  // ##### ..#.. ..#.. ..#.. ..#..
    case 'U': return "#...##...##...##...#.###.";  // #...# #...# #...# #...# .###.
    case 'W': return "#...##...##.#.###.###...#";  // #...# #...# #.#.# ##.## #...#
    default:  return NULL;
  }
}

// The "P" (PM) marker: an 11x14 LCD symbol with a square-ish bowl (corners cut by a dot)
// and a straight stem.
static const char PM_BITS[] =
  "#########.."
  "###########"
  "###.....###"
  "###.....###"
  "###.....###"
  "###.....###"
  "###########"
  "#########.."
  "###........"
  "###........"
  "###........"
  "###........"
  "###........"
  "###........";

// Draws a grid of `cols` x `rows` dots. With `on` false, the '#' dots are drawn
// as ghosts instead (an unlit fixed LCD symbol).
static void draw_dots(int x, int y, const char *bits, int cols, int rows, int pitch, int dot, bool on,
                      GColor ink) {
  if (!bits || (!on && !s_settings.ghosts)) return;
  for (int r = 0; r < rows; r++) {
    for (int c = 0; c < cols; c++) {
      if (bits[r * cols + c] != '#') continue;
      fill(x + c * pitch, y + r * pitch, dot, dot, on ? ink : s_ghost, !on);
    }
  }
}

// Dot-matrix cell: lit dots solid, the rest as ghosts (if enabled). Dots are
// dot_w x dot_h rectangles on a px x py grid.
static void draw_matrix(int x, int y, const char *bits, int cols, int rows, int px, int py, int dot_w,
                        int dot_h, GColor ink) {
  for (int r = 0; r < rows; r++) {
    for (int c = 0; c < cols; c++) {
      bool lit = bits && bits[r * cols + c] == '#';
      if (!lit && !s_settings.ghosts) continue;
      fill(x + c * px, y + r * py, dot_w, dot_h, lit ? ink : s_ghost, !lit);
    }
  }
}

// Day-of-week text: 5x5 letters with 3x5 dots on a 4x6 grid, 22px apart, leaving room for the
// day of the month after them.
static void draw_day(int x, int y, const char *text, GColor ink) {
  for (int i = 0; text[i]; i++) draw_matrix(x + i * 22, y, day_glyph(text[i]), 5, 5, 4, 6, 3, 5, ink);
}

// The "Dot matrix" digit style: 5x7 digits (the classic dot-matrix display's) for the numbers,
// 5x5 ones beside the weekday, and tiny 3x5 letters for the indicator labels.
static const char *const MATRIX_DIGITS[] = {  // 0-9, then the minus
  ".###.#...##...##...##...##...#.###.", "..#...##....#....#....#....#...###.",
  ".###.#...#....#...#...#...#...#####", ".###.#...#....#..##.....##...#.###.",
  "...#...##..#.#.#..#.#####...#....#.", "######....####.....#....##...#.###.",
  "..##..#...#....####.#...##...#.###.", "#####....#...#...#...#....#....#...",
  ".###.#...##...#.###.#...##...#.###.", ".###.#...##...#.####....#...#..##..",
  "...............#####...............",
};
static const char *const MATRIX_DIGITS_5X5[] = {
  ".###.#...##...##...#.###.", "..#...##....#....#...###.", "####.....#.###.#....#####",
  "####.....#.###.....#####.", "#...##...######....#....#", "######....####.....#####.",
  ".###.#....####.#...#.###.", "#####....#...#...#...#...", ".###.#...#.###.#...#.###.",
  ".###.#...#.####....#.###.",
};
_Static_assert(sizeof(".###.#...##...##...##...##...#.###.") == 36, "5x7 glyphs are 35 dots");

// A 5x7 digit filling the box (dots about two thirds of their spacing wide, nearly as tall as it);
// unlit dots as ghosts unless `ghosts` is false.
static void draw_matrix_digit(int x, int y, int w, int h, int value, GColor ink, bool ghosts) {
  const char *bits = (value >= 0 && value <= DIGIT_MINUS) ? MATRIX_DIGITS[value] : NULL;
  const int px = w / 5, py = h / 7, dw = px - (px >= 6 ? 2 : 1), dh = py - (py >= 8 ? 2 : 1);
  x += (w - (5 * px - (px - dw))) / 2;
  y += (h - (7 * py - (py - dh))) / 2;
  if (ghosts) {
    draw_matrix(x, y, bits, 5, 7, px, py, dw, dh, ink);
    return;
  }
  for (int i = 0; bits && i < 35; i++) {
    if (bits[i] == '#') fill(x + i % 5 * px, y + i / 5 * py, dw, dh, ink, false);
  }
}

// A narrow "1" (3x7) or minus (3 dots) in a sign slot, level with draw_matrix_digit's dots for a
// box `h` tall, `px` px apart.
static void draw_matrix_sign(int x, int y, int h, bool one, int px, GColor ink) {
  static const char ONE[] = ".#.##..#..#..#..#.###";
  const int py = h / 7, dh = py - (py >= 8 ? 2 : 1);
  y += (h - (7 * py - (py - dh))) / 2;
  if (one) draw_matrix(x, y, ONE, 3, 7, px, py, px - 1, dh, ink);
  else draw_matrix(x, y + 3 * py, "###", 3, 1, px, py, px - 1, dh, ink);
}

// ---------------------------------------------------------------------------
// Fonts for the other digit styles: pre-rendered glyphs (tools/gen_fonts.py, resources/fonts)

// The font file's groups, one per screen area (the order of GROUPS in tools/gen_fonts.py).
typedef enum { FG_TIME, FG_WEEKDAY, FG_DATE, FG_RANGE, FG_RIGHT, FG_LABEL, FG_COUNT } FontGroup;
#define DEGREE_CHAR '\xB0'  // the degree mark's code in the font file
enum { ALIGN_LEFT, ALIGN_CENTER, ALIGN_RIGHT };

static uint8_t *s_font;  // the selected font's file, or NULL for the 7-segment digits

static bool font_active(void) { return s_font != NULL; }

static int le16(const uint8_t *p) { return p[0] | p[1] << 8; }

// A font file's glyph height in group `g`.
static int group_height(const uint8_t *f, int g) { return f[4 + 4 * g]; }

// A glyph's table entry: char, width, advance, left bearing (signed), bitmap offset (2 bytes).
static const uint8_t *find_glyph(const uint8_t *f, int g, char ch) {
  const int count = f[4 + 4 * g + 1];
  const uint8_t *table = f + le16(f + 4 + 4 * g + 2);
  for (int i = 0; i < count; i++) {
    if (table[6 * i] == (uint8_t)ch) return table + 6 * i;
  }
  return NULL;
}

static int font_height(FontGroup g) { return group_height(s_font, g); }
static const uint8_t *font_glyph(FontGroup g, char ch) { return find_glyph(s_font, g, ch); }

// Checks a font file before it is used: its header (`groups` groups), and that every table and
// bitmap is inside it.
static bool font_valid(const uint8_t *f, size_t size, int groups) {
  if (size < 4 + 4 * (size_t)groups || f[0] != 'L' || f[1] != 'F' || f[2] != 1 || f[3] != groups) return false;
  for (int g = 0; g < groups; g++) {
    const size_t h = f[4 + 4 * g], count = f[4 + 4 * g + 1], table = le16(f + 4 + 4 * g + 2);
    if (table + 6 * count > size) return false;
    for (size_t i = 0; i < count; i++) {
      const uint8_t *e = f + table + 6 * i;
      if ((size_t)le16(e + 4) + h * ((e[1] * 2 + 7) / 8) > size) return false;
    }
  }
  return true;
}

// A font file from the resources, in memory, or NULL if it can't be loaded.
static uint8_t *load_font(uint32_t id, int groups) {
  ResHandle handle = resource_get_handle(id);
  const size_t size = resource_size(handle);
  uint8_t *buf = malloc(size);
  if (!buf) return NULL;
  if (resource_load(handle, buf, size) != size || !font_valid(buf, size, groups)) {
    free(buf);
    return NULL;
  }
  return buf;
}

// Loads the font of the chosen digit style (only that one is in memory), or none for the
// 7-segment digits. If it can't be loaded, the face falls back to the 7-segment digits.
static void load_digit_font(void) {
  if (s_font) {
    free(s_font);
    s_font = NULL;
  }
  uint32_t id;
  switch (s_settings.digit_style) {
    case DIGITS_SAIRA:   id = RESOURCE_ID_FONT_SAIRA; break;
    case DIGITS_HANDJET: id = RESOURCE_ID_FONT_HANDJET; break;
    case DIGITS_ICEBERG: id = RESOURCE_ID_FONT_ICEBERG; break;
    case DIGITS_STENCIL: id = RESOURCE_ID_FONT_STENCIL; break;
    default: return;
  }
  s_font = load_font(id, FG_COUNT);
}

// Copies a glyph of font `f` with its top-left corner at (x, y). Coverage 3 is the ink and 1-2
// are shades between the ink and the background `bg`. With `density` below DENSITY_FULL only the
// solid part is drawn, as dots (inactive indicator labels).
static void blit_glyph(const uint8_t *f, int g, const uint8_t *e, int x, int y, GColor ink, GColor bg,
                       int density) {
  const int w = e[1], h = group_height(f, g), stride = (w * 2 + 7) / 8;
  const uint8_t *bits = f + le16(e + 4);
  const uint8_t shade[4] = { 0, mix_color(ink, bg, 1).argb, mix_color(ink, bg, 2).argb, ink.argb };
  for (int r = 0; r < h; r++) {
    const int yy = y + r;
    if (yy < 0 || yy >= PBL_DISPLAY_HEIGHT) continue;
    GBitmapDataRowInfo row = gbitmap_get_data_row_info(s_fb, yy);
    for (int c = 0; c < w; c++) {
      const int xx = x + c;
      if (xx < row.min_x || xx > row.max_x) continue;
      const int level = (bits[r * stride + (c >> 2)] >> (6 - 2 * (c & 3))) & 3;
      if (level == 0) continue;
      if (density >= DENSITY_FULL) row.data[xx] = shade[level];
      else if (level >= 2 && BAYER4[yy & 3][xx & 3] < density) row.data[xx] = ink.argb;
    }
  }
}

// A digit-style glyph on the LCD.
static void draw_glyph(FontGroup g, const uint8_t *e, int x, int y, GColor ink, int density) {
  blit_glyph(s_font, g, e, x, y, ink, s_lcd, density);
}

// The width of a text (the sum of its advances); a space is as wide as a '0' (a blank digit).
static int font_text_width(FontGroup g, const char *text) {
  int w = 0;
  for (const char *p = text; *p; p++) {
    const uint8_t *e = font_glyph(g, *p == ' ' ? '0' : *p);
    if (e) w += e[2];
  }
  return w;
}

// Draws a text in the box x..x+w-1, on the baseline `baseline`.
static void font_draw_text(FontGroup g, const char *text, int x, int w, int baseline, int align,
                           GColor ink, int density) {
  if (align != ALIGN_LEFT) {
    const int tw = font_text_width(g, text);
    x += align == ALIGN_RIGHT ? w - tw : (w - tw) / 2;
  }
  const int top = baseline - font_height(g);
  for (const char *p = text; *p; p++) {
    const uint8_t *e = font_glyph(g, *p == ' ' ? '0' : *p);
    if (!e) continue;
    if (*p != ' ') draw_glyph(g, e, x + (int8_t)e[3], top, ink, density);
    x += e[2];
  }
}

// One glyph whose ink is centred on cx (the time's digits, each in its own box).
static void font_draw_centered(FontGroup g, char ch, int cx, int baseline, GColor ink) {
  const uint8_t *e = font_glyph(g, ch);
  if (e) draw_glyph(g, e, cx - e[1] / 2, baseline - font_height(g), ink, DENSITY_FULL);
}

// The bezels' font (Chakra Petch, pre-rendered by tools/gen_fonts.py into bezel.bin): every
// character an upper-cased custom text may use, with capitals 13 px tall, and 11 px for texts
// too long for 13. A glyph's bitmap starts at the top of the capitals and reaches below the
// baseline. A text with any other character (an emoji, an accent) uses the system font.
typedef enum { BG_NORMAL, BG_SMALL, BG_COUNT } BezelGroup;
static const uint8_t BEZEL_CAP[BG_COUNT] = { 13, 11 };
static uint8_t *s_bezel_font;  // NULL if it couldn't be loaded: the system font is used instead

static bool bezel_font_has(BezelGroup g, const char *text) {
  if (!s_bezel_font) return false;
  for (const char *p = text; *p; p++) {
    if (!find_glyph(s_bezel_font, g, *p)) return false;
  }
  return true;
}

static int bezel_text_width(BezelGroup g, const char *text) {
  int w = 0;
  for (const char *p = text; *p; p++) w += find_glyph(s_bezel_font, g, *p)[2];
  return w;
}

// Draws a text with the top of its capitals at `cap_top`, on the case colour.
static void bezel_draw_text(BezelGroup g, const char *text, int x, int cap_top, GColor ink) {
  for (const char *p = text; *p; p++) {
    const uint8_t *e = find_glyph(s_bezel_font, g, *p);
    blit_glyph(s_bezel_font, g, e, x + (int8_t)e[3], cap_top, ink, s_col[COL_CASE], DENSITY_FULL);
    x += e[2];
  }
}

// ---------------------------------------------------------------------------
// Data helpers

static bool weather_valid(void) {
  return s_weather.updated != 0 && time(NULL) - s_weather.updated <= WEATHER_MAX_AGE;
}

// Today's high and low: from a valid reading taken on the current day (they belong to the day
// they were fetched for, so they turn to "--" at midnight until the next reading).
static bool range_valid(void) {
  if (!weather_valid() || !s_weather.has_range) return false;
  const time_t updated = s_weather.updated;
  return localtime(&updated)->tm_yday == s_now.tm_yday;
}

static void update_heart_rate(void) {
#if defined(PBL_HEALTH)
  time_t now = time(NULL);
  HealthServiceAccessibilityMask mask =
      health_service_metric_accessible(HealthMetricHeartRateBPM, now, now);
  HealthValue bpm = (mask & HealthServiceAccessibilityMaskAvailable)
      ? health_service_peek_current_value(HealthMetricHeartRateBPM) : 0;
  s_hr = bpm > 0 ? (int)bpm : -1;
#endif
}

static void update_steps(void) {
#if defined(PBL_HEALTH)
  time_t start = time_start_of_today();
  time_t end = time(NULL);
  HealthServiceAccessibilityMask mask =
      health_service_metric_accessible(HealthMetricStepCount, start, end);
  s_steps = (mask & HealthServiceAccessibilityMaskAvailable)
      ? (int)health_service_sum_today(HealthMetricStepCount) : -1;
#endif
}

// True for colours a "light on dark" treatment suits (the ghosts are fainter on those).
static bool is_dark(GColor c) { return 299 * c.r + 587 * c.g + 114 * c.b < 1500; }

static void apply_theme(void) {
  bool inv = s_settings.inverted;

  // One colour per component: the custom ones, or else the normal or inverted LCD. The case is
  // black or silver either way (Case color).
  if (s_colors.enabled) {
    for (int i = 0; i < COL_COUNT; i++) s_col[i] = (GColor){ .argb = s_colors.argb[i] | 0xC0 };
    s_col[COL_CASE] = s_settings.silver ? GColorLightGray : GColorBlack;
  } else {
    const GColor ink = inv ? GColorWhite : GColorBlack;
    // The case does not depend on the theme.
    const GColor frame = s_settings.silver ? GColorLightGray : GColorBlack;
    const GColor frame_text = s_settings.silver ? GColorBlack : GColorWhite;
    for (int i = 0; i < COL_COUNT; i++) s_col[i] = ink;
    s_col[COL_CASE] = frame;
    s_col[COL_TOP_LEFT] = s_col[COL_TOP_RIGHT] = s_col[COL_BADGE] = frame_text;
    s_col[COL_HEART] = s_col[COL_LABEL] = frame_text;
    s_col[COL_EDGE] = GColorBlack;
    s_col[COL_LCD] = inv ? GColorBlack : GColorWhite;
    s_col[COL_UNLIT] = inv ? GColorDarkGray : GColorLightGray;
  }
  s_lcd = s_col[COL_LCD];
  s_ghost = s_col[COL_UNLIT];

  // Light dots on a dark LCD look brighter than dark dots on a light one, so the unlit
  // segments are sparser there. Inactive indicator labels are solid on a light LCD, dotted on
  // a dark one (dark gray is the darkest shade there is, so dots make it dimmer).
  const bool dark = is_dark(s_lcd);
  s_ghost_density = dark ? GHOST_DENSITY_INVERTED : GHOST_DENSITY;
  s_label_off_density = dark ? LABEL_OFF_DENSITY_INVERTED : DENSITY_FULL;
}

static void apply_backlight(void) {
#if defined(PBL_RGB_BACKLIGHT)
  if (s_settings.backlight == BACKLIGHT_SYSTEM) light_set_system_color();
  else if (s_settings.backlight == BACKLIGHT_CUSTOM) light_set_color_rgb888(s_settings.backlight_custom);
  else light_set_color_rgb888(s_settings.backlight);
#endif
}

// ---------------------------------------------------------------------------
// Drawing

// Screen layout (Pebble Time 2, 200x228): a black case with a white "LCD" panel
// running edge to edge, between a top and a bottom bezel of the same height (BEZEL_H rows
// each, outside the LCD window's 2 px edges).
#define LCD_X 0
#define LCD_Y 24
#define LCD_W 200
#define LCD_H 180
#define BEZEL_H (LCD_Y - 2)
_Static_assert(LCD_Y + LCD_H + 2 + BEZEL_H == PBL_DISPLAY_HEIGHT, "the bezels are not the same height");

// Row 1: weekday and day of the month (left) and the indicators (right), in the rows
// BOX_TOP..BOX_BOTTOM.
#define WEEKDAY_X 6
#define WEEKDAY_Y 35
#define BOX_RIGHT 191   // the indicators' right edge
#define BOX_DIV 152     // the "active only" style's second column ends 2 px left of this
#define BOX_TOP 35      // the indicators' row: top (= weekday top)
#define BOX_BOTTOM 63   // and bottom (= weekday bottom)
#define LABEL_H 10      // indicator label height in rows

// Row 2: the time.
// Proportions measured on the W-221H: the time is ~41% of the LCD height and sits
// just above the rule, and its digits are spread across nearly the whole width: the first digit's box starts about
// 4px from the edge, with the PM marker in the empty left part of that box. (The
// original's marker starts level with the digits and 6px from the edge; here it is
// nudged up and in a little.)
#define TIME_Y 78
#define TIME_W 35
#define TIME_H 70
#define PM_X 7    // the PM marker's distance from the LCD's left edge
#define PM_DY -2  // and how many rows it sits above the top of the digits

// Row 3: today's low and high (left) and the temperature (right), under a 2px rule.
#define ROW3_LINE_Y 156
#define ROW3_DIV_X 126  // vertical divider between the low / high box and the right box
#define ROW3_H 36       // height of the right box's digits
// The right box's digits and the temperature's parts: the sign slot's
// minus, the full-width digit whose right verticals make the "1" of 100+, the two digits and
// the degree mark.
#define RIGHT_DIGIT_W 19
#define TEMP_MINUS_X 130
#define TEMP_ONE_X 121
#define TEMP_D1_X 144
#define TEMP_D2_X 166
#define TEMP_DEG_X 188

// Bezels: the top of the bezel font's capitals at its full size in each bezel, where the icons
// line up.
#define TOP_CAP_Y ((BEZEL_H - 13) / 2)                                      // 13 px capitals
#define BOTTOM_CAP_Y (PBL_DISPLAY_HEIGHT - BEZEL_H + (BEZEL_H - 13) / 2)
#define TOP_RIGHT_MAX 104  // widest the top-right bezel text may be

static const char *const DAYS[] = { "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT" };

static void draw_text(GContext *ctx, const char *text, GRect box, GTextAlignment align, GColor color) {
  graphics_context_set_text_color(ctx, color);
  graphics_draw_text(ctx, text, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD), box,
                     GTextOverflowModeTrailingEllipsis, align, NULL);
}

// A bezel text is drawn in the bezel font when it has all the text's characters, at the size its
// bezel chose (see draw_top_bezel and draw_bottom_bezel), cut with "..." if it still doesn't fit
// its box. Otherwise it is drawn in the system font (Gothic 18 bold), which cuts it the same way.
typedef enum { BAR_TOP, BAR_BOTTOM } Bezel;

// Whether a text fits `w` px at size `g` (a system-font text always counts: it is cut anyway).
static bool bezel_fits(BezelGroup g, const char *text, int w) {
  return !bezel_font_has(g, text) || bezel_text_width(g, text) <= w;
}

// The width a bezel text takes (at most `w`), in whichever font it is drawn in.
static int bezel_width(BezelGroup g, const char *text, int w) {
  if (bezel_font_has(g, text)) {
    const int tw = bezel_text_width(g, text);
    return tw < w ? tw : w;
  }
  return graphics_text_layout_get_content_size(text, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD), GRect(0, 0, w, 30),
                                               GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft).w;
}

// A bezel text in the box x..x+w-1, centred in its bezel's height.
static void draw_bezel_text(GContext *ctx, Bezel bar, BezelGroup g, const char *text, int x, int w,
                            GTextAlignment align, GColor color) {
  if (bezel_font_has(g, text)) {
    char cut[32];
    if (bezel_text_width(g, text) > w) {  // as many characters as fit before "..."
      const int dots = bezel_text_width(g, "...");
      size_t n = 0;
      int tw = 0;
      while (text[n] && n + 4 < sizeof(cut)) {
        const int adv = find_glyph(s_bezel_font, g, text[n])[2];
        if (tw + adv + dots > w) break;
        tw += adv;
        n++;
      }
      while (n > 0 && text[n - 1] == ' ') n--;  // no space before the dots
      memcpy(cut, text, n);
      memcpy(cut + n, "...", 4);
      text = cut;
    }
    const int tw = bezel_text_width(g, text);
    x += align == GTextAlignmentRight ? w - tw : align == GTextAlignmentCenter ? (w - tw) / 2 : 0;
    const int cap_y = (bar == BAR_TOP ? 0 : PBL_DISPLAY_HEIGHT - BEZEL_H) + (BEZEL_H - BEZEL_CAP[g]) / 2;
    if (!(s_fb = graphics_capture_frame_buffer(ctx))) return;
    bezel_draw_text(g, text, x, cap_y, color);
    graphics_release_frame_buffer(ctx, s_fb);
    s_fb = NULL;
    return;
  }
  // The system font's 11 px capitals start 7 px below the box top: centred in the bezel.
  const int top = (bar == BAR_TOP ? 0 : PBL_DISPLAY_HEIGHT - BEZEL_H) + (BEZEL_H - 11) / 2 - 7;
  draw_text(ctx, text, GRect(x, top, w, 22), align, color);
}

// Draws a label so it fits its indicator-box cell. The text is drawn in the
// bold font with its ink starting on the cell's top row, then re-sampled in
// the framebuffer (nearest-neighbour, so edges stay crisp), one letter at a time,
// vertically to `height` rows by dropping rows from inside each letter (see row_to_drop),
// so horizontal bars keep their thickness, and drawn with the ordered dither at `density`
// (out of DENSITY_FULL). The result is centred in `cell` (or right-aligned); only the
// cell's interior is touched.
#define FIT_MAX_W 42  // the widest label cell ("active only") is 42 px,
#define FIT_MAX_H 12  // and none is taller than 12 rows
#define FIT_MAX_LETTERS 8
#define BAR_RUN 4  // a horizontal run of this many ink pixels makes a row part of a bar

typedef uint8_t FitRows[FIT_MAX_H][FIT_MAX_W];

// The longest horizontal run of ink in one row of a letter (columns x0..x1).
static int longest_run(const FitRows buf, int y, int x0, int x1) {
  int best = 0, run = 0;
  for (int x = x0; x <= x1; x++) {
    run = buf[y][x] ? run + 1 : 0;
    if (run > best) best = run;
  }
  return best;
}

// Which of a letter's `n` rows (rows[] holds their indices into buf) to drop when it
// is one row too tall. Never the first or last row, and rows that belong to a
// horizontal bar are avoided (dropping one would thin the bar to 1px while the
// letter's other bars stay 2px). Among the rest, the row most like its neighbour
// wins, then the one nearest the middle of the letter, where a letter is usually
// just vertical strokes.
static int row_to_drop(const FitRows buf, const int *rows, int n, int x0, int x1) {
  int best = n / 2, best_cost = 1 << 30;
  for (int i = 1; i < n - 1; i++) {
    int up = 0, down = 0;
    for (int x = x0; x <= x1; x++) {
      up += buf[rows[i]][x] != buf[rows[i - 1]][x];
      down += buf[rows[i]][x] != buf[rows[i + 1]][x];
    }
    int like = up < down ? up : down;  // differences from the most similar neighbour
    int bar = longest_run(buf, rows[i], x0, x1) >= BAR_RUN ? 1000 : 0;
    int middle = abs(2 * i - (n - 1));
    int cost = bar + like * 10 + middle;
    if (cost < best_cost) { best_cost = cost; best = i; }
  }
  return best;
}

static void draw_fitted_text(GContext *ctx, const char *text, GRect cell, int height, GColor color,
                             int density, bool right) {
  if (color.argb == s_lcd.argb) return;  // invisible, and the ink could not be told from the LCD
  // Gothic 18 bold capitals start 7px below the text box's top.
  GRect box = GRect(cell.origin.x - 1, cell.origin.y - 7, cell.size.w + 2, 22);
  draw_text(ctx, text, box, GTextAlignmentCenter, color);
  GBitmap *fb = graphics_capture_frame_buffer(ctx);
  if (!fb) return;
  int x0 = cell.origin.x, x1 = x0 + cell.size.w - 1;
  int y0 = cell.origin.y, y1 = y0 + cell.size.h - 1;

  // Ink bounding box inside the cell.
  int ix0 = x1, ix1 = x0, iy0 = y1, iy1 = y0;
  for (int y = y0; y <= y1; y++) {
    GBitmapDataRowInfo row = gbitmap_get_data_row_info(fb, y);
    for (int x = x0; x <= x1; x++) {
      if (row.data[x] != color.argb) continue;
      if (x < ix0) ix0 = x;
      if (x > ix1) ix1 = x;
      if (y < iy0) iy0 = y;
      if (y > iy1) iy1 = y;
    }
  }
  int w = ix1 - ix0 + 1, h = iy1 - iy0 + 1;
  if (w <= 0 || h <= 0 || w > FIT_MAX_W || h > FIT_MAX_H) {
    graphics_release_frame_buffer(ctx, fb);
    return;
  }
  static FitRows buf, out;
  for (int y = 0; y < h; y++) {
    GBitmapDataRowInfo row = gbitmap_get_data_row_info(fb, iy0 + y);
    for (int x = 0; x < w; x++) {
      buf[y][x] = row.data[ix0 + x] == color.argb;  // 1 = ink
      row.data[ix0 + x] = s_lcd.argb;
    }
  }

  // Re-sample one letter (a run of columns with ink) at a time into `out`.
  int rows_out = h > height ? height : h;
  int kept = 0, letter = 0, prev_end = 0;
  for (int x = 0; x < w && letter < FIT_MAX_LETTERS;) {
    bool ink = false;
    for (int y = 0; y < h; y++) ink |= buf[y][x];
    if (!ink) { x++; continue; }
    int start = x;
    for (; x < w; x++) {
      bool col = false;
      for (int y = 0; y < h; y++) col |= buf[y][x];
      if (!col) break;
    }
    int len = x - start, last = x - 1;

    // Vertical: drop rows from this letter until it fits.
    int rows[FIT_MAX_H], n = h;
    for (int i = 0; i < h; i++) rows[i] = i;
    while (n > height) {
      int drop = row_to_drop(buf, rows, n, start, last);
      for (int i = drop; i < n - 1; i++) rows[i] = rows[i + 1];
      n--;
    }

    // Horizontal: the letter's width and the text's own spacing are kept.
    const int gap = kept > 0 ? start - prev_end - 1 : 0;
    if (kept + gap + len > FIT_MAX_W) break;
    for (int y = 0; y < rows_out; y++) {
      for (int i = 0; i < gap; i++) out[y][kept + i] = 0;
      for (int i = 0; i < len; i++) out[y][kept + gap + i] = buf[rows[y]][start + i];
    }
    kept += gap + len;
    prev_end = last;
    letter++;
  }

  int ox = right ? x1 + 1 - kept : (x0 + x1 + 1) / 2 - kept / 2, oy = y0 + (cell.size.h - rows_out) / 2;
  for (int y = 0; y < rows_out; y++) {
    GBitmapDataRowInfo row = gbitmap_get_data_row_info(fb, oy + y);
    for (int i = 0; i < kept; i++) {
      bool lit = out[y][i] && BAYER4[(oy + y) & 3][(ox + i) & 3] < density;
      row.data[ox + i] = lit ? color.argb : s_lcd.argb;
    }
  }
  graphics_release_frame_buffer(ctx, fb);
}

static bool is_24h(void) {
  if (s_settings.time_format == FORMAT_AUTO) return clock_is_24h_style();
  return s_settings.time_format == FORMAT_24H;
}

// Imperial measurement system (chosen on the watch) means Fahrenheit when following it.
static bool use_fahrenheit(void) {
  if (s_settings.temp_unit != UNIT_AUTO) return s_settings.temp_unit == UNIT_F;
#if defined(PBL_HEALTH)
  return health_service_get_measurement_system_for_display(HealthMetricWalkedDistanceMeters) ==
         MeasurementSystemImperial;
#else
  return false;
#endif
}

// A temperature in tenths of a degree Celsius, in the unit shown, rounded.
static int display_temp(int t10) {
  if (use_fahrenheit()) {
    int f50 = t10 * 9 + 1600;  // fiftieths of a degree Fahrenheit, rounded only once below
    return (f50 + (f50 >= 0 ? 25 : -25)) / 50;
  }
  return (t10 + (t10 >= 0 ? 5 : -5)) / 10;
}

// The big time, with the PM marker.
static void draw_time(void) {
  bool is24 = is_24h();
  int hour = s_now.tm_hour;
  bool pm = hour >= 12;
  if (!is24) {
    hour %= 12;
    if (hour == 0) hour = 12;
  }
  const int ty = TIME_Y, tw = TIME_W, th = TIME_H;
  // AM/PM only in 12-hour mode: the P beside the hours, or PM in the indicator box (pm_in_box).
  const bool zero12 = !is24 && s_settings.pm_in_box;
  if (!is24 && !zero12) draw_dots(PM_X, ty + PM_DY, PM_BITS, 11, 14, 1, 1, pm, s_col[COL_PM]);
  // Digit boxes (left edges) and the colon, spread like the original's.
  static const int X[4] = { 6, 49, 108, 153 };
  const int colon_x = 93;
  // 12-hour: the first digit is blank or a 1 (the P sits where a 0 would be). 24-hour: the
  // leading zero is optional.
  int tens = (hour >= 10 || (is24 && !s_settings.hour_no_zero) || zero12) ? hour / 10 : DIGIT_BLANK;
  if (font_active()) {
    // Each digit centred in its box, the group centred on the digits' height; the font's colon
    // is centred on the digits too (tools/gen_fonts.py). The glyphs sit FONT_TIME_DX right of
    // the boxes' centres, which centres the time on the screen.
    const int base = ty + (th + font_height(FG_TIME)) / 2;
    enum { FONT_TIME_DX = 3 };
    const int c0 = X[0] + tw / 2 + FONT_TIME_DX, c1 = X[1] + tw / 2 + FONT_TIME_DX;
    const int c2 = X[2] + tw / 2 + FONT_TIME_DX, c3 = X[3] + tw / 2 + FONT_TIME_DX;
    if (tens != DIGIT_BLANK) font_draw_centered(FG_TIME, '0' + tens, c0, base, s_col[COL_HOURS]);
    font_draw_centered(FG_TIME, '0' + hour % 10, c1, base, s_col[COL_HOURS]);
    font_draw_centered(FG_TIME, ':', (c1 + c2) / 2, base, s_col[COL_COLON]);
    font_draw_centered(FG_TIME, '0' + s_now.tm_min / 10, c2, base, s_col[COL_MINUTES]);
    font_draw_centered(FG_TIME, '0' + s_now.tm_min % 10, c3, base, s_col[COL_MINUTES]);
    return;
  }
  if (is24 || zero12) {
    draw_digit(X[0], ty, tw, th, tens, s_col[COL_HOURS]);
  } else if (s_settings.digit_style == DIGITS_MATRIX) {  // just the 1, clear of the PM marker
    draw_matrix_digit(X[0], ty, tw, th, tens, s_col[COL_HOURS], false);
  } else {
    // In 12-hour mode the first digit can only be a 1 (or blank), so it only has
    // the two right-hand segments, as on the real watch. This also keeps the PM
    // marker clear of an unlit digit.
    draw_segments(X[0], ty, tw, th, tens == 1 ? SEG_B | SEG_C : 0, SEG_B | SEG_C, s_col[COL_HOURS]);
  }
  draw_digit(X[1], ty, tw, th, hour % 10, s_col[COL_HOURS]);
  // As on the original, the colon's dots are centred 36% and 70% of the way down the digits.
  const int dot = 7, dot1 = th * 36 / 100, dot2 = th * 70 / 100;  // dot size; centres below the top
  fill(colon_x, ty + dot1 - dot / 2, dot, dot, s_col[COL_COLON], false);
  fill(colon_x, ty + dot2 - dot / 2, dot, dot, s_col[COL_COLON], false);
  draw_digit(X[2], ty, tw, th, s_now.tm_min / 10, s_col[COL_MINUTES]);
  draw_digit(X[3], ty, tw, th, s_now.tm_min % 10, s_col[COL_MINUTES]);
}

// The bottom line of something `h` px tall in the bottom row, centred between the divider line
// and the bottom of the LCD.
static int row3_base(int h) {
  const int top = ROW3_LINE_Y + 2, bottom = LCD_Y + LCD_H - 1;
  return (top + bottom + 1) / 2 + h / 2;
}

// The baseline of the weekday (and the day of the month) in a font: centred on the indicators' row.
static int font_weekday_base(void) {
  return (BOX_TOP + BOX_BOTTOM + font_height(FG_WEEKDAY)) / 2;
}

// The day of the month after the weekday, in 7-segment digits as tall as the weekday letters,
// so the row reads e.g. "MON 05".
static void draw_month_day(void) {
  const int d = s_now.tm_mday, w = 11, h = BOX_BOTTOM - BOX_TOP;
  if (font_active()) {  // in the weekday's font and size, after it (see draw_lcd)
    char text[3] = { '0' + d / 10, '0' + d % 10, 0 };
    font_draw_text(FG_WEEKDAY, text, font_text_width(FG_WEEKDAY, DAYS[s_now.tm_wday]) + WEEKDAY_X + 4,
                   0, font_weekday_base(), ALIGN_LEFT, s_col[COL_WEEKDAY], DENSITY_FULL);
    return;
  }
  if (s_settings.digit_style == DIGITS_MATRIX) {  // the weekday letters' 5x5 dots, a little narrower
    draw_matrix(74, WEEKDAY_Y, MATRIX_DIGITS_5X5[d / 10], 5, 5, 3, 6, 2, 5, s_col[COL_WEEKDAY]);
    draw_matrix(91, WEEKDAY_Y, MATRIX_DIGITS_5X5[d % 10], 5, 5, 3, 6, 2, 5, s_col[COL_WEEKDAY]);
    return;
  }
  draw_digit(75, WEEKDAY_Y, w, h, d / 10, s_col[COL_WEEKDAY]);  // with a leading zero (05)
  draw_digit(89, WEEKDAY_Y, w, h, d % 10, s_col[COL_WEEKDAY]);
}

// The right box: the temperature. A half-width sign slot (minus, or the
// "1" of 100+ in Fahrenheit) followed by two digits, so -99..199 all fit; "--"
// while there is no recent weather.
static void draw_temperature(void) {
  const GColor ink = s_col[COL_RIGHT];
  const int dy = row3_base(ROW3_H) - ROW3_H, dh = ROW3_H;
  int temp = display_temp(s_weather.temp), v = abs(temp);
  bool valid = weather_valid();
  bool neg = valid && temp < 0, hundred = valid && v >= 100;
  if (font_active()) {
    // Centred in the box. Below zero or from 100 up there is no degree mark, so the font can stay
    // as big as two digits and a degree mark allow (tools/gen_fonts.py).
    char text[8];
    if (!valid) snprintf(text, sizeof(text), "--%c", DEGREE_CHAR);
    else if (neg || hundred) snprintf(text, sizeof(text), "%d", temp);
    else snprintf(text, sizeof(text), "%d%c", temp, DEGREE_CHAR);
    font_draw_text(FG_RIGHT, text, ROW3_DIV_X + 2, LCD_W - ROW3_DIV_X - 2, row3_base(font_height(FG_RIGHT)),
                   ALIGN_CENTER, ink, DENSITY_FULL);
    return;
  }
  // The minus is the font's middle bar in a narrow box; the "1" is the right
  // verticals of a full-width digit placed so they land in the sign slot.
  // Unlit parts go first so the lit one is never covered by a ghost.
  const int w = RIGHT_DIGIT_W;
  if (s_settings.digit_style == DIGITS_MATRIX) {  // the minus or the 1, without ghosts
    if (neg) draw_matrix_sign(TEMP_MINUS_X + 2, dy, dh, false, 3, ink);
    if (hundred) draw_matrix_sign(TEMP_MINUS_X + 3, dy, dh, true, 3, ink);
  } else {
    if (!neg) draw_segments(TEMP_MINUS_X, dy, 13, dh, 0, SEG_G, ink);
    if (!hundred) draw_segments(TEMP_ONE_X, dy, w, dh, 0, SEG_B | SEG_C, ink);
    if (neg) draw_segments(TEMP_MINUS_X, dy, 13, dh, SEG_G, SEG_G, ink);
    if (hundred) draw_segments(TEMP_ONE_X, dy, w, dh, SEG_B | SEG_C, SEG_B | SEG_C, ink);
  }
  if (!valid) {
    draw_digit(TEMP_D1_X, dy, w, dh, DIGIT_MINUS, ink);
    draw_digit(TEMP_D2_X, dy, w, dh, DIGIT_MINUS, ink);
  } else {
    int tens = (v >= 10) ? v / 10 % 10 : DIGIT_BLANK;
    draw_digit(TEMP_D1_X, dy, w, dh, tens, ink);
    draw_digit(TEMP_D2_X, dy, w, dh, v % 10, ink);
  }
  // Degree mark: a round 7x7 ring, 2px thick.
  static const char DEGREE_BITS[] =
    "..###.."
    ".#####."
    "##...##"
    "##...##"
    "##...##"
    ".#####."
    "..###..";
  draw_dots(TEMP_DEG_X, dy, DEGREE_BITS, 7, 7, 1, 1, true, ink);
}

// Today's low and high side by side, centred in the
// bottom row, with a short divider between them. Each is a mark (a tall arrow, a triangle, or
// LO / HI: range_marks), a narrow slot for the "1" of 100+ and two digits; no degree mark (the
// right box's temperature has one, and the space goes to bigger digits). Below zero, the minus
// goes before the number (above the triangle with triangles).
#define RANGE_DIGIT_W 15
#define RANGE_DIGIT_H 30
#define RANGE_LOW_X 2     // left edge of the low's arrow
#define RANGE_HIGH_X 66   // and of the high's
#define RANGE_DIV_X 61    // the divider between them
// A filled vertical arrow, its tip up or down, centred on cx: `total` rows, of which `head` are the
// head (widening to `head_w`) and the rest a `shaft` px wide shaft.
static void draw_arrow(int cx, int top, bool up, int total, int head, int head_w, int shaft, GColor ink) {
  for (int i = 0; i < total; i++) {
    const int y = up ? top + i : top + total - 1 - i;  // i = 0 at the tip
    const int half = i < head ? (head_w - 1) * i / (2 * (head - 1)) : -1;
    if (half >= 0) fill(cx - half, y, 2 * half + 1, 1, ink, false);
    else fill(cx - (shaft - 1) / 2, y, shaft, 1, ink, false);
  }
}

// LO / HI in solid 3x5 dot-matrix letters (2x2 px dots), for the 7-segment style.
static void draw_small_letters(int x, int y, const char *text, GColor ink) {
  static const char *const L[] = { "#..", "#..", "#..", "#..", "###" };
  static const char *const O[] = { "###", "#.#", "#.#", "#.#", "###" };
  static const char *const H[] = { "#.#", "#.#", "###", "#.#", "#.#" };
  static const char *const I[] = { "###", ".#.", ".#.", ".#.", "###" };
  for (int k = 0; text[k]; k++) {
    const char *const *g = text[k] == 'L' ? L : text[k] == 'O' ? O : text[k] == 'H' ? H : I;
    for (int r = 0; r < 5; r++) {
      for (int c = 0; c < 3; c++) {
        if (g[r][c] == '#') fill(x + 8 * k + 2 * c, y + 2 * r, 2, 2, ink, false);
      }
    }
  }
}

static void draw_range_value(int x, int t10, bool valid, bool high, GColor ink) {
  const int h = font_active() ? font_height(FG_RANGE) : RANGE_DIGIT_H, w = RANGE_DIGIT_W;
  const int y = row3_base(h) - h, cy = y + h / 2;  // the digits' top and middle
  const int d1 = x + 20, d2 = x + 37;  // the digits
  int temp = display_temp(t10), v = abs(temp);
  bool neg = valid && temp < 0, hundred = valid && v >= 100;
  // The marks sit level with the middle of the digits.
  bool minus_before = true;  // the minus goes before the number, except above the triangles
  switch (s_settings.range_marks) {
    case MARKS_TRIANGLES: {  // a 15x8 triangle, with a below-zero minus above it
      const int ay = cy - 4;
      draw_arrow(x + 7, ay, high, 8, 8, 15, 0, ink);
      if (neg) fill(x + 2, ay - 9, 11, 3, ink, false);
      minus_before = false;
      break;
    }
    case MARKS_LOHI:
      if (font_active()) {
        font_draw_text(FG_LABEL, high ? "HI" : "LO", x, 0, cy + font_height(FG_LABEL) / 2, ALIGN_LEFT, ink,
                       DENSITY_FULL);
      } else {
        draw_small_letters(x, cy - 5, high ? "HI" : "LO", ink);
      }
      break;
    default:  // a tall arrow: 26 px, a 7 px head 11 px wide and a 2 px shaft
      draw_arrow(x + 5, cy - 13, high, 26, 7, 11, 2, ink);
  }
  if (font_active()) {
    // Right-aligned after the mark, with a below-zero minus as a short bar just before the digits
    // (the fonts' own minus is wide). A long number (below -9, or 100 and up) may use up to 5 px
    // more on the right rather than run into the mark.
    char text[6];
    if (valid) snprintf(text, sizeof(text), "%d", v);
    else snprintf(text, sizeof(text), "--");
    const bool bar = neg && minus_before;
    const int mark_end = s_settings.range_marks == MARKS_LOHI ? x + font_text_width(FG_LABEL, "LO") + 2 : x + 12;
    int right = x + 52;
    const int start = right - font_text_width(FG_RANGE, text) - (bar ? 7 : 0);
    if (start < mark_end) right += mark_end - start < 5 ? mark_end - start : 5;
    font_draw_text(FG_RANGE, text, right - 40, 40, y + h, ALIGN_RIGHT, ink, DENSITY_FULL);
    if (bar) fill(right - font_text_width(FG_RANGE, text) - 6, cy - 1, 5, 3, ink, false);
    return;
  }
  // The "1" of 100+: the right verticals of a digit placed so they land between the mark and
  // the first digit. No unlit ghost at this size: it would crowd the digits.
  if (hundred && s_settings.digit_style == DIGITS_MATRIX) draw_matrix_sign(x + 13, y, h, true, 2, ink);
  else if (hundred) draw_segments(x + 3, y, w, h, SEG_B | SEG_C, SEG_B | SEG_C, ink);
  if (!valid) {
    draw_digit(d1, y, w, h, DIGIT_MINUS, ink);
    draw_digit(d2, y, w, h, DIGIT_MINUS, ink);
    return;
  }
  // Below zero, before the number: in the empty tens place, or as a short bar before it.
  const bool minus_digit = neg && minus_before && v < 10;
  draw_digit(d1, y, w, h, minus_digit ? DIGIT_MINUS : (v >= 10) ? v / 10 % 10 : DIGIT_BLANK, ink);
  draw_digit(d2, y, w, h, v % 10, ink);
  if (neg && minus_before && v >= 10) {  // (clear of LO / HI, which is wider than an arrow)
    if (s_settings.range_marks == MARKS_LOHI) fill(x + 15, cy - 1, 4, 3, ink, false);
    else fill(x + 13, cy - 1, 6, 3, ink, false);
  }
}

// The short divider between the low and the high (rows top..bottom), in the divider line style.
static void draw_range_divider(int x, int top, int bottom) {
  const GColor c = s_col[COL_RULES];
  switch (s_settings.line_style) {
    case LINES_SEGMENTED:
      for (int y = top; y <= bottom; y += 4) fill(x, y, 1, 2, c, false);
      break;
    case LINES_RULER:  // a line with a dot at each end
      fill(x, top + 4, 1, bottom - top - 7, c, false);
      fill(x, top + 2, 1, 1, c, false);
      fill(x, bottom - 2, 1, 1, c, false);
      break;
    case LINES_BRACKETS:  // fainter, so the corners frame the box
      fill(x, top + 6, 1, bottom - top - 11, mix_color(c, s_lcd, 2), false);
      break;
    case LINES_HUD:  // a small T on top
      fill(x, top + 3, 1, bottom - top - 5, c, false);
      fill(x - 1, top + 2, 3, 1, c, false);
      break;
    default:
      fill(x, top, 1, bottom - top + 1, c, false);
  }
}

static void draw_temperature_range(void) {
  const bool valid = range_valid();
  draw_range_value(RANGE_LOW_X, s_weather.temp_min, valid, false, s_col[COL_DATE]);
  draw_range_value(RANGE_HIGH_X, s_weather.temp_max, valid, true, s_col[COL_DATE]);
  const int base = row3_base(RANGE_DIGIT_H);
  draw_range_divider(RANGE_DIV_X, base - RANGE_DIGIT_H - 2, base + 1);
}

// Viewfinder corners of the box x0..x1, y0..y1 (the ones in `corners`): arms `a` px long, `t` thick.
enum { CORNER_TL = 1, CORNER_TR = 2, CORNER_BL = 4, CORNER_BR = 8 };
static void draw_brackets(int x0, int x1, int y0, int y1, int corners, int a, int t, GColor c) {
  if (corners & CORNER_TL) { fill(x0, y0, a, t, c, false); fill(x0, y0, t, a, c, false); }
  if (corners & CORNER_TR) { fill(x1 - a + 1, y0, a, t, c, false); fill(x1 - t + 1, y0, t, a, c, false); }
  if (corners & CORNER_BL) { fill(x0, y1 - t + 1, a, t, c, false); fill(x0, y1 - a + 1, t, a, c, false); }
  if (corners & CORNER_BR) {
    fill(x1 - a + 1, y1 - t + 1, a, t, c, false);
    fill(x1 - t + 1, y1 - a + 1, t, a, c, false);
  }
}

// The lines between the time and the bottom row and between its two boxes, in
// the chosen style.
static void draw_rules(void) {
  const GColor c = s_col[COL_RULES];
  const int y0 = ROW3_LINE_Y, yb = LCD_Y + LCD_H - 1, xd = ROW3_DIV_X;
  switch (s_settings.line_style) {
    case LINES_SEGMENTED:  // dashes, like an instrument display
      for (int x = 2; x < LCD_W - 2; x += 9) fill(x, y0, x + 6 > LCD_W - 2 ? LCD_W - 2 - x : 6, 2, c, false);
      for (int y = y0 + 4; y <= yb; y += 7) fill(xd, y, 2, y + 4 > yb + 1 ? yb + 1 - y : 4, c, false);
      break;
    case LINES_RULER:  // a hairline with ruler ticks, a longer one every 32 px
      fill(LCD_X, y0, LCD_W, 1, c, false);
      for (int x = 4; x < LCD_W; x += 8) fill(x, y0 + 1, 1, x % 32 == 4 ? 3 : 1, c, false);
      fill(xd, y0, 1, yb - y0 + 1, c, false);
      for (int y = y0 + 8; y < yb; y += 8) fill(xd + 1, y, 2, 1, c, false);
      break;
    case LINES_BRACKETS: {  // no lines: each box's four corners, except the two at the bottom
                            // outer ends while the LCD's own corner brackets are there (the
                            // matching window edge), 2 px below them
      const int outer_bottom = s_settings.edge_style == EDGE_MATCH ? 0 : CORNER_BL | CORNER_BR;
      draw_brackets(2, xd - 3, y0 + 1, yb - 1, (CORNER_TL | CORNER_TR | CORNER_BR) | (outer_bottom & CORNER_BL),
                    8, 1, c);
      draw_brackets(xd + 4, LCD_W - 3, y0 + 1, yb - 1, (CORNER_TL | CORNER_TR | CORNER_BL) | (outer_bottom & CORNER_BR),
                    8, 1, c);
      break;
    }
    case LINES_HUD: {  // the line splits into two 45-degree arms that meet the divider, with angled tips
      const int arm = 12;
      fill(6, y0, xd - arm - 6 + 1, 2, c, false);
      fill(xd + arm, y0, LCD_W - 7 - (xd + arm) + 1, 2, c, false);
      for (int i = 0; i < arm; i++) {
        fill(xd - arm + i, y0 + i, 2, 1, c, false);
        fill(xd + arm - i, y0 + i, 2, 1, c, false);
      }
      fill(xd, y0 + arm - 1, 2, yb - y0 - arm + 2, c, false);
      for (int i = 0; i <= 4; i++) {
        fill(6 - i, y0 + i, 2, 1, c, false);
        fill(LCD_W - 7 + i, y0 + i, 2, 1, c, false);
      }
      break;
    }
    default:
      fill(LCD_X, y0, LCD_W, 2, c, false);
      fill(xd, y0, 2, yb - y0 + 1, c, false);
  }
}

// The window edge styles of their own, on both edges (`top` and `bottom` are the 2 px bands' first
// rows). Outward means into the case, inward into the LCD.
static void draw_lcd_edge_style(GColor c, int top, int bottom) {
  switch (s_settings.edge_style) {
    case EDGE_TAB: {  // a 2 px line that steps 3 px out into the case around a raised centre section
      const int x0 = 66, x1 = LCD_W - 66, rise = 3;
      for (int e = 0; e < 2; e++) {
        const int y = e ? bottom : top, out = e ? 1 : -1;  // out: away from the LCD
        fill(LCD_X, y, x0 - LCD_X, 2, c, false);
        fill(x1, y, LCD_W - x1, 2, c, false);
        fill(x0 + rise, y + out * rise, x1 - x0 - 2 * rise, 2, c, false);
        for (int i = 0; i <= rise; i++) {
          fill(x0 + i, y + out * i, 2, 2, c, false);
          fill(x1 - 2 - i, y + out * i, 2, 2, c, false);
        }
      }
      break;
    }
    case EDGE_NOTCHED:  // a 2 px line with three small gaps, its ends bent 45 degrees into the LCD
      for (int e = 0; e < 2; e++) {
        const int y = e ? bottom : top, in = e ? -1 : 1;
        int x = 6;
        static const uint8_t GAPS[] = { 48, 98, 148 };  // each gap is 4 px
        for (unsigned g = 0; g <= ARRAY_LENGTH(GAPS); g++) {
          const int end = g < ARRAY_LENGTH(GAPS) ? GAPS[g] : LCD_W - 6;
          fill(x, y, end - x, 2, c, false);
          x = end + 4;
        }
        for (int i = 1; i <= 4; i++) {
          fill(6 - i, y + in * i + (e ? 1 : 0), 2, 1, c, false);
          fill(LCD_W - 7 + i, y + in * i + (e ? 1 : 0), 2, 1, c, false);
        }
      }
      break;
    default:
      break;
  }
}

// The LCD window's top and bottom edges (the 2px bands between it and the case) in the divider
// line style, in the dividers' colour. Solid keeps the plain edge drawn in canvas_update.
static void draw_lcd_edges(void) {
  // The window edge colour with custom colours; otherwise the dividers' colour (the plain theme's
  // edge is black, which a black case would hide).
  const GColor c = s_colors.enabled ? s_col[COL_EDGE] : s_col[COL_RULES];
  const int top = LCD_Y - 2, bottom = LCD_Y + LCD_H;  // each band is 2 rows
  if (s_settings.edge_style != EDGE_MATCH) {
    draw_lcd_edge_style(c, top, bottom);
    return;
  }
  switch (s_settings.line_style) {
    case LINES_SEGMENTED:
      for (int x = 2; x < LCD_W - 2; x += 9) {
        const int w = x + 6 > LCD_W - 2 ? LCD_W - 2 - x : 6;
        fill(x, top, w, 2, c, false);
        fill(x, bottom, w, 2, c, false);
      }
      break;
    case LINES_RULER:  // 2 px lines, with ticks pointing into the LCD
      fill(LCD_X, top, LCD_W, 2, c, false);
      fill(LCD_X, bottom, LCD_W, 2, c, false);
      for (int x = 4; x < LCD_W; x += 8) {
        const int len = x % 32 == 4 ? 5 : 2;
        fill(x, top + 2, 1, len, c, false);
        fill(x, bottom - len, 1, len, c, false);
      }
      break;
    case LINES_BRACKETS:  // only the LCD's four corners, bolder than the bottom row's
      draw_brackets(LCD_X, LCD_X + LCD_W - 1, top, bottom + 1, CORNER_TL | CORNER_TR | CORNER_BL | CORNER_BR,
                    14, 2, c);
      break;
    case LINES_HUD:  // lines with 45-degree tips bent into the LCD, like the main divider's ends
      fill(6, top, LCD_W - 12, 2, c, false);
      fill(6, bottom, LCD_W - 12, 2, c, false);
      for (int i = 1; i <= 4; i++) {
        fill(6 - i, top + i, 2, 1, c, false);
        fill(LCD_W - 7 + i, top + i, 2, 1, c, false);
        fill(6 - i, bottom + 1 - i, 2, 1, c, false);
        fill(LCD_W - 7 + i, bottom + 1 - i, 2, 1, c, false);
      }
      break;
    default:
      break;
  }
}

// Everything on the white LCD panel, drawn straight into the framebuffer.
static void draw_lcd(void) {
  draw_lcd_edges();
  if (font_active()) {  // centred on the indicators' row (2 px left: the letters have side bearings)
    font_draw_text(FG_WEEKDAY, DAYS[s_now.tm_wday], WEEKDAY_X - 2, 0, font_weekday_base(), ALIGN_LEFT,
                   s_col[COL_WEEKDAY], DENSITY_FULL);
  } else {
    draw_day(WEEKDAY_X, WEEKDAY_Y, DAYS[s_now.tm_wday], s_col[COL_WEEKDAY]);
  }
  draw_month_day();
  draw_time();
  draw_rules();
  draw_temperature_range();
  draw_temperature();
}

// A small 1-bit picture ('#' = ink), drawn pixel by pixel through the graphics API.
static void draw_icon(GContext *ctx, int x, int y, const char *const *rows, int n, GColor color) {
  graphics_context_set_fill_color(ctx, color);
  for (int r = 0; r < n; r++) {
    for (int c = 0; rows[r][c]; c++) {
      if (rows[r][c] == '#') graphics_fill_rect(ctx, GRect(x + c, y + r, 1, 1), 0, GCornerNone);
    }
  }
}

// The battery icon: an 18x10 outline with a terminal, filled to the charge level, with a bolt
// while charging (cut out of the fill, or drawn in the ink when the fill is too short for it).
static void draw_battery_icon(GContext *ctx, int x, int y, GColor ink) {
  static const char *const BOLT[] = { "..##", ".##.", "####", ".##.", "##.." };
  const int pct = s_battery.charge_percent, fill = (14 * pct + 50) / 100;
  graphics_context_set_stroke_color(ctx, ink);
  graphics_draw_rect(ctx, GRect(x, y, 18, 10));
  graphics_context_set_fill_color(ctx, ink);
  graphics_fill_rect(ctx, GRect(x + 18, y + 3, 2, 4), 0, GCornerNone);
  if (fill > 0) graphics_fill_rect(ctx, GRect(x + 2, y + 2, fill, 6), 0, GCornerNone);
  if (s_battery.is_charging) draw_icon(ctx, x + 7, y + 2, BOLT, 5, fill > 6 ? s_col[COL_CASE] : ink);
}

// Top bezel: a battery icon and the level, and footprints and the step
// count; or the custom texts when those are switched off. The right text takes the width it
// needs (up to TOP_RIGHT_MAX) and the left text gets the rest.
static void draw_top_bezel(GContext *ctx) {
  static const char *const FEET[] = {  // two footprints (sole and heel), the right one ahead
    "........###.", ".......#####", ".......#####", ".......#####", ".###...#####", "#####...###.",
    "#####.......", "#####...###.", "#####...###.", ".###........", "............", ".###........",
    ".###........",
  };
  enum { ICON_GAP = 4, BATTERY_W = 20, FEET_W = 12 };
  char left[24], right[24];
  if (s_settings.show_battery) {
    snprintf(left, sizeof(left), "%d", s_battery.charge_percent);  // (the icon says it's a percentage)
  } else {
    snprintf(left, sizeof(left), "%s", s_settings.top_left);
  }
  if (!s_settings.show_steps) {
    snprintf(right, sizeof(right), "%s", s_settings.top_right);
  } else if (s_steps >= 10000) {  // in thousands, rounded down: 12K
    snprintf(right, sizeof(right), "%dK", s_steps / 1000);
  } else if (s_steps >= 1000) {  // with one decimal: 5.2K
    snprintf(right, sizeof(right), "%d.%dK", s_steps / 1000, s_steps % 1000 / 100);
  } else if (s_steps >= 0) {
    snprintf(right, sizeof(right), "%d", s_steps);
  } else {
    snprintf(right, sizeof(right), "--");
  }
  // The battery level and the step count are 11 px tall, short enough to stay clear of the
  // Center tab window edge's raised middle. The custom texts are 13 px, or both 11 px when the
  // left one doesn't fit beside the right one.
  const int left_x = 10 + (s_settings.show_battery ? BATTERY_W + ICON_GAP : 0);
  BezelGroup g = BG_NORMAL, gl, gr;
  int right_w, right_start;
  for (;;) {
    gl = s_settings.show_battery ? BG_SMALL : g;
    gr = s_settings.show_steps ? BG_SMALL : g;
    right_w = bezel_width(gr, right, TOP_RIGHT_MAX - 4) + 4;
    if (right_w > TOP_RIGHT_MAX) right_w = TOP_RIGHT_MAX;
    right_start = 190 - right_w - (s_settings.show_steps ? FEET_W + ICON_GAP : 0);
    if (g == BG_SMALL || bezel_fits(gl, left, right_start - left_x - 4)) break;
    g = BG_SMALL;
  }
  draw_bezel_text(ctx, BAR_TOP, gr, right, 190 - right_w, right_w, GTextAlignmentRight, s_col[COL_TOP_RIGHT]);
  if (s_settings.show_steps) {  // the footprints just left of the number
    draw_icon(ctx, right_start + 2, TOP_CAP_Y, FEET, ARRAY_LENGTH(FEET), s_col[COL_TOP_RIGHT]);
  }
  if (s_settings.show_battery) draw_battery_icon(ctx, 10, TOP_CAP_Y + 1, s_col[COL_TOP_LEFT]);
  draw_bezel_text(ctx, BAR_TOP, gl, left, left_x, right_start - left_x - 4, GTextAlignmentLeft, s_col[COL_TOP_LEFT]);
}

// Bottom bezel: a heart and the latest heart rate, and the custom label.
static void draw_bottom_bezel(GContext *ctx) {
  static const char *const HEART[] = {
    "..###...###..", ".#####.#####.", "#############", "#############", "#############",
    ".###########.", "..#########..", "...#######...", "....#####....", ".....###.....", "......#......",
  };
  draw_icon(ctx, 12, BOTTOM_CAP_Y + 1, HEART, ARRAY_LENGTH(HEART), s_col[COL_BADGE]);
  char hr_text[12];
  if (s_hr > 0) snprintf(hr_text, sizeof(hr_text), "%d", s_hr);
  else snprintf(hr_text, sizeof(hr_text), "--");
  // Both texts 13 px tall, or both 11 px when the label doesn't fit at 13.
  const BezelGroup g = bezel_fits(BG_NORMAL, s_settings.bezel_label, 120) ? BG_NORMAL : BG_SMALL;
  draw_bezel_text(ctx, BAR_BOTTOM, g, hr_text, 30, 40, GTextAlignmentLeft, s_col[COL_HEART]);
  draw_bezel_text(ctx, BAR_BOTTOM, g, s_settings.bezel_label, 70, 120, GTextAlignmentRight, s_col[COL_LABEL]);
}

// One indicator: its label, whether it is on, and its colour when on.
typedef struct {
  const char *label;
  bool on;
  GColor ink;
} Indicator;

// The top-left corner of the area the indicator styles share.
#define IND_X0 107
#define IND_Y0 BOX_TOP

// The "Dot matrix" style's indicator labels: 3x5 letters of 2x2 px dots (10 px tall), 2 px apart.
static const char *tiny_letter(char ch) {
  switch (ch) {
    case 'B': return "##.#.###.#.###.";
    case 'C': return "####..#..#..###";
    case 'D': return "##.#.##.##.###.";
    case 'E': return "####..####..###";
    case 'F': return "####..####..#..";
    case 'G': return "####..#.##.####";
    case 'H': return "#.##.#####.##.#";
    case 'L': return "#..#..#..#..###";
    case 'M': return "#.########.##.#";
    case 'P': return "####.#####..#..";
    case 'S': return "####..###..####";
    case 'T': return "###.#..#..#..#.";
    case 'U': return "#.##.##.##.####";
    default:  return NULL;
  }
}

static void draw_tiny_label(const char *text, GRect cell, bool right, GColor color, int density) {
  const int tw = 8 * (int)strlen(text) - 2;
  int x = right ? cell.origin.x + cell.size.w - tw : cell.origin.x + (cell.size.w - tw) / 2;
  const int y = cell.origin.y + (cell.size.h - 10) / 2;
  for (; *text; text++, x += 8) {
    const char *bits = tiny_letter(*text);
    for (int i = 0; bits && i < 15; i++) {
      if (bits[i] != '#') continue;
      for (int k = 0; k < 4; k++) {  // the dot's 4 pixels, dithered when off
        const int px = x + i % 3 * 2 + k % 2, py = y + i / 3 * 2 + k / 2;
        if (density >= DENSITY_FULL || BAYER4[py & 3][px & 3] < density) fill(px, py, 1, 1, color, false);
      }
    }
  }
}

// An indicator's label in `cell`: lit, or faint dots when off. The system font fitted to the
// cell, or the digit style's font.
static void draw_indicator_label(GContext *ctx, const Indicator *ind, GRect cell, bool right) {
  const GColor color = ind->on ? ind->ink : s_ghost;
  const int density = ind->on ? DENSITY_FULL : s_label_off_density;
  if (s_settings.digit_style == DIGITS_MATRIX) {
    if (!(s_fb = graphics_capture_frame_buffer(ctx))) return;
    draw_tiny_label(ind->label, cell, right, color, density);
    graphics_release_frame_buffer(ctx, s_fb);
    s_fb = NULL;
    return;
  }
  if (!font_active()) {
    draw_fitted_text(ctx, ind->label, cell, LABEL_H, color, density, right);
    return;
  }
  if (!(s_fb = graphics_capture_frame_buffer(ctx))) return;
  font_draw_text(FG_LABEL, ind->label, cell.origin.x, cell.size.w,
                 cell.origin.y + (cell.size.h + font_height(FG_LABEL)) / 2, right ? ALIGN_RIGHT : ALIGN_CENTER,
                 color, density);
  graphics_release_frame_buffer(ctx, s_fb);
  s_fb = NULL;
}

// Whether (px, py) is inside the rounded rectangle x, y, w, h with corner radius r.
static bool in_round_rect(int px, int py, int x, int y, int w, int h, int r) {
  if (px < x || py < y || px >= x + w || py >= y + h) return false;
  const int cx = px < x + r ? x + r : px >= x + w - r ? x + w - r : px;
  const int cy = py < y + r ? y + r : py >= y + h - r ? y + h - r : py;
  if (cx == px || cy == py) return true;
  const int dx = 2 * px + 1 - 2 * cx, dy = 2 * py + 1 - 2 * cy;  // from the corner's centre, in half pixels
  return dx * dx + dy * dy <= 4 * r * r;
}

// Pills: each indicator in its own rounded outline; an active one is filled with its colour and
// its label cut out of it, an inactive one is a faint dotted outline with a faint label.
#define PILL_W 41
#define PILL_H 13
#define PILL_R 5
static void draw_indicator_pills(GContext *ctx, const Indicator *ind) {
  // CHG and MUTE on the left, BT and DST on the right.
  static const uint8_t SLOT[4][2] = { { 44, 0 }, { 1, 0 }, { 44, 15 }, { 1, 15 } };  // BT, CHG, DST, MUTE
  for (int i = 0; i < 4; i++) {
    if (!ind[i].on && !s_settings.ghosts) continue;
    const int x = IND_X0 + SLOT[i][0], y = IND_Y0 + SLOT[i][1];
    draw_indicator_label(ctx, &ind[i], GRect(x + 2, y + 2, PILL_W - 4, LABEL_H), false);
    if (!(s_fb = graphics_capture_frame_buffer(ctx))) return;
    for (int yy = y; yy < y + PILL_H; yy++) {
      GBitmapDataRowInfo row = gbitmap_get_data_row_info(s_fb, yy);
      for (int xx = x; xx < x + PILL_W; xx++) {
        if (!in_round_rect(xx, yy, x, y, PILL_W, PILL_H, PILL_R)) continue;
        if (ind[i].on) {  // fill, with the label cut out of it
          row.data[xx] = row.data[xx] == ind[i].ink.argb ? s_lcd.argb : ind[i].ink.argb;
        } else if (!in_round_rect(xx, yy, x + 1, y + 1, PILL_W - 2, PILL_H - 2, PILL_R - 1) &&
                   BAYER4[yy & 3][xx & 3] < s_label_off_density) {  // the outline, in dots
          row.data[xx] = s_ghost.argb;
        }
      }
    }
    graphics_release_frame_buffer(ctx, s_fb);
    s_fb = NULL;
  }
}

// Active only: the labels of the indicators that are on, right-aligned, two to a column (the
// right column first). Nothing is shown for the others.
static void draw_indicator_active(GContext *ctx, const Indicator *ind) {
  int n = 0;
  for (int i = 0; i < 4; i++) {
    if (!ind[i].on) continue;
    const int right = n < 2 ? BOX_RIGHT + 1 : BOX_DIV - 2;  // right edge of the column
    draw_indicator_label(ctx, &ind[i], GRect(right - 42, IND_Y0 + 1 + (n % 2) * 15, 42, 12), true);
    n++;
  }
}

// Icons (double-size pixel art), in one row: Bluetooth, charging (or PM), Quiet Time (the moon)
// and daylight saving time (the sun). Faint dots when off.
static void draw_icon_2x(int x, int y, const char *const *rows, int n, GColor c, int density) {
  for (int r = 0; r < 2 * n; r++) {
    GBitmapDataRowInfo row = gbitmap_get_data_row_info(s_fb, y + r);
    const char *bits = rows[r / 2];
    for (int k = 0; bits[k / 2]; k++) {
      const int xx = x + k;
      if (bits[k / 2] != '#' || xx < row.min_x || xx > row.max_x) continue;
      if (density >= DENSITY_FULL || BAYER4[(y + r) & 3][xx & 3] < density) row.data[xx] = c.argb;
    }
  }
}

static void draw_indicator_icons(GContext *ctx, const Indicator *ind, bool pm_cell) {
  static const char *const BT[] = { "..#..", "..##.", "#.#.#", ".###.", "..#..", ".###.", "#.#.#", "..##.", "..#.." };
  static const char *const BOLT[] = { "...##", "..##.", ".##..", "#####", "..##.", ".##..", "##..." };
  static const char *const MOON[] = { "..###", ".##..", "##...", "##...", "##...", "##...", ".##..", "..###" };
  static const char *const SUN[] = { "....#....", ".#.....#.", "...###...", "..#####..", "#.#####.#",
                                     "..#####..", "...###...", ".#.....#.", "....#...." };
  struct { const char *const *rows; int n; int x; int ind; } icon[] = {
    { BT, ARRAY_LENGTH(BT), IND_X0 + 3, 0 }, { BOLT, ARRAY_LENGTH(BOLT), IND_X0 + 23, 1 },
    { MOON, ARRAY_LENGTH(MOON), IND_X0 + 44, 3 }, { SUN, ARRAY_LENGTH(SUN), IND_X0 + 63, 2 },
  };
  if (!(s_fb = graphics_capture_frame_buffer(ctx))) return;
  for (unsigned k = 0; k < ARRAY_LENGTH(icon); k++) {
    const Indicator *it = &ind[icon[k].ind];
    if (k == 1 && pm_cell) continue;  // PM is a label, drawn below
    if (!it->on && !s_settings.ghosts) continue;
    draw_icon_2x(icon[k].x, IND_Y0 + 5 + (18 - 2 * icon[k].n) / 2, icon[k].rows, icon[k].n,
                 it->on ? it->ink : s_ghost, it->on ? DENSITY_FULL : s_label_off_density);
  }
  graphics_release_frame_buffer(ctx, s_fb);
  s_fb = NULL;
  if (pm_cell && (ind[1].on || s_settings.ghosts)) {
    draw_indicator_label(ctx, &ind[1], GRect(IND_X0 + 18, IND_Y0 + 9, 24, LABEL_H), false);
  }
}

// The indicators, in the chosen style: lit (ink) when active, faint otherwise. DST is lit while
// daylight saving time is in effect in the watch's time zone (the phone provides the zone; the
// watch's own clock knows when DST applies).
static void draw_indicators(GContext *ctx) {
  // On the charger but no longer charging: the battery is full.
  const bool full = s_battery.is_plugged && !s_battery.is_charging;
  const bool pm_cell = !is_24h() && s_settings.pm_in_box;
  // In the order BT, CHG (or PM), DST, MUTE.
  const Indicator ind[] = {
    { "BT",   s_connected, s_col[COL_BT] },
    // With the leading zero in 12-hour time, PM takes CHG's place (the battery icon shows charging).
    { pm_cell ? "PM" : full ? "FULL" : "CHG", pm_cell ? s_now.tm_hour >= 12 : s_battery.is_charging || full,
      pm_cell ? s_col[COL_PM] : s_col[COL_CHG] },
    { "DST",  s_now.tm_isdst > 0, s_col[COL_DST] },
    { "MUTE", s_quiet, s_col[COL_MUTE] },
  };
  switch (s_settings.indicator_style) {
    case IND_ACTIVE: draw_indicator_active(ctx, ind); break;
    case IND_ICONS:  draw_indicator_icons(ctx, ind, pm_cell); break;
    default:         draw_indicator_pills(ctx, ind);
  }
}

static void canvas_update(Layer *layer, GContext *ctx) {
  // Case frame and LCD window.
  graphics_context_set_fill_color(ctx, s_col[COL_CASE]);
  graphics_fill_rect(ctx, layer_get_bounds(layer), 0, GCornerNone);
  // The LCD window edge: a plain band with solid lines (and the edge matching them). The other
  // styles draw their own edge (draw_lcd_edges) on the case instead, so the band is left out.
  const bool plain_edge = s_settings.edge_style == EDGE_MATCH && s_settings.line_style == LINES_SOLID;
  if (plain_edge) {
    graphics_context_set_fill_color(ctx, s_col[COL_EDGE]);
    graphics_fill_rect(ctx, GRect(LCD_X, LCD_Y - 2, LCD_W, LCD_H + 4), 0, GCornerNone);
  }
  graphics_context_set_fill_color(ctx, s_lcd);
  graphics_fill_rect(ctx, GRect(LCD_X, LCD_Y, LCD_W, LCD_H), 0, GCornerNone);
  // A black LCD (inverted) would melt into a plain black case: mark the panel's edges. (With
  // custom colours the edge is a colour of its own.)
  if (!s_colors.enabled && s_settings.inverted && s_col[COL_CASE].argb == GColorBlackARGB8 && plain_edge) {
    graphics_context_set_fill_color(ctx, GColorDarkGray);
    graphics_fill_rect(ctx, GRect(LCD_X, LCD_Y - 1, LCD_W, 1), 0, GCornerNone);
    graphics_fill_rect(ctx, GRect(LCD_X, LCD_Y + LCD_H, LCD_W, 1), 0, GCornerNone);
  }

  s_quiet = quiet_time_is_active();
  s_fb = graphics_capture_frame_buffer(ctx);
  if (!s_fb) return;
  draw_lcd();
  graphics_release_frame_buffer(ctx, s_fb);
  s_fb = NULL;

  // Text goes through the regular text API, so it comes after the framebuffer
  // is released.
  draw_top_bezel(ctx);
  draw_bottom_bezel(ctx);
  draw_indicators(ctx);
}

// ---------------------------------------------------------------------------
// Services

static void request_weather(void) {
  DictionaryIterator *iter;
  if (app_message_outbox_begin(&iter) != APP_MSG_OK) return;
  dict_write_uint8(iter, MESSAGE_KEY_RequestWeather, 1);
  app_message_outbox_send();
}

// Asks the phone for weather only when the phone is connected and the last reading is not
// recent. Each request wakes the phone app for a location fix and a web request.
static void refresh_weather_if_needed(void) {
  if (!s_connected) return;
  // A recent reading is enough, except when the high and low on screen belong to yesterday.
  if (range_valid() && time(NULL) - s_weather.updated < (WEATHER_REFRESH_MIN - 5) * 60) return;
  request_weather();
}

// Once a minute (the hour too, so a whole-hour time zone change redraws at once).
static void tick_handler(struct tm *now, TimeUnits changed) {
  s_now = *now;
  if (s_settings.show_steps) update_steps();
  if (now->tm_min % WEATHER_REFRESH_MIN == 0) refresh_weather_if_needed();
  layer_mark_dirty(s_canvas);
}

static void battery_handler(BatteryChargeState state) {
  s_battery = state;
  layer_mark_dirty(s_canvas);
}

static void play_vibe(uint8_t pattern) {
  static const uint32_t TRIPLE[] = { 150, 100, 150, 100, 150 };
  static const uint32_t HEARTBEAT[] = { 80, 120, 200 };
  static const uint32_t SOS[] = { 100, 100, 100, 100, 100, 300, 300, 100, 300, 100, 300, 300,
                                  100, 100, 100, 100, 100 };
  switch (pattern) {
    case VIBE_SHORT:  vibes_short_pulse(); break;
    case VIBE_LONG:   vibes_long_pulse(); break;
    case VIBE_DOUBLE: vibes_double_pulse(); break;
    case VIBE_TRIPLE:
      vibes_enqueue_custom_pattern(
          (VibePattern){ .durations = TRIPLE, .num_segments = ARRAY_LENGTH(TRIPLE) });
      break;
    case VIBE_HEARTBEAT:
      vibes_enqueue_custom_pattern(
          (VibePattern){ .durations = HEARTBEAT, .num_segments = ARRAY_LENGTH(HEARTBEAT) });
      break;
    case VIBE_SOS:
      vibes_enqueue_custom_pattern((VibePattern){ .durations = SOS, .num_segments = ARRAY_LENGTH(SOS) });
      break;
    default: break;
  }
}

static void connection_handler(bool connected) {
  if (connected != s_connected && !quiet_time_is_active()) {
    play_vibe(connected ? s_settings.vibe_connect : s_settings.vibe_disconnect);
  }
  const bool changed = connected != s_connected;
  s_connected = connected;
  if (!changed) return;  // nothing visible changes
  layer_mark_dirty(s_canvas);
  if (connected) refresh_weather_if_needed();
}

#if defined(PBL_HEALTH)
static void health_handler(HealthEventType event, void *context) {
  if (event != HealthEventHeartRateUpdate) return;
  const int before = s_hr;
  update_heart_rate();
  if (s_hr != before) layer_mark_dirty(s_canvas);
}
#endif


// Reads a preset colour such as "FFA020" into *rgb. Returns false, leaving *rgb
// alone, unless it is exactly six hex digits.
static bool parse_hex(const char *hex, uint32_t *rgb) {
  uint32_t value = 0;
  int digits = 0;
  for (; *hex; hex++, digits++) {
    char ch = *hex;
    int d = (ch >= '0' && ch <= '9') ? ch - '0'
          : (ch >= 'A' && ch <= 'F') ? ch - 'A' + 10
          : (ch >= 'a' && ch <= 'f') ? ch - 'a' + 10 : -1;
    if (d < 0) return false;
    value = (value << 4) | d;
  }
  if (digits != 6) return false;
  *rgb = value;
  return true;
}

// Copies a settings-page text into a fixed buffer, upper-cased to match the
// other printed text.
static void copy_upper(char *dst, size_t size, const char *src) {
  strncpy(dst, src, size - 1);
  dst[size - 1] = '\0';
  // The cut is in bytes but the settings page counts characters: if it landed inside a
  // multi-byte UTF-8 character, drop that whole character so the string stays valid.
  size_t len = strlen(dst);
  if (strlen(src) > len) {
    while (len > 0 && ((unsigned char)dst[len - 1] & 0xC0) == 0x80) len--;  // continuation bytes
    if (len > 0 && ((unsigned char)dst[len - 1] & 0xC0) == 0xC0) len--;  // their lead byte
    dst[len] = '\0';
  }
  for (char *c = dst; *c; c++) {
    if (*c >= 'a' && *c <= 'z') *c -= 'a' - 'A';
  }
}

static int tuple_int(Tuple *t) {
  if (t->type == TUPLE_CSTRING) return atoi(t->value->cstring);
  return (int)t->value->int32;
}

static void inbox_handler(DictionaryIterator *iter, void *context) {
  Tuple *t;
  bool settings_changed = false;
  bool colors_changed = false;

  if ((t = dict_find(iter, MESSAGE_KEY_Temp))) {
    const int16_t temp = (int16_t)tuple_int(t);
    const time_t now = time(NULL);
    Tuple *lo = dict_find(iter, MESSAGE_KEY_TempMin), *hi = dict_find(iter, MESSAGE_KEY_TempMax);
    const bool has_range = lo && hi;
    const int16_t temp_min = has_range ? (int16_t)tuple_int(lo) : 0;
    const int16_t temp_max = has_range ? (int16_t)tuple_int(hi) : 0;
    // A new day's high and low must be saved too, even if nothing else changed.
    // (localtime() returns one shared buffer, so the two days are read one at a time.)
    const time_t before = s_weather.updated;
    const int day_before = before == 0 ? -1 : localtime(&before)->tm_yday;
    const bool new_day = day_before != localtime(&now)->tm_yday;
    const bool changed = temp != s_weather.temp || has_range != s_weather.has_range ||
                         temp_min != s_weather.temp_min || temp_max != s_weather.temp_max || new_day;
    s_weather.temp = temp;
    s_weather.temp_min = temp_min;
    s_weather.temp_max = temp_max;
    s_weather.has_range = has_range ? 1 : 0;
    s_weather.updated = now;
    // Flash writes cost energy: save when the value changed, otherwise about hourly (the
    // saved timestamp only has to be good enough for the 3 hour "too old" limit).
    if (changed || now - s_weather_saved >= 60 * 60) {
      persist_write_data(WEATHER_KEY, &s_weather, sizeof(s_weather));
      s_weather_saved = now;
    }
  }
  // Time & date
  if ((t = dict_find(iter, MESSAGE_KEY_TimeFormat))) {
    const char *f = t->value->cstring;
    s_settings.time_format = strcmp(f, "12") == 0 ? FORMAT_12H : strcmp(f, "24") == 0 ? FORMAT_24H : FORMAT_AUTO;
    settings_changed = true;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_EdgeStyle))) {
    const char *v = t->value->cstring;
    s_settings.edge_style = strcmp(v, "tab") == 0 ? EDGE_TAB : strcmp(v, "notched") == 0 ? EDGE_NOTCHED : EDGE_MATCH;
    settings_changed = true;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_RangeMarks))) {
    const char *v = t->value->cstring;
    s_settings.range_marks = strcmp(v, "triangles") == 0 ? MARKS_TRIANGLES : strcmp(v, "lohi") == 0 ? MARKS_LOHI
        : MARKS_TALL;
    settings_changed = true;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_IndicatorStyle))) {
    const char *v = t->value->cstring;
    s_settings.indicator_style = strcmp(v, "active") == 0 ? IND_ACTIVE : strcmp(v, "icons") == 0 ? IND_ICONS
        : IND_PILLS;
    settings_changed = true;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_TimeZero12))) {
    s_settings.pm_in_box = tuple_int(t) != 0;
    settings_changed = true;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_TimeZero))) {
    s_settings.hour_no_zero = tuple_int(t) == 0;
    settings_changed = true;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_TempUnit))) {
    const char *u = t->value->cstring;
    s_settings.temp_unit = strcmp(u, "C") == 0 ? UNIT_C : strcmp(u, "F") == 0 ? UNIT_F : UNIT_AUTO;
    settings_changed = true;
  }
  // Top bezel
  if ((t = dict_find(iter, MESSAGE_KEY_ShowBattery))) {
    s_settings.show_battery = tuple_int(t);
    settings_changed = true;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_TopLeftText))) {
    copy_upper(s_settings.top_left, sizeof(s_settings.top_left), t->value->cstring);
    settings_changed = true;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_ShowSteps))) {
    s_settings.show_steps = tuple_int(t);
    settings_changed = true;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_TopRightText))) {
    copy_upper(s_settings.top_right, sizeof(s_settings.top_right), t->value->cstring);
    settings_changed = true;
  }
  // Bottom bezel
  if ((t = dict_find(iter, MESSAGE_KEY_BezelLabel))) {
    copy_upper(s_settings.bezel_label, sizeof(s_settings.bezel_label), t->value->cstring);
    settings_changed = true;
  }
  // Appearance
  bool font_changed = false;
  if ((t = dict_find(iter, MESSAGE_KEY_DigitStyle))) {
    const char *d = t->value->cstring;
    const uint8_t style = strcmp(d, "saira") == 0 ? DIGITS_SAIRA : strcmp(d, "handjet") == 0 ? DIGITS_HANDJET
        : strcmp(d, "iceberg") == 0 ? DIGITS_ICEBERG : strcmp(d, "stencil") == 0 ? DIGITS_STENCIL
        : strcmp(d, "matrix") == 0 ? DIGITS_MATRIX : DIGITS_SEGMENT;
    font_changed = style != s_settings.digit_style;
    s_settings.digit_style = style;
    settings_changed = true;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_LineStyle))) {
    const char *l = t->value->cstring;
    s_settings.line_style = strcmp(l, "segmented") == 0 ? LINES_SEGMENTED : strcmp(l, "ruler") == 0 ? LINES_RULER
        : strcmp(l, "brackets") == 0 ? LINES_BRACKETS : strcmp(l, "hud") == 0 ? LINES_HUD : LINES_SOLID;
    settings_changed = true;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_CaseColor))) {
    const char *c = t->value->cstring;
    s_settings.silver = strcmp(c, "silver") == 0;
    settings_changed = true;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_Inverted))) {
    s_settings.inverted = tuple_int(t);
    settings_changed = true;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_Ghosts))) {
    s_settings.ghosts = tuple_int(t);
    settings_changed = true;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_BacklightColor))) {
    // "system", "custom" (colour in BacklightCustom), or a hex colour such as "FFA020".
    const char *choice = t->value->cstring;
    uint32_t rgb;
    if (strcmp(choice, "system") == 0) s_settings.backlight = BACKLIGHT_SYSTEM;
    else if (strcmp(choice, "custom") == 0) s_settings.backlight = BACKLIGHT_CUSTOM;
    else if (parse_hex(choice, &rgb)) s_settings.backlight = rgb;
    settings_changed = true;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_BacklightCustom))) {
    s_settings.backlight_custom = (uint32_t)tuple_int(t) & 0xFFFFFF;  // 0xRRGGBB from the picker
    settings_changed = true;
  }
  // Alerts
  if ((t = dict_find(iter, MESSAGE_KEY_VibeDisconnect))) {
    int v = tuple_int(t);
    s_settings.vibe_disconnect = (v >= 0 && v < VIBE_COUNT) ? v : VIBE_NONE;
    settings_changed = true;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_VibeConnect))) {
    int v = tuple_int(t);
    s_settings.vibe_connect = (v >= 0 && v < VIBE_COUNT) ? v : VIBE_NONE;
    settings_changed = true;
  }

  // Custom colors (the picker sends 0xRRGGBB; the display keeps the nearest of its 64 colours)
  if ((t = dict_find(iter, MESSAGE_KEY_CustomColors))) {
    s_colors.enabled = tuple_int(t) ? 1 : 0;
    colors_changed = true;
  }
  for (int i = 0; i < COL_COUNT; i++) {
    if ((t = dict_find(iter, color_key(i)))) {
      s_colors.argb[i] = GColorFromHEX(tuple_int(t) & 0xFFFFFF).argb;
      colors_changed = true;
    }
  }
  if (colors_changed) {
    persist_write_data(COLORS_KEY, &s_colors, sizeof(s_colors));
    settings_changed = true;
  }

  if (settings_changed) {
    persist_write_data(SETTINGS_KEY, &s_settings, sizeof(s_settings));
    if (font_changed) load_digit_font();
    apply_theme();
    apply_backlight();
    if (s_settings.show_steps) update_steps();
    refresh_weather_if_needed();
  }
  layer_mark_dirty(s_canvas);
}

// ---------------------------------------------------------------------------
// Lifecycle

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  s_canvas = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_canvas, canvas_update);
  layer_add_child(root, s_canvas);
}

static void window_unload(Window *window) {
  layer_destroy(s_canvas);
}

static void init(void) {
  s_settings = (Settings){
    .show_battery = true,
    .top_left = "30 DAY BATT",
    .show_steps = true,
    .top_right = "WR 3ATM",
    .bezel_label = "PEBBLE",
    .ghosts = true,
    .backlight = BACKLIGHT_SYSTEM,
    .vibe_disconnect = VIBE_DOUBLE,
    .vibe_connect = VIBE_SHORT,
  };
  if (persist_exists(SETTINGS_KEY)) {
    persist_read_data(SETTINGS_KEY, &s_settings, sizeof(s_settings));
  } else if (persist_get_size(SETTINGS_KEY_SHARED) == (int)sizeof(s_settings)) {
    // Saved by the build that put Settings under the colours' key: moved to its own key. (The
    // colours it overwrote are lost; they come back with the next Save on the settings page.)
    persist_read_data(SETTINGS_KEY_SHARED, &s_settings, sizeof(s_settings));
    persist_write_data(SETTINGS_KEY, &s_settings, sizeof(s_settings));
  }
  if (s_settings.digit_style >= DIGITS_COUNT) s_settings.digit_style = DIGITS_SEGMENT;
  if (s_settings.line_style >= LINES_COUNT) s_settings.line_style = LINES_SOLID;
  if (s_settings.edge_style >= EDGE_COUNT) s_settings.edge_style = EDGE_MATCH;
  if (s_settings.indicator_style >= IND_COUNT) s_settings.indicator_style = IND_PILLS;
  if (s_settings.range_marks >= MARKS_COUNT) s_settings.range_marks = MARKS_TALL;
  load_digit_font();
  s_bezel_font = load_font(RESOURCE_ID_FONT_BEZEL, BG_COUNT);
  persist_read_data(WEATHER_KEY, &s_weather, sizeof(s_weather));
  if (s_weather.has_range > 1) s_weather.has_range = 0;
  s_colors.enabled = 0;
  memcpy(s_colors.argb, COLOR_DEFAULTS, sizeof(s_colors.argb));
  // (Only data of the colours' own size: the key once held Settings by mistake.)
  if (persist_get_size(COLORS_KEY) == (int)sizeof(s_colors)) persist_read_data(COLORS_KEY, &s_colors, sizeof(s_colors));
  if (s_colors.enabled > 1) s_colors.enabled = 0;
  apply_theme();
  apply_backlight();

  time_t now = time(NULL);
  s_now = *localtime(&now);
  s_battery = battery_state_service_peek();
  s_connected = connection_service_peek_pebble_app_connection();
  update_steps();

  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers){ .load = window_load, .unload = window_unload });
  window_stack_push(s_window, true);

  tick_timer_service_subscribe(MINUTE_UNIT | HOUR_UNIT, tick_handler);
  battery_state_service_subscribe(battery_handler);
  connection_service_subscribe(
      (ConnectionHandlers){ .pebble_app_connection_handler = connection_handler });
#if defined(PBL_HEALTH)
  update_heart_rate();
  health_service_events_subscribe(health_handler, NULL);
#endif

  app_message_register_inbox_received(inbox_handler);
  // The inbox must hold a full settings Save: about 230 bytes with plain text and over 400
  // with emoji in the three custom texts, plus 11 bytes for each of the 24 colour settings.
  // The outbox only ever carries the one-byte weather request.
  app_message_open(1024, dict_calc_buffer_size(1, sizeof(uint8_t)));
}

static void deinit(void) {
  tick_timer_service_unsubscribe();
  battery_state_service_unsubscribe();
  connection_service_unsubscribe();
#if defined(PBL_HEALTH)
  health_service_events_unsubscribe();
#endif
  window_destroy(s_window);
  if (s_font) free(s_font);
  if (s_bezel_font) free(s_bezel_font);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}

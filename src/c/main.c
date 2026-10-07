// LCD 221: a watch face for the Pebble Time 2 (emery, 200x228) that imitates
// the Casio W-221H: a white "LCD" with slanted 7-segment digits, a dot-matrix
// weekday and a 2x2 indicator box, between a top and a bottom bezel.
//
// Everything on the LCD panel is rasterized straight into the framebuffer, which
// allows things the drawing API can't do: polygon segments with a slant and
// gray-shade anti-aliasing, and faint "ghost" segments for the unlit parts (a
// sparse ordered dither). Only the bezel text and indicator labels go through
// the regular text API.
//
// File map (search for the "----" section banners):
//   raster primitives  span/fill and the ghost dither
//   7-segment digits   polygon fill, anti-aliasing, digits (outlines: segments.h)
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

// The 7-segment digits (time, date, temperature) lean like the W-221H's: LCD_SLANT
// is the lean in thousandths of the height (75 = 7.5%, as measured on the
// original). LCD_AA smooths the edges of the slanted digits with the display's gray
// shades. Upright digits (the "Slanted digits" setting off) are drawn without
// smoothing: their edges are vertical and horizontal, so plain pixels are sharper.
#define LCD_SLANT 75
#define LCD_AA true
#define SETTINGS_KEY 17  // bumped whenever existing Settings fields change layout (new fields may use free padding)
#define WEATHER_KEY 6  // bumped whenever Weather changes layout
#define COLORS_KEY 18  // ColorSettings, persisted apart from Settings so old saves stay valid
#define WEATHER_MAX_AGE (3 * 60 * 60)
#define WEATHER_REFRESH_MIN 30
#define SECONDS_BURST_DEFAULT_S 30  // how long the seconds tick after a shake (setting: 5..120)
#define SECONDS_BURST_MIN_S 5
#define SECONDS_BURST_MAX_S 120

#define BACKLIGHT_SYSTEM 0xFFFFFFFFu  // the user's normal backlight colour
#define BACKLIGHT_CUSTOM 0xFFFFFFFEu  // use backlight_custom

// Vibration patterns selectable for phone connect/disconnect.
typedef enum {
  VIBE_NONE, VIBE_SHORT, VIBE_LONG, VIBE_DOUBLE, VIBE_TRIPLE, VIBE_HEARTBEAT, VIBE_SOS, VIBE_COUNT
} VibeChoice;

// Settings.hourly_vibe when on. It was the "vibration only" choice of the hourly chime, whose
// sounds were removed in 1.4.0: saved settings that chose it keep vibrating, the sounds are off.
#define HOURLY_VIBE_ON 4

// Time format and temperature unit: follow the watch, or force one.
enum { FORMAT_AUTO = 0, FORMAT_24H = 1, FORMAT_12H = 2 };
enum { UNIT_AUTO = 0, UNIT_C = 1, UNIT_F = 2 };
// The black case can be "charcoal": dark gray dots dithered over it, 25% of the pixels or a
// checkerboard. The value is also the dots' density out of 16 (see DENSITY_FULL) divided by 4.
enum { CASE_SOLID = 0, CASE_DOTS = 1, CASE_CHECKER = 2 };

// Digit styles: the 7-segment digits, or a font for every text and number on the LCD.
enum { DIGITS_SEGMENT = 0, DIGITS_OXANIUM = 1, DIGITS_CHAKRA = 2, DIGITS_ORBITRON = 3, DIGITS_COUNT };
// Divider line styles.
enum { LINES_SOLID = 0, LINES_SEGMENTED = 1, LINES_RULER = 2, LINES_BRACKETS = 3, LINES_HUD = 4, LINES_COUNT };

// Date padding: zeros, a blank for the first number only, or blanks for both numbers.
enum { PAD_ZERO = 0, PAD_FIRST_BLANK = 1, PAD_BOTH_BLANK = 2 };

// Everything the settings page controls, grouped and ordered like the page itself
// (config.js). Saved with persist_write_data: bump SETTINGS_KEY when it changes.
typedef struct {
  // Time & date
  uint8_t time_format;         // FORMAT_AUTO follows the watch's 12/24-hour setting
  bool day_first;
  // Right box
  bool show_seconds;           // seconds instead of the temperature (always, or after a shake: see below)
  uint8_t temp_unit;           // UNIT_AUTO follows the watch's measurement system
  // Top bezel
  bool show_battery;           // top-left: battery level, else top_left
  char top_left[20];           // top-left text when the battery level is off, upper-cased
  bool show_steps;             // top-right: step count, else top_right
  char top_right[20];          // top-right text when the step count is off, upper-cased
  // Bottom bezel
  bool heart_rate;             // HR badge + latest BPM instead of the WR badge
  char bezel_label[16];        // printed on the bottom bezel, upper-cased
  // Appearance
  bool silver;                 // silver case instead of black
  bool inverted;               // light digits on a dark LCD
  bool slanted;                // digits lean like the original's (off: upright and sharper)
  bool ghosts;                 // show faint unlit segments
  uint32_t backlight;          // 0xRRGGBB tint, BACKLIGHT_SYSTEM, or BACKLIGHT_CUSTOM
  uint32_t backlight_custom;   // 0xRRGGBB, from the settings page's colour picker
  // Alerts
  uint8_t vibe_disconnect;     // VibeChoice played when the phone disconnects
  uint8_t vibe_connect;        // VibeChoice played when it reconnects
  uint8_t hourly_vibe;         // HOURLY_VIBE_ON: a double pulse on the hour (0: off)
  uint8_t unused1, unused2;    // were the hourly chime's Quiet Time and volume settings
  // Added after the groups above, at the end, so settings saved by earlier versions still load.
  // (They sit in what used to be padding, so the struct keeps its size; init() sanitises them.)
  uint8_t seconds_on_shake;    // 1: seconds tick only for a while after a wrist shake
  uint8_t seconds_burst_s;     // how many seconds they tick for
  uint8_t hour_no_zero;        // 1: 24-hour time has no leading zero (7:05, like the original)
  uint8_t date_pad;            // single-digit date numbers: PAD_ZERO (06-05), PAD_FIRST_BLANK ( 6-05), PAD_BOTH_BLANK ( 6- 5)
  uint8_t date_range;          // 1: the date box shows today's high and low; the day of the month moves up next to the weekday
  uint8_t case_pattern;        // black case only: CASE_SOLID, or dark gray dots over it (CASE_DOTS, CASE_CHECKER)
  // Added at the end: settings saved before are shorter, and these keep their defaults (0).
  uint8_t digit_style;         // DIGITS_SEGMENT, or one of the pre-rendered fonts
  uint8_t line_style;          // LINES_SOLID, ... (the dividers in the bottom part of the LCD)
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
  COL_WEEKDAY, COL_FRAME, COL_BT, COL_CHG, COL_SIG, COL_MUTE,              // top row of the LCD (COL_SIG: unused)
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

// The four indicator-box labels, in the order draw_indicator_labels() lists them. (SIG, the
// hourly chime's indicator, made way for DST; COL_SIG stays so saved colours keep their places.)
static const uint8_t INDICATOR_COLORS[] = { COL_BT, COL_CHG, COL_DST, COL_MUTE };

static Window *s_window;
static Layer *s_canvas;
static GBitmap *s_fb;

static Settings s_settings;
static ColorSettings s_colors;
static Weather s_weather;
static time_t s_weather_saved;       // when s_weather was last written to persistent storage
static bool s_focus = true;          // the face is the app in front (not covered by a system window)
static bool s_quiet;                 // Quiet Time, read once per redraw
static time_t s_burst_until;         // seconds_on_shake: the seconds show until this time (0 = idle)
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
static int s_slant;                  // digit lean in thousandths of the height (0 = upright)
static bool s_smooth;                // anti-alias the digits' edges (only while slanted)
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

// Upright digits: maps a 0..1000 coordinate to whole pixels so that every bar of a digit
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

// Fills a polygon given in 0..1000 coordinates, scaled into the box (x, y, w, h).
// A pixel is filled when its centre is inside (even-odd rule).
// `slant` leans the shape right like italics, in thousandths of the height:
// each row is shifted by its height above the bottom times slant/1000, with
// sub-pixel precision so edges stay clean diagonals.
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

static void fill_poly(int x, int y, int w, int h, const int16_t *pts, int n, int slant, GColor c,
                      bool dither) {
  int16_t snapped[16];
  if (slant == 0 && n <= 8) {
    snap_poly(w, h, pts, n, snapped);
    pts = snapped;
  }
  int first, last;
  poly_rows(pts, n, h, &first, &last);
  for (int r = first; r <= last; r++) {
    int32_t shift = (int32_t)(2 * (h - r) - 1) * slant;  // row shift, in 1/2000 px
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
      int c0 = -floor_div(1000 - 2 * xs[i] * w - shift, 2000);   // ceil(px - 0.5)
      int c1 = floor_div(2 * xs[i + 1] * w + shift - 1000, 2000);
      if (c0 <= c1) span(y + r, x + c0, x + c1, c, dither);
    }
  }
}

// Anti-aliased version of fill_poly for lit (ink) shapes: each pixel's
// coverage is measured exactly across its width at 4 heights, then drawn in
// one of the display's shades between ink and white (black, dark gray, light
// gray). A pixel is only ever darkened, never lightened, so shapes drawn one
// after another (neighbouring segments) don't eat into each other.
#define AA_SUB 4
#define AA_MAX_W 96

// `wa` thirds of colour a mixed with the rest of colour b (the display has 4 levels per
// channel). With black on white, wa = 1 gives light gray and wa = 2 gives dark gray.
static GColor mix_color(GColor a, GColor b, int wa) {
  GColor c = GColorBlack;
  c.r = (a.r * wa + b.r * (3 - wa) + 1) / 3;
  c.g = (a.g * wa + b.g * (3 - wa) + 1) / 3;
  c.b = (a.b * wa + b.b * (3 - wa) + 1) / 3;
  return c;
}

// `ink` is the digit's colour; the shades between it and the LCD colour are worked out here.
static void fill_poly_aa(int x, int y, int w, int h, const int16_t *pts, int n, int slant,
                         GColor ink) {
  const uint8_t aa[3] = { mix_color(ink, s_lcd, 1).argb, mix_color(ink, s_lcd, 2).argb, ink.argb };
  int32_t cov[AA_MAX_W];
  int cols = w + h * slant / 1000 + 2;
  if (cols > AA_MAX_W) cols = AA_MAX_W;
  int first, last;
  poly_rows(pts, n, h, &first, &last);
  for (int r = first; r <= last; r++) {
    memset(cov, 0, sizeof(cov[0]) * cols);
    for (int k = 0; k < AA_SUB; k++) {
      // Sub-row centre in 0..1000 shape units, and its slant shift in 1/1000 px.
      int32_t yn = (int32_t)(2 * AA_SUB * r + 2 * k + 1) * 1000 / (2 * AA_SUB * h);
      int32_t shift = (int32_t)(2 * AA_SUB * (h - r) - 2 * k - 1) * slant / (2 * AA_SUB);
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
        int32_t a = xs[i] * w + shift, b = xs[i + 1] * w + shift;  // span in 1/1000 px
        if (a < 0) a = 0;
        if (b > (int32_t)cols * 1000) b = (int32_t)cols * 1000;
        for (int c = a / 1000; c < cols && (int32_t)c * 1000 < b; c++) {
          int32_t lo = a > c * 1000 ? a : c * 1000, hi = b < (c + 1) * 1000 ? b : (c + 1) * 1000;
          if (hi > lo) cov[c] += hi - lo;
        }
      }
    }
    int yy = y + r;
    if (yy < 0 || yy >= PBL_DISPLAY_HEIGHT) continue;
    GBitmapDataRowInfo row = gbitmap_get_data_row_info(s_fb, yy);
    for (int c = 0; c < cols; c++) {
      // Full coverage = AA_SUB * 1000.
      int level = cov[c] >= 3000 ? 2 : cov[c] >= 1800 ? 1 : cov[c] >= 600 ? 0 : -1;
      int xx = x + c;
      if (level < 0 || xx < row.min_x || xx > row.max_x) continue;
      uint8_t cur = row.data[xx];
      int cur_level = cur == aa[2] ? 2 : cur == aa[1] ? 1 : -1;
      if (level > cur_level) row.data[xx] = aa[level];
    }
  }
}

// A 7-segment digit built from the segment outlines of the "7-Segment" font by
// Jan Bobrowski (see segments.h), scaled to the box and slanted by s_slant. Only the
// segments in `present` are drawn; unlit ones appear as ghosts (if enabled),
// drawn first so lit segments always win.
static void draw_segments(int x, int y, int w, int h, uint8_t on, uint8_t present, GColor ink) {
  for (int pass = s_settings.ghosts ? 0 : 1; pass <= 1; pass++) {
    uint8_t mask = present & (pass ? on : (uint8_t)~on);
    for (int i = 0; i < 7; i++) {
      if (!(mask & (1 << i))) continue;
      if (pass && s_smooth) fill_poly_aa(x, y, w, h, SEG_POLYS[i], SEG_POLY_LEN[i], s_slant, ink);
      else fill_poly(x, y, w, h, SEG_POLYS[i], SEG_POLY_LEN[i], s_slant,
                     pass ? ink : s_ghost, !pass);
    }
  }
}

static void draw_digit(int x, int y, int w, int h, int value, GColor ink) {
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

// Day-of-week text: 5x5 letters with 4x5 dots on a 5x6 grid, one column apart.
// Upright, like the W-221H's. `narrow` letters (3x5 dots on a 4x6 grid, 22px apart) leave
// room for the day of the month after them.
static void draw_day(int x, int y, const char *text, bool narrow, GColor ink) {
  for (int i = 0; text[i]; i++) {
    if (narrow) draw_matrix(x + i * 22, y, day_glyph(text[i]), 5, 5, 4, 6, 3, 5, ink);
    else draw_matrix(x + i * 30, y, day_glyph(text[i]), 5, 5, 5, 6, 4, 5, ink);
  }
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

static int font_height(FontGroup g) { return s_font[4 + 4 * g]; }

// A glyph's table entry: char, width, advance, left bearing (signed), bitmap offset (2 bytes).
static const uint8_t *font_glyph(FontGroup g, char ch) {
  const int count = s_font[4 + 4 * g + 1];
  const uint8_t *table = s_font + le16(s_font + 4 + 4 * g + 2);
  for (int i = 0; i < count; i++) {
    if (table[6 * i] == (uint8_t)ch) return table + 6 * i;
  }
  return NULL;
}

// Checks a font file before it is used: its header, and that every table and bitmap is inside it.
static bool font_valid(const uint8_t *f, size_t size) {
  if (size < 4 + 4 * FG_COUNT || f[0] != 'L' || f[1] != 'F' || f[2] != 1 || f[3] != FG_COUNT) return false;
  for (int g = 0; g < FG_COUNT; g++) {
    const size_t h = f[4 + 4 * g], count = f[4 + 4 * g + 1], table = le16(f + 4 + 4 * g + 2);
    if (table + 6 * count > size) return false;
    for (size_t i = 0; i < count; i++) {
      const uint8_t *e = f + table + 6 * i;
      if ((size_t)le16(e + 4) + h * ((e[1] * 2 + 7) / 8) > size) return false;
    }
  }
  return true;
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
    case DIGITS_OXANIUM:  id = RESOURCE_ID_FONT_OXANIUM; break;
    case DIGITS_CHAKRA:   id = RESOURCE_ID_FONT_CHAKRAPETCH; break;
    case DIGITS_ORBITRON: id = RESOURCE_ID_FONT_ORBITRON; break;
    default: return;
  }
  ResHandle handle = resource_get_handle(id);
  const size_t size = resource_size(handle);
  uint8_t *buf = malloc(size);
  if (!buf) return;
  if (resource_load(handle, buf, size) != size || !font_valid(buf, size)) {
    free(buf);
    return;
  }
  s_font = buf;
}

// Copies a glyph with its top-left corner at (x, y). Coverage 3 is the ink and 1-2 are shades
// between the ink and the LCD colour, like the anti-aliased segments. With `density` below
// DENSITY_FULL only the solid part is drawn, as dots (inactive indicator labels).
static void draw_glyph(FontGroup g, const uint8_t *e, int x, int y, GColor ink, int density) {
  const int w = e[1], h = font_height(g), stride = (w * 2 + 7) / 8;
  const uint8_t *bits = s_font + le16(e + 4);
  const uint8_t shade[4] = { 0, mix_color(ink, s_lcd, 1).argb, mix_color(ink, s_lcd, 2).argb, ink.argb };
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

  // The digits.
  // (The fonts are upright.)
  s_slant = s_settings.slanted && !font_active() ? LCD_SLANT : 0;
  s_smooth = LCD_AA && s_slant != 0;

  // One colour per component: the custom ones, or else the normal or inverted LCD. The case is
  // black or silver either way (Case color; the charcoal cases are black with a pattern).
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
// running edge to edge, a top bezel above it and a bottom bezel below.
#define LCD_X 0
#define LCD_Y 24
#define LCD_W 200
#define LCD_H 176

// Row 1: weekday (left) and the 2x2 indicator box (right).
#define WEEKDAY_X 12
#define WEEKDAY_NARROW_X 6  // the narrowed weekday, followed by the day of the month
#define WEEKDAY_Y 34
#define BOX_LEFT 109    // indicator box, left outer line (left cells as wide as the right ones)
#define BOX_RIGHT 191   // indicator box, right outer line
#define BOX_DIV 152     // indicator box, vertical divider (off-centre: the left column has MUTE, the widest)
#define BOX_TOP 34      // indicator box, top outer line (= weekday top)
#define BOX_BOTTOM 62   // indicator box, bottom outer line (= weekday bottom)
#define BOX_MID 48      // indicator box, middle divider
#define BOX_RADIUS 6    // indicator box, radius of the rounded corners
#define LABEL_H 10      // indicator label height in rows

// Row 2: the time.
// Proportions measured on the W-221H: the time is ~41% of the LCD height and sits
// just above the rule, its digits are ~7% wider than tall-relative to the LCD, and
// they are spread across nearly the whole width: the first digit's box starts about
// 4px from the edge, with the PM marker in the empty left part of that box. (The
// original's marker starts level with the digits and 6px from the edge; here it is
// nudged up and in a little.)
#define TIME_Y 76
#define TIME_W 35
#define TIME_H 70
#define PM_X 7    // the PM marker's distance from the LCD's left edge
#define PM_DY -2  // and how many rows it sits above the top of the digits

// Row 3: date (left) and seconds / temperature (right), under a 2px rule.
#define ROW3_LINE_Y 154
#define ROW3_DIV_X 126  // vertical divider between the date box and the right box
#define ROW3_Y 160      // top of the right box's digits
#define ROW3_H 36       // height of the right box's digits
// The right box's digits (seconds, temperature) and the temperature's parts: the sign slot's
// minus, the full-width digit whose right verticals make the "1" of 100+, the two digits and
// the degree mark.
#define RIGHT_DIGIT_W 19
#define TEMP_MINUS_X 130
#define TEMP_ONE_X 121
#define TEMP_D1_X 144
#define TEMP_D2_X 166
#define TEMP_DEG_X 188

// Bezels.
#define TOP_RIGHT_MAX 104  // widest the top-right bezel text may be
#define BOTTOM_CAP 208     // top of the bottom bezel's 14px capitals (centred in the bezel)

static const char *const DAYS[] = { "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT" };

static void draw_text(GContext *ctx, const char *text, GRect box, GTextAlignment align, GColor color) {
  graphics_context_set_text_color(ctx, color);
  graphics_draw_text(ctx, text, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD), box,
                     GTextOverflowModeTrailingEllipsis, align, NULL);
}

// Printed bezel text: larger (Gothic 24 bold) for readability. `y` is the top
// of the capitals; the font draws them BEZEL_CAP_OFFSET below the box top.
#define BEZEL_CAP_OFFSET 10
static void draw_bezel_text(GContext *ctx, const char *text, int x, int y, int w,
                            GTextAlignment align, GColor color) {
  graphics_context_set_text_color(ctx, color);
  graphics_draw_text(ctx, text, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
                     GRect(x, y - BEZEL_CAP_OFFSET, w, 30),
                     GTextOverflowModeTrailingEllipsis, align, NULL);
}

// Draws a label so it fits its indicator-box cell. The text is drawn in the
// bold font with its ink starting on the cell's top row, then re-sampled in
// the framebuffer (nearest-neighbour, so edges stay crisp), one letter at a time:
//  - vertically to `height` rows by dropping rows from inside each letter (see
//    row_to_drop), so horizontal bars keep their thickness;
//  - optionally horizontally, each letter to its own width `letter_w[i]`
//    (NULL = keep the width), with 2px letter gaps and stems kept at 2px;
//  - drawn with the ordered dither at `density` (out of DENSITY_FULL).
// The result is centred in `cell`; only the cell's interior is touched.
#define FIT_MAX_W 40  // the widest indicator cell is 39 px,
#define FIT_MAX_H 12  // and every cell is 12 rows tall
#define FIT_MAX_LETTERS 8
_Static_assert(BOX_RIGHT - BOX_DIV - 2 <= FIT_MAX_W && BOX_DIV - BOX_LEFT - 3 <= FIT_MAX_W,
               "an indicator cell is wider than the label scratch buffer");
_Static_assert(BOX_MID - BOX_TOP - 2 <= FIT_MAX_H && BOX_BOTTOM - BOX_MID - 2 <= FIT_MAX_H,
               "an indicator cell is taller than the label scratch buffer");
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

static void draw_fitted_text(GContext *ctx, const char *text, GRect cell, const int8_t *letter_w,
                             int letter_w_n, int height, GColor color, int density) {
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

    // Horizontal: keep the letter's width (and the text's own spacing), or
    // stretch it, with stems trimmed back to the bold font's 2px (bars by 1px).
    int tw = len, gap = 0;
    if (letter_w) {
      tw = (letter < letter_w_n && letter_w[letter] > len) ? letter_w[letter] : len;
      gap = kept > 0 ? 2 : 0;
    } else if (kept > 0) {
      gap = start - prev_end - 1;
    }
    if (kept + gap + tw > FIT_MAX_W) break;
    for (int y = 0; y < rows_out; y++) {
      for (int i = 0; i < gap; i++) out[y][kept + i] = 0;
      for (int i = 0; i < tw; i++) out[y][kept + gap + i] = buf[rows[y]][start + i * len / tw];
      if (tw == len) continue;
      int run_len = 0;
      for (int i = 0; i <= tw; i++) {
        if (i < tw && out[y][kept + gap + i]) { run_len++; continue; }
        int trim = (run_len >= 3 && run_len <= 5) ? run_len - 2 : (run_len > 5 ? 1 : 0);
        for (int k = 1; k <= trim; k++) out[y][kept + gap + i - k] = 0;
        run_len = 0;
      }
    }
    kept += gap + tw;
    prev_end = last;
    letter++;
  }

  int ox = (x0 + x1 + 1) / 2 - kept / 2, oy = y0 + (cell.size.h - rows_out) / 2;
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

// The indicator box's outline. Like the W-221H, the top-right and bottom-left
// corners are rounded (radius BOX_RADIUS); the outer frame is 2px, the inner
// dividers are 1px.
static void draw_indicator_frame(void) {
  // RING: one quadrant of a 2px ring of radius 6 around the corner's centre,
  // as (dx, dy) offsets (pixel centres 4.5 <= distance < 6.5).
  _Static_assert(BOX_RADIUS == 6, "RING is drawn for radius 6");
  static const int8_t RING[][2] = {
    {0, 5}, {0, 6}, {1, 5}, {1, 6}, {2, 5}, {2, 6}, {3, 4}, {3, 5}, {4, 3}, {4, 4},
    {4, 5}, {5, 0}, {5, 1}, {5, 2}, {5, 3}, {5, 4}, {6, 0}, {6, 1}, {6, 2},
  };
  const int r = BOX_RADIUS;
  const int ur_x = BOX_RIGHT - r, ur_y = BOX_TOP + r;    // top-right corner centre
  const int ll_x = BOX_LEFT + r, ll_y = BOX_BOTTOM - r;  // bottom-left corner centre
  fill(BOX_LEFT, BOX_TOP, ur_x - BOX_LEFT, 2, s_col[COL_FRAME], false);              // top
  fill(BOX_RIGHT - 1, ur_y, 2, BOX_BOTTOM - ur_y + 1, s_col[COL_FRAME], false);      // right
  fill(ll_x + 1, BOX_BOTTOM - 1, BOX_RIGHT - ll_x, 2, s_col[COL_FRAME], false);      // bottom
  fill(BOX_LEFT, BOX_TOP, 2, ll_y - BOX_TOP, s_col[COL_FRAME], false);               // left
  for (unsigned i = 0; i < ARRAY_LENGTH(RING); i++) {
    fill(ur_x + RING[i][0], ur_y - RING[i][1], 1, 1, s_col[COL_FRAME], false);
    fill(ll_x - RING[i][0], ll_y + RING[i][1], 1, 1, s_col[COL_FRAME], false);
  }
  // Inner dividers: solid 1px lines, meeting the inside of the frame.
  fill(BOX_LEFT + 2, BOX_MID, BOX_RIGHT - BOX_LEFT - 3, 1, s_col[COL_FRAME], false);
  fill(BOX_DIV, BOX_TOP + 2, 1, BOX_BOTTOM - BOX_TOP - 3, s_col[COL_FRAME], false);
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
  if (!is24) draw_dots(PM_X, ty + PM_DY, PM_BITS, 11, 14, 1, 1, pm, s_col[COL_PM]);  // AM/PM only in 12-hour mode
  // Digit boxes (left edges) and the colon, spread like the original's.
  static const int X[4] = { 6, 49, 108, 153 };
  const int colon_x = 93;
  // 12-hour: the first digit is blank or a 1 (the P sits where a 0 would be). 24-hour: the
  // leading zero is optional.
  int tens = (hour >= 10 || (is24 && !s_settings.hour_no_zero)) ? hour / 10 : DIGIT_BLANK;
  if (font_active()) {
    // Each digit centred in its box, the group centred on the digits' height; the font's colon
    // is centred on the digits too (tools/gen_fonts.py).
    const int base = ty + (th + font_height(FG_TIME)) / 2;
    if (tens != DIGIT_BLANK) font_draw_centered(FG_TIME, '0' + tens, X[0] + tw / 2, base, s_col[COL_HOURS]);
    font_draw_centered(FG_TIME, '0' + hour % 10, X[1] + tw / 2, base, s_col[COL_HOURS]);
    font_draw_centered(FG_TIME, ':', (X[1] + tw + X[2]) / 2, base, s_col[COL_COLON]);
    font_draw_centered(FG_TIME, '0' + s_now.tm_min / 10, X[2] + tw / 2, base, s_col[COL_MINUTES]);
    font_draw_centered(FG_TIME, '0' + s_now.tm_min % 10, X[3] + tw / 2, base, s_col[COL_MINUTES]);
    return;
  }
  if (is24) {
    draw_digit(X[0], ty, tw, th, tens, s_col[COL_HOURS]);
  } else {
    // In 12-hour mode the first digit can only be a 1 (or blank), so it only has
    // the two right-hand segments, as on the real watch. This also keeps the PM
    // marker clear of an unlit digit.
    draw_segments(X[0], ty, tw, th, tens == 1 ? SEG_B | SEG_C : 0, SEG_B | SEG_C, s_col[COL_HOURS]);
  }
  draw_digit(X[1], ty, tw, th, hour % 10, s_col[COL_HOURS]);
  // The colon's dots follow the digits' slant. As on the original, their centres
  // sit 36% and 70% of the way down the digits (not symmetrically).
  const int dot = 7, dot1 = th * 36 / 100, dot2 = th * 70 / 100;  // dot size; centres below the top
  fill(colon_x + (th - dot1) * s_slant / 1000, ty + dot1 - dot / 2, dot, dot, s_col[COL_COLON], false);
  fill(colon_x + (th - dot2) * s_slant / 1000, ty + dot2 - dot / 2, dot, dot, s_col[COL_COLON], false);
  draw_digit(X[2], ty, tw, th, s_now.tm_min / 10, s_col[COL_MINUTES]);
  draw_digit(X[3], ty, tw, th, s_now.tm_min % 10, s_col[COL_MINUTES]);
}

// The bottom line of something `h` px tall in the bottom row: the right box's digit baseline, or,
// with corner brackets, centred between the brackets' top and bottom rows.
static int row3_base(int h) {
  if (s_settings.line_style != LINES_BRACKETS) return ROW3_Y + ROW3_H;
  const int top = ROW3_LINE_Y + 1, bottom = LCD_Y + LCD_H - 2;
  return (top + bottom + 1) / 2 + h / 2;
}

// The date as "DD-MM" or "MM-DD". As on the W-221H, its digits are about 72% as
// tall as the right box's and share their baseline (so the time stands out), centred
// in the date box.
static void draw_date(void) {
  const GColor ink = s_col[COL_DATE];
  int first = s_settings.day_first ? s_now.tm_mday : s_now.tm_mon + 1;
  int second = s_settings.day_first ? s_now.tm_mon + 1 : s_now.tm_mday;
  const int w = 15, h = 26;
  const int y = row3_base(h) - h;  // bottom aligned with the right box's digits
  const bool blank_first = s_settings.date_pad != PAD_ZERO;
  const bool blank_second = s_settings.date_pad == PAD_BOTH_BLANK;
  if (font_active()) {
    char text[8];
    snprintf(text, sizeof(text), "%c%d-%c%d", (first >= 10 || !blank_first) ? '0' + first / 10 : ' ',
             first % 10, (second >= 10 || !blank_second) ? '0' + second / 10 : ' ', second % 10);
    font_draw_text(FG_DATE, text, 0, ROW3_DIV_X, row3_base(font_height(FG_DATE)), ALIGN_CENTER, ink,
                   DENSITY_FULL);
    return;
  }
  draw_digit(27, y, w, h, (first >= 10 || !blank_first) ? first / 10 : DIGIT_BLANK, ink);
  draw_digit(46, y, w, h, first % 10, ink);
  draw_segments(64, y, 8, h, SEG_G, SEG_G, ink);  // dash: the font's middle bar
  draw_digit(75, y, w, h, (second >= 10 || !blank_second) ? second / 10 : DIGIT_BLANK, ink);
  draw_digit(94, y, w, h, second % 10, ink);
}

// With the date box showing the high and low: the day of the month after the (narrowed) weekday,
// in 7-segment digits as tall as the weekday letters, so the row reads e.g. "MON 05".
static void draw_month_day(void) {
  const int d = s_now.tm_mday, w = 11, h = BOX_BOTTOM - BOX_TOP;
  const bool blank = d < 10 && s_settings.date_pad != PAD_ZERO;
  if (font_active()) {
    char text[3] = { blank ? ' ' : '0' + d / 10, '0' + d % 10, 0 };
    font_draw_text(FG_DATE, text, 75, 0, BOX_BOTTOM, ALIGN_LEFT, s_col[COL_WEEKDAY], DENSITY_FULL);
    return;
  }
  draw_digit(75, WEEKDAY_Y, w, h, blank ? DIGIT_BLANK : d / 10, s_col[COL_WEEKDAY]);
  draw_digit(89, WEEKDAY_Y, w, h, d % 10, s_col[COL_WEEKDAY]);
}

// Right box, option 1: the seconds, two digits centred in the box.
static void draw_seconds(void) {
  const GColor ink = s_col[COL_RIGHT];
  if (font_active()) {
    char text[3] = { '0' + s_now.tm_sec / 10, '0' + s_now.tm_sec % 10, 0 };
    font_draw_text(FG_RIGHT, text, ROW3_DIV_X + 2, LCD_W - ROW3_DIV_X - 2, row3_base(font_height(FG_RIGHT)),
                   ALIGN_CENTER, ink, DENSITY_FULL);
    return;
  }
  const int top = row3_base(ROW3_H) - ROW3_H;
  draw_digit(143, top, RIGHT_DIGIT_W, ROW3_H, s_now.tm_sec / 10, ink);
  draw_digit(165, top, RIGHT_DIGIT_W, ROW3_H, s_now.tm_sec % 10, ink);
}

// Right box, option 2: the temperature. A half-width sign slot (minus, or the
// "1" of 100+ in Fahrenheit) followed by two digits, so -99..199 all fit; "--"
// while there is no recent weather.
static void draw_temperature(void) {
  const GColor ink = s_col[COL_RIGHT];
  const int dy = row3_base(ROW3_H) - ROW3_H, dh = ROW3_H;
  int temp = display_temp(s_weather.temp), v = abs(temp);
  bool valid = weather_valid();
  bool neg = valid && temp < 0, hundred = valid && v >= 100;
  if (font_active()) {
    // Right-aligned. Below zero or from 100 up there is no degree mark, so the font can stay
    // as big as two digits and a degree mark allow (tools/gen_fonts.py).
    char text[8];
    if (!valid) snprintf(text, sizeof(text), "--%c", DEGREE_CHAR);
    else if (neg || hundred) snprintf(text, sizeof(text), "%d", temp);
    else snprintf(text, sizeof(text), "%d%c", temp, DEGREE_CHAR);
    font_draw_text(FG_RIGHT, text, ROW3_DIV_X + 2, LCD_W - ROW3_DIV_X - 5, row3_base(font_height(FG_RIGHT)),
                   ALIGN_RIGHT, ink, DENSITY_FULL);
    return;
  }
  // The minus is the font's middle bar in a narrow box; the "1" is the right
  // verticals of a full-width digit placed so they land in the sign slot.
  // Unlit parts go first so the lit one is never covered by a ghost.
  const int w = RIGHT_DIGIT_W;
  if (!neg) draw_segments(TEMP_MINUS_X, dy, 13, dh, 0, SEG_G, ink);
  if (!hundred) draw_segments(TEMP_ONE_X, dy, w, dh, 0, SEG_B | SEG_C, ink);
  if (neg) draw_segments(TEMP_MINUS_X, dy, 13, dh, SEG_G, SEG_G, ink);
  if (hundred) draw_segments(TEMP_ONE_X, dy, w, dh, SEG_B | SEG_C, SEG_B | SEG_C, ink);
  if (!valid) {
    draw_digit(TEMP_D1_X, dy, w, dh, DIGIT_MINUS, ink);
    draw_digit(TEMP_D2_X, dy, w, dh, DIGIT_MINUS, ink);
  } else {
    int tens = (v >= 10) ? v / 10 % 10 : DIGIT_BLANK;
    draw_digit(TEMP_D1_X, dy, w, dh, tens, ink);
    draw_digit(TEMP_D2_X, dy, w, dh, v % 10, ink);
  }
  // Degree mark: a round 7x7 ring, 2px thick, shifted with the digits' slanted
  // top edge.
  static const char DEGREE_BITS[] =
    "..###.."
    ".#####."
    "##...##"
    "##...##"
    "##...##"
    ".#####."
    "..###..";
  draw_dots(TEMP_DEG_X + (dh - 4) * s_slant / 1000, dy, DEGREE_BITS, 7, 7, 1, 1, true, ink);
}

// The date box's alternative to the date: today's low and high side by side, bottom-aligned
// with the right box's digits, with a short divider between them. Each is a down or up arrow
// (with a minus above it for a temperature below zero), a narrow slot for the "1" of 100+ and
// two digits; no degree mark (the right box's temperature has one, and the space goes to bigger
// digits).
#define RANGE_DIGIT_W 15
#define RANGE_DIGIT_H 30
#define RANGE_LOW_X 1     // left edge of the low's arrow
#define RANGE_HIGH_X 65   // and of the high's
#define RANGE_DIV_X 60    // the divider between them
static void draw_range_value(int x, int t10, bool valid, const char *arrow, GColor ink) {
  const int h = font_active() ? font_height(FG_RANGE) : RANGE_DIGIT_H, w = RANGE_DIGIT_W;
  const int y = row3_base(h) - h;
  const int d1 = x + 20, d2 = x + 37;  // the digits
  int temp = display_temp(t10), v = abs(temp);
  bool neg = valid && temp < 0, hundred = valid && v >= 100;
  // The arrow sits level with the middle of the digits, shifted with their slant, and a
  // below-zero minus (as thick as a digit's bar) above it.
  const int ay = y + (h - 6) / 2, ax = x + (h / 2) * s_slant / 1000;
  draw_dots(ax, ay, arrow, 11, 6, 1, 1, true, ink);
  if (neg) fill(ax + 1 + 10 * s_slant / 1000, ay - 10, 9, 3, ink, false);
  if (font_active()) {  // right-aligned after the arrow (100 and up runs a little further left)
    char text[6];
    if (valid) snprintf(text, sizeof(text), "%d", v);
    else snprintf(text, sizeof(text), "--");
    font_draw_text(FG_RANGE, text, x + 12, 40, y + h, ALIGN_RIGHT, ink, DENSITY_FULL);
    return;
  }
  // The "1" of 100+: the right verticals of a digit placed so they land between the arrow and
  // the first digit. No unlit ghost at this size: it would crowd the digits.
  if (hundred) draw_segments(x + 3, y, w, h, SEG_B | SEG_C, SEG_B | SEG_C, ink);
  if (!valid) {
    draw_digit(d1, y, w, h, DIGIT_MINUS, ink);
    draw_digit(d2, y, w, h, DIGIT_MINUS, ink);
  } else {
    draw_digit(d1, y, w, h, (v >= 10) ? v / 10 % 10 : DIGIT_BLANK, ink);
    draw_digit(d2, y, w, h, v % 10, ink);
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
  static const char UP[] =
    ".....#....."
    "....###...."
    "...#####..."
    "..#######.."
    ".#########."
    "###########";
  static const char DOWN[] =
    "###########"
    ".#########."
    "..#######.."
    "...#####..."
    "....###...."
    ".....#.....";
  const bool valid = range_valid();
  draw_range_value(RANGE_LOW_X, s_weather.temp_min, valid, DOWN, s_col[COL_DATE]);
  draw_range_value(RANGE_HIGH_X, s_weather.temp_max, valid, UP, s_col[COL_DATE]);
  const int base = row3_base(RANGE_DIGIT_H);
  draw_range_divider(RANGE_DIV_X, base - RANGE_DIGIT_H - 2, base + 1);
}

// A shake's burst of seconds is running. It also ends if the clock was set back meanwhile, so
// it can never last longer than seconds_burst_s.
static bool burst_active(void) {
  if (s_burst_until == 0) return false;
  const time_t now = time(NULL);
  return now < s_burst_until && s_burst_until - now <= s_settings.seconds_burst_s;
}

// Seconds in the right box: always, or (seconds_on_shake) only during a burst after a wrist
// shake, with the temperature there the rest of the time.
static bool seconds_showing(void) {
  if (!s_settings.show_seconds) return false;
  return !s_settings.seconds_on_shake || burst_active();
}

// Viewfinder corners (8 px arms) of the box x0..x1, y0..y1.
static void draw_brackets(int x0, int x1, int y0, int y1, GColor c) {
  const int a = 8;
  fill(x0, y0, a, 1, c, false);
  fill(x0, y0, 1, a, c, false);
  fill(x1 - a + 1, y0, a, 1, c, false);
  fill(x1, y0, 1, a, c, false);
  fill(x0, y1, a, 1, c, false);
  fill(x0, y1 - a + 1, 1, a, c, false);
  fill(x1 - a + 1, y1, a, 1, c, false);
  fill(x1, y1 - a + 1, 1, a, c, false);
}

// The lines between the time and the bottom row and between the date box and the right box, in
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
    case LINES_BRACKETS:  // no lines: corners around the two boxes
      draw_brackets(2, xd - 3, y0 + 1, yb - 1, c);
      draw_brackets(xd + 4, LCD_W - 3, y0 + 1, yb - 1, c);
      break;
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

// Everything on the white LCD panel, drawn straight into the framebuffer.
static void draw_lcd(void) {
  const int day_x = s_settings.date_range ? WEEKDAY_NARROW_X : WEEKDAY_X;
  if (font_active()) {  // on the weekday row's bottom line
    font_draw_text(FG_WEEKDAY, DAYS[s_now.tm_wday], day_x, 0, BOX_BOTTOM, ALIGN_LEFT, s_col[COL_WEEKDAY],
                   DENSITY_FULL);
  } else {
    draw_day(day_x, WEEKDAY_Y, DAYS[s_now.tm_wday], s_settings.date_range, s_col[COL_WEEKDAY]);
  }
  if (s_settings.date_range) draw_month_day();
  draw_indicator_frame();
  draw_time();
  draw_rules();
  if (s_settings.date_range) draw_temperature_range();
  else draw_date();
  if (seconds_showing()) draw_seconds();
  else draw_temperature();
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

// Top bezel (Gothic 18 bold): a battery icon and the level, and a walking figure and the step
// count; or the custom texts when those are switched off. The right text takes the width it
// needs (up to TOP_RIGHT_MAX) and the left text gets the rest.
static void draw_top_bezel(GContext *ctx) {
  static const char *const WALKER[] = {
    "...##.....", "...##.....", "..........", "..####....", ".#.###....", "#..##.##..",
    "...##.....", "..#..#....", ".#...#....", "#.....#...", "......#...",
  };
  enum { ICON_GAP = 4, BATTERY_W = 20, WALKER_W = 10 };
  char left[24], right[24];
  if (s_settings.show_battery) {
    snprintf(left, sizeof(left), "%d%%", s_battery.charge_percent);
  } else {
    snprintf(left, sizeof(left), "%s", s_settings.top_left);
  }
  if (!s_settings.show_steps) {
    snprintf(right, sizeof(right), "%s", s_settings.top_right);
  } else if (s_steps >= 1000) {
    snprintf(right, sizeof(right), "%d,%03d", s_steps / 1000, s_steps % 1000);
  } else if (s_steps >= 0) {
    snprintf(right, sizeof(right), "%d", s_steps);
  } else {
    snprintf(right, sizeof(right), "--");
  }
  int right_w = graphics_text_layout_get_content_size(
      right, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD), GRect(0, 0, 190, 22),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentRight).w + 4;
  if (right_w > TOP_RIGHT_MAX) right_w = TOP_RIGHT_MAX;
  draw_text(ctx, right, GRect(190 - right_w, -2, right_w, 22), GTextAlignmentRight, s_col[COL_TOP_RIGHT]);
  int right_start = 190 - right_w;
  if (s_settings.show_steps) {  // the figure just left of the number
    right_start -= WALKER_W + ICON_GAP;
    draw_icon(ctx, right_start + 4, 5, WALKER, ARRAY_LENGTH(WALKER), s_col[COL_TOP_RIGHT]);
  }
  int left_x = 10;
  if (s_settings.show_battery) {
    draw_battery_icon(ctx, left_x, 6, s_col[COL_TOP_LEFT]);
    left_x += BATTERY_W + ICON_GAP;
  }
  draw_text(ctx, left, GRect(left_x, -2, right_start - left_x - 4, 22), GTextAlignmentLeft, s_col[COL_TOP_LEFT]);
}

// Bottom bezel (larger text): the WR badge (or HR badge and heart rate) and the
// custom label.
static void draw_bottom_bezel(GContext *ctx) {
  graphics_context_set_stroke_color(ctx, s_col[COL_BADGE]);
  graphics_draw_round_rect(ctx, GRect(10, BOTTOM_CAP - 3, 30, 20), 3);
  draw_bezel_text(ctx, s_settings.heart_rate ? "HR" : "WR", 10, BOTTOM_CAP, 30, GTextAlignmentCenter,
                  s_col[COL_BADGE]);
  if (s_settings.heart_rate) {
    char hr_text[12];
    if (s_hr > 0) snprintf(hr_text, sizeof(hr_text), "%d", s_hr);
    else snprintf(hr_text, sizeof(hr_text), "--");
    draw_bezel_text(ctx, hr_text, 44, BOTTOM_CAP, 36, GTextAlignmentLeft, s_col[COL_HEART]);
  }
  draw_bezel_text(ctx, s_settings.bezel_label, 82, BOTTOM_CAP, 108, GTextAlignmentRight,
                  s_col[COL_LABEL]);
}

// The indicator box's labels: lit (ink) when active, faint otherwise. DST is lit while daylight
// saving time is in effect in the watch's time zone (the phone provides the zone; the watch's
// own clock knows when DST applies). Each is fitted to LABEL_H rows and centred vertically in
// its cell (the cells are the rows between the 2px frame and the middle divider); "BT" is also
// widened. Active labels gather on the right (BT, DST), the usually inactive ones on the left
// (CHG, MUTE). The BT and MUTE cells stop 1px short of the rounded corners that intrude.
static void draw_indicator_labels(GContext *ctx) {
  const int top_h = BOX_MID - BOX_TOP - 2, bottom_h = BOX_BOTTOM - BOX_MID - 2;
  const int left_w = BOX_DIV - BOX_LEFT, right_w = BOX_RIGHT - BOX_DIV;
  static const int8_t BT_WIDTHS[] = { 8, 9 };  // widened B and T
  // On the charger but no longer charging: the battery is full.
  const bool full = s_battery.is_plugged && !s_battery.is_charging;
  struct { const char *label; bool on; GRect cell; const int8_t *letter_w; int letter_w_n; } ind[] = {
    { "BT",   s_connected,
      GRect(BOX_DIV + 1, BOX_TOP + 2, right_w - 4, top_h), BT_WIDTHS, ARRAY_LENGTH(BT_WIDTHS) },
    { full ? "FULL" : "CHG", s_battery.is_charging || full,
      GRect(BOX_LEFT + 2, BOX_TOP + 2, left_w - 3, top_h), NULL, 0 },
    { "DST",  s_now.tm_isdst > 0,
      GRect(BOX_DIV + 1, BOX_MID + 1, right_w - 2, bottom_h), NULL, 0 },
    { "MUTE", s_quiet,
      GRect(BOX_LEFT + 4, BOX_MID + 1, left_w - 4, bottom_h), NULL, 0 },
  };
  // With a font, the labels are its glyphs, centred in their cells (drawn into the framebuffer).
  if (font_active() && !(s_fb = graphics_capture_frame_buffer(ctx))) return;
  for (unsigned i = 0; i < ARRAY_LENGTH(ind); i++) {
    if (!ind[i].on && !s_settings.ghosts) continue;
    const GColor color = ind[i].on ? s_col[INDICATOR_COLORS[i]] : s_ghost;
    const int density = ind[i].on ? DENSITY_FULL : s_label_off_density;
    if (font_active()) {
      const GRect c = ind[i].cell;
      font_draw_text(FG_LABEL, ind[i].label, c.origin.x, c.size.w,
                     c.origin.y + (c.size.h + font_height(FG_LABEL)) / 2, ALIGN_CENTER, color, density);
    } else {
      draw_fitted_text(ctx, ind[i].label, ind[i].cell, ind[i].letter_w, ind[i].letter_w_n, LABEL_H,
                       color, density);
    }
  }
  if (font_active()) {
    graphics_release_frame_buffer(ctx, s_fb);
    s_fb = NULL;
  }
}

// The charcoal case: dark gray dots over the black, on rows y0..y1, `density` out of DENSITY_FULL
// with the same ordered dither as the ghosts (8 = a checkerboard).
static void draw_case_pattern(int y0, int y1, int density) {
  for (int y = y0; y <= y1; y++) {
    GBitmapDataRowInfo row = gbitmap_get_data_row_info(s_fb, y);
    for (int x = row.min_x; x <= row.max_x; x++) {
      if (BAYER4[y & 3][x & 3] < density) row.data[x] = GColorDarkGrayARGB8;
    }
  }
}

static void canvas_update(Layer *layer, GContext *ctx) {
  // Case frame and LCD window.
  graphics_context_set_fill_color(ctx, s_col[COL_CASE]);
  graphics_fill_rect(ctx, layer_get_bounds(layer), 0, GCornerNone);
  graphics_context_set_fill_color(ctx, s_col[COL_EDGE]);
  graphics_fill_rect(ctx, GRect(LCD_X, LCD_Y - 2, LCD_W, LCD_H + 4), 0, GCornerNone);
  graphics_context_set_fill_color(ctx, s_lcd);
  graphics_fill_rect(ctx, GRect(LCD_X, LCD_Y, LCD_W, LCD_H), 0, GCornerNone);
  // A black LCD (inverted) would melt into a plain black case: mark the panel's edges. (With
  // custom colours the edge is a colour of its own; a charcoal case stands apart by itself.)
  // COL_CASE's custom colour is no longer offered: the case is always Case color.
  const bool charcoal = !s_settings.silver && s_settings.case_pattern != CASE_SOLID;
  if (!s_colors.enabled && s_settings.inverted && s_col[COL_CASE].argb == GColorBlackARGB8 &&
      !charcoal) {
    graphics_context_set_fill_color(ctx, GColorDarkGray);
    graphics_fill_rect(ctx, GRect(LCD_X, LCD_Y - 1, LCD_W, 1), 0, GCornerNone);
    graphics_fill_rect(ctx, GRect(LCD_X, LCD_Y + LCD_H, LCD_W, 1), 0, GCornerNone);
  }

  s_quiet = quiet_time_is_active();
  s_fb = graphics_capture_frame_buffer(ctx);
  if (!s_fb) return;
  if (charcoal) {
    draw_case_pattern(0, LCD_Y - 3, s_settings.case_pattern * 4);
    draw_case_pattern(LCD_Y + LCD_H + 2, PBL_DISPLAY_HEIGHT - 1, s_settings.case_pattern * 4);
  }
  draw_lcd();
  graphics_release_frame_buffer(ctx, s_fb);
  s_fb = NULL;

  // Text goes through the regular text API, so it comes after the framebuffer
  // is released.
  draw_top_bezel(ctx);
  draw_bottom_bezel(ctx);
  draw_indicator_labels(ctx);
}

// ---------------------------------------------------------------------------
// Services

static void request_weather(void) {
  DictionaryIterator *iter;
  if (app_message_outbox_begin(&iter) != APP_MSG_OK) return;
  dict_write_uint8(iter, MESSAGE_KEY_RequestWeather, 1);
  app_message_outbox_send();
}

// Asks the phone for weather only when it would be shown and used: the temperature is on
// screen (not the seconds), the phone is connected, and the last reading is not recent.
// Each request wakes the phone app for a location fix and a web request.
static void refresh_weather_if_needed(void) {
  // The temperature is on screen unless the seconds are always shown (and the date box shows
  // the date).
  if ((s_settings.show_seconds && !s_settings.seconds_on_shake && !s_settings.date_range) ||
      !s_connected) return;
  // A recent reading is enough, except when the high and low on screen belong to yesterday.
  const bool stale_range = s_settings.date_range && !range_valid();
  if (!stale_range && s_weather.updated != 0 &&
      time(NULL) - s_weather.updated < (WEATHER_REFRESH_MIN - 5) * 60) return;
  request_weather();
}

static void tick_handler(struct tm *now, TimeUnits changed);

// Once a minute normally (plus the hour, so a whole-hour time zone change redraws at once);
// every second only while the seconds are shown AND the face is in front.
static void subscribe_ticks(void) {
  tick_timer_service_unsubscribe();
  const bool per_second = seconds_showing() && s_focus;
  tick_timer_service_subscribe(per_second ? SECOND_UNIT : (MINUTE_UNIT | HOUR_UNIT), tick_handler);
}

// A wrist shake: start (or extend) the burst of ticking seconds.
static void start_seconds_burst(void) {
  if (!s_focus || !s_settings.show_seconds || !s_settings.seconds_on_shake) return;
  s_burst_until = time(NULL) + s_settings.seconds_burst_s;
  subscribe_ticks();
  time_t now = time(NULL);
  s_now = *localtime(&now);
  layer_mark_dirty(s_canvas);
}

static void shake_handler(AccelAxisType axis, int32_t direction) { start_seconds_burst(); }

// The accelerometer's shake detection (the SDK calls it the "tap" service) is only listened to
// while the setting needs it.
static void update_shake_subscription(void) {
  accel_tap_service_unsubscribe();
  if (s_settings.show_seconds && s_settings.seconds_on_shake) {
    accel_tap_service_subscribe(shake_handler);
  } else {
    s_burst_until = 0;
  }
}

// A notification or menu covering the face makes per-second redraws pointless.
static void focus_handler(bool in_focus) {
  s_focus = in_focus;
  subscribe_ticks();
  if (in_focus) {
    time_t now = time(NULL);
    s_now = *localtime(&now);
    layer_mark_dirty(s_canvas);
  }
}

static void tick_handler(struct tm *now, TimeUnits changed) {
  s_now = *now;
  if (s_burst_until != 0 && !burst_active()) {
    s_burst_until = 0;      // the burst is over: back to a tick a minute and the temperature
    subscribe_ticks();
  }
  if (changed & MINUTE_UNIT) {
    if (s_settings.show_steps) update_steps();
    if (now->tm_min % WEATHER_REFRESH_MIN == 0) refresh_weather_if_needed();
  }
  layer_mark_dirty(s_canvas);
  // The hourly vibration: on the hour, while the face is showing, never during Quiet Time.
  if (now->tm_min == 0 && now->tm_sec == 0 && s_settings.hourly_vibe == HOURLY_VIBE_ON &&
      !quiet_time_is_active()) {
    vibes_double_pulse();
  }
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

// Health events wake the app, so they are only listened to while the heart rate is shown. (The
// step count is read on the minute tick, which redraws the face anyway.)
static void update_health_subscription(void) {
#if defined(PBL_HEALTH)
  health_service_events_unsubscribe();
  if (s_settings.heart_rate) {
    update_heart_rate();
    health_service_events_subscribe(health_handler, NULL);
  }
#endif
}

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
  if ((t = dict_find(iter, MESSAGE_KEY_DateFormat))) {
    // "DM" or "MD", or "minmax": today's low and high in the date box (the day of the month
    // moves up next to the weekday; day_first keeps its last value for when the date returns).
    const char *f = t->value->cstring;
    s_settings.date_range = strcmp(f, "minmax") == 0 ? 1 : 0;
    if (!s_settings.date_range) s_settings.day_first = strcmp(f, "DM") == 0;
    settings_changed = true;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_TimeZero))) {
    s_settings.hour_no_zero = tuple_int(t) ? 0 : 1;
    settings_changed = true;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_DatePadding))) {
    const char *p = t->value->cstring;
    s_settings.date_pad = strcmp(p, "first") == 0 ? PAD_FIRST_BLANK : strcmp(p, "both") == 0 ? PAD_BOTH_BLANK : PAD_ZERO;
    settings_changed = true;
  }
  // Right box
  if ((t = dict_find(iter, MESSAGE_KEY_RightBox))) {
    s_settings.show_seconds = strcmp(t->value->cstring, "seconds") == 0;
    settings_changed = true;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_SecondsMode))) {
    s_settings.seconds_on_shake = strcmp(t->value->cstring, "shake") == 0 ? 1 : 0;
    settings_changed = true;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_SecondsDuration))) {
    int v = tuple_int(t);
    s_settings.seconds_burst_s = v < SECONDS_BURST_MIN_S ? SECONDS_BURST_MIN_S
        : v > SECONDS_BURST_MAX_S ? SECONDS_BURST_MAX_S : v;
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
  if ((t = dict_find(iter, MESSAGE_KEY_HeartRate))) {
    s_settings.heart_rate = tuple_int(t);
    settings_changed = true;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_BezelLabel))) {
    copy_upper(s_settings.bezel_label, sizeof(s_settings.bezel_label), t->value->cstring);
    settings_changed = true;
  }
  // Appearance
  bool font_changed = false;
  if ((t = dict_find(iter, MESSAGE_KEY_DigitStyle))) {
    const char *d = t->value->cstring;
    const uint8_t style = strcmp(d, "oxanium") == 0 ? DIGITS_OXANIUM : strcmp(d, "chakra") == 0 ? DIGITS_CHAKRA
        : strcmp(d, "orbitron") == 0 ? DIGITS_ORBITRON : DIGITS_SEGMENT;
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
    s_settings.case_pattern = strcmp(c, "dots") == 0 ? CASE_DOTS
        : strcmp(c, "checker") == 0 ? CASE_CHECKER : CASE_SOLID;
    settings_changed = true;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_Inverted))) {
    s_settings.inverted = tuple_int(t);
    settings_changed = true;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_Slanted))) {
    s_settings.slanted = tuple_int(t);
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
  if ((t = dict_find(iter, MESSAGE_KEY_HourlyVibe))) {
    s_settings.hourly_vibe = tuple_int(t) ? HOURLY_VIBE_ON : 0;
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
    update_shake_subscription();
    update_health_subscription();
    subscribe_ticks();
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
    .day_first = true,
    .show_battery = true,
    .top_left = "30 DAY BATT",
    .show_steps = true,
    .top_right = "WR 3ATM",
    .bezel_label = "PEBBLE",
    .slanted = true,
    .ghosts = true,
    .backlight = BACKLIGHT_SYSTEM,
    .vibe_disconnect = VIBE_DOUBLE,
    .vibe_connect = VIBE_SHORT,
    .seconds_burst_s = SECONDS_BURST_DEFAULT_S,
  };
  // Settings saved by older versions are shorter: the fields added at the end keep their defaults.
  persist_read_data(SETTINGS_KEY, &s_settings, sizeof(s_settings));
  // Old saves left padding where these two now live.
  if (s_settings.seconds_on_shake > 1) s_settings.seconds_on_shake = 0;
  if (s_settings.hour_no_zero > 1) s_settings.hour_no_zero = 0;
  if (s_settings.date_pad > PAD_BOTH_BLANK) s_settings.date_pad = PAD_ZERO;
  if (s_settings.date_range > 1) s_settings.date_range = 0;
  if (s_settings.case_pattern > CASE_CHECKER) s_settings.case_pattern = CASE_SOLID;
  if (s_settings.digit_style >= DIGITS_COUNT) s_settings.digit_style = DIGITS_SEGMENT;
  if (s_settings.line_style >= LINES_COUNT) s_settings.line_style = LINES_SOLID;
  load_digit_font();
  if (s_settings.hourly_vibe != HOURLY_VIBE_ON) s_settings.hourly_vibe = 0;  // the chime's sounds are gone
  if (s_settings.seconds_burst_s < SECONDS_BURST_MIN_S || s_settings.seconds_burst_s > SECONDS_BURST_MAX_S) {
    s_settings.seconds_burst_s = SECONDS_BURST_DEFAULT_S;
  }
  persist_read_data(WEATHER_KEY, &s_weather, sizeof(s_weather));
  if (s_weather.has_range > 1) s_weather.has_range = 0;
  s_colors.enabled = 0;
  memcpy(s_colors.argb, COLOR_DEFAULTS, sizeof(s_colors.argb));
  persist_read_data(COLORS_KEY, &s_colors, sizeof(s_colors));
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

  update_shake_subscription();
  subscribe_ticks();
  battery_state_service_subscribe(battery_handler);
  app_focus_service_subscribe(focus_handler);
  connection_service_subscribe(
      (ConnectionHandlers){ .pebble_app_connection_handler = connection_handler });
  update_health_subscription();

  app_message_register_inbox_received(inbox_handler);
  // The inbox must hold a full settings Save: about 230 bytes with plain text and over 400
  // with emoji in the three custom texts, plus 11 bytes for each of the 24 colour settings.
  // The outbox only ever carries the one-byte weather request.
  app_message_open(1024, dict_calc_buffer_size(1, sizeof(uint8_t)));
}

static void deinit(void) {
  tick_timer_service_unsubscribe();
  battery_state_service_unsubscribe();
  app_focus_service_unsubscribe();
  accel_tap_service_unsubscribe();
  connection_service_unsubscribe();
#if defined(PBL_HEALTH)
  health_service_events_unsubscribe();
#endif
  window_destroy(s_window);
  if (s_font) free(s_font);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}

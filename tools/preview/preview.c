// The watch face's own drawing code (src/c/main.c), built as WebAssembly for the live preview
// on the settings page. build.sh compiles it; src/pkjs/custom-clay.js runs it.
//
// The page hands the current (unsaved) settings to main.c's own inbox handler, exactly as the
// watch receives them on Save, then main.c draws a frame into FB. Only the system font (the
// 7-segment style's indicator labels, and bezel texts the bezel font can't draw) is drawn by the
// page, with the phone's own bold font in place of Pebble's Gothic. Sensor values are made up.
#include "pebble.h"
#define main watch_main
#include "../../src/c/main.c"
#undef main

// ---------------------------------------------------------------------------
// From the page (the "env" imports).

extern void js_text(const char *text, int big, int x, int y, int w, int h, int align, int argb);
extern int js_text_width(const char *text, int big);
extern int js_resource_size(int id);
extern void js_resource_load(int id, uint8_t *buf, int size);

// ---------------------------------------------------------------------------
// The C library bits main.c uses.

void *memset(void *d, int c, size_t n) { uint8_t *p = d; while (n--) *p++ = (uint8_t)c; return d; }
void *memcpy(void *d, const void *s, size_t n) {
  uint8_t *p = d; const uint8_t *q = s; while (n--) *p++ = *q++; return d;
}
int strcmp(const char *a, const char *b) {
  while (*a && *a == *b) { a++; b++; }
  return (unsigned char)*a - (unsigned char)*b;
}
size_t strlen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }
char *strncpy(char *d, const char *s, size_t n) {
  size_t i = 0;
  for (; i < n && s[i]; i++) d[i] = s[i];
  for (; i < n; i++) d[i] = '\0';
  return d;
}
int abs(int v) { return v < 0 ? -v : v; }
int atoi(const char *s) {
  int sign = 1, v = 0;
  while (*s == ' ') s++;
  if (*s == '-' || *s == '+') sign = *s++ == '-' ? -1 : 1;
  while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
  return sign * v;
}

// The only allocation is the digit font (one at a time): a bump allocator is plenty for a page
// that is open for a minute.
extern unsigned char __heap_base;
static uintptr_t s_heap = (uintptr_t)&__heap_base;
void *malloc(size_t n) {
  uintptr_t p = (s_heap + 7) & ~(uintptr_t)7;
  size_t end = p + n, have = __builtin_wasm_memory_size(0) * 65536;
  if (end > have && __builtin_wasm_memory_grow(0, (end - have + 65535) / 65536) < 0) return NULL;
  s_heap = end;
  return (void *)p;
}
void free(void *p) {}

// snprintf for the formats main.c uses: %d (with a zero-padded width), %s, %c and %%.
int snprintf(char *out, size_t size, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  size_t n = 0;
#define PUT(ch) do { if (n + 1 < size) out[n] = (ch); n++; } while (0)
  for (; *fmt; fmt++) {
    if (*fmt != '%') { PUT(*fmt); continue; }
    fmt++;
    int width = 0;
    if (*fmt == '0') fmt++;
    while (*fmt >= '0' && *fmt <= '9') width = width * 10 + (*fmt++ - '0');
    if (*fmt == 'd') {
      int v = va_arg(ap, int);
      unsigned u = v < 0 ? -(unsigned)v : (unsigned)v;
      char digits[12]; int k = 0;
      do { digits[k++] = '0' + u % 10; u /= 10; } while (u);
      if (v < 0) PUT('-');
      for (int i = k; i < width; i++) PUT('0');
      while (k) PUT(digits[--k]);
    } else if (*fmt == 's') {
      for (const char *s = va_arg(ap, const char *); *s; s++) PUT(*s);
    } else if (*fmt == 'c') {
      PUT((char)va_arg(ap, int));
    } else if (*fmt == '%') {
      PUT('%');
    }
  }
#undef PUT
  if (size) out[n < size ? n : size - 1] = '\0';
  va_end(ap);
  return (int)n;
}

// The page's clock: preview_time() sets it; every time is "now".
static time_t s_time;
static struct tm s_tm;
time_t time(time_t *t) { if (t) *t = s_time; return s_time; }
struct tm *localtime(const time_t *t) { return &s_tm; }

// ---------------------------------------------------------------------------
// The SDK: drawing into FB (8-bit ARGB, like the watch's framebuffer).

static uint8_t FB[PBL_DISPLAY_HEIGHT][PBL_DISPLAY_WIDTH];
struct GBitmap { int unused; };
static struct GBitmap s_bitmap;
static GColor s_fill_color, s_stroke_color, s_text_color;
static bool s_24h = true;

GColor GColorFromHEX(uint32_t h) {
  return (GColor){ .a = 3, .r = (h >> 22) & 3, .g = (h >> 14) & 3, .b = (h >> 6) & 3 };
}
GBitmapDataRowInfo gbitmap_get_data_row_info(const GBitmap *b, uint16_t y) {
  return (GBitmapDataRowInfo){ FB[y], 0, PBL_DISPLAY_WIDTH - 1 };
}
GBitmap *graphics_capture_frame_buffer(GContext *c) { return &s_bitmap; }
bool graphics_release_frame_buffer(GContext *c, GBitmap *b) { return true; }
void graphics_context_set_fill_color(GContext *c, GColor g) { s_fill_color = g; }
void graphics_context_set_stroke_color(GContext *c, GColor g) { s_stroke_color = g; }
void graphics_context_set_text_color(GContext *c, GColor g) { s_text_color = g; }

static void put_rect(int x0, int y0, int w, int h, GColor c) {
  for (int y = y0; y < y0 + h; y++) {
    for (int x = x0; x < x0 + w; x++) {
      if (x >= 0 && x < PBL_DISPLAY_WIDTH && y >= 0 && y < PBL_DISPLAY_HEIGHT) FB[y][x] = c.argb;
    }
  }
}
void graphics_fill_rect(GContext *c, GRect r, uint16_t radius, GCornerMask m) {
  put_rect(r.origin.x, r.origin.y, r.size.w, r.size.h, s_fill_color);
}
void graphics_draw_rect(GContext *c, GRect r) {
  const int x = r.origin.x, y = r.origin.y, w = r.size.w, h = r.size.h;
  put_rect(x, y, w, 1, s_stroke_color);
  put_rect(x, y + h - 1, w, 1, s_stroke_color);
  put_rect(x, y, 1, h, s_stroke_color);
  put_rect(x + w - 1, y, 1, h, s_stroke_color);
}
// A 1px outline with quarter-circle corners of `radius`.
void graphics_draw_round_rect(GContext *c, GRect r, uint16_t radius) {
  const int x = r.origin.x, y = r.origin.y, w = r.size.w, h = r.size.h, k = radius;
  put_rect(x + k, y, w - 2 * k, 1, s_stroke_color);
  put_rect(x + k, y + h - 1, w - 2 * k, 1, s_stroke_color);
  put_rect(x, y + k, 1, h - 2 * k, s_stroke_color);
  put_rect(x + w - 1, y + k, 1, h - 2 * k, s_stroke_color);
  for (int i = 0; i < k; i++) {  // the corners' pixels, inset by how far the arc is from the box
    int j = k - i;               // rows from the corner's centre
    int d = 0;
    while ((d + 1) * (d + 1) + (j - 1) * (j - 1) < k * k) d++;
    int inset = k - d;
    for (int sx = 0; sx < 2; sx++) {
      for (int sy = 0; sy < 2; sy++) {
        int px = sx ? x + w - 1 - inset : x + inset, py = sy ? y + h - 1 - i : y + i;
        put_rect(px, py, 1, 1, s_stroke_color);
        int px2 = sx ? x + w - 1 - i : x + i, py2 = sy ? y + h - 1 - inset : y + inset;
        put_rect(px2, py2, 1, 1, s_stroke_color);
      }
    }
  }
}
GRect layer_get_bounds(Layer *l) { return GRect(0, 0, PBL_DISPLAY_WIDTH, PBL_DISPLAY_HEIGHT); }

// The two system fonts main.c uses; the page draws them into FB.
GFont fonts_get_system_font(const char *key) { return (GFont)(uintptr_t)(key[0] == 'b'); }
void graphics_draw_text(GContext *c, const char *text, GFont f, GRect r, GTextOverflowMode o,
                        GTextAlignment a, void *attrs) {
  js_text(text, (int)(uintptr_t)f, r.origin.x, r.origin.y, r.size.w, r.size.h, a, s_text_color.argb);
}
GSize graphics_text_layout_get_content_size(const char *text, GFont f, GRect r, GTextOverflowMode o,
                                            GTextAlignment a) {
  int w = js_text_width(text, (int)(uintptr_t)f);
  return (GSize){ w > r.size.w ? r.size.w : w, 18 };
}

// The digit fonts (resources/fonts/*.bin), handed over by the page.
ResHandle resource_get_handle(uint32_t id) { return (ResHandle)(uintptr_t)id; }
size_t resource_size(ResHandle h) { return js_resource_size((int)(uintptr_t)h); }
size_t resource_load(ResHandle h, uint8_t *buf, size_t n) {
  js_resource_load((int)(uintptr_t)h, buf, (int)n);
  return n;
}

// ---------------------------------------------------------------------------
// The rest of the SDK: made-up readings, and services that do nothing here.

bool clock_is_24h_style(void) { return s_24h; }
bool quiet_time_is_active(void) { return false; }
bool speaker_is_muted(void) { return false; }
static int s_sample_battery = 80, s_sample_steps = 5234;  // see preview_sample()
BatteryChargeState battery_state_service_peek(void) {
  return (BatteryChargeState){ .charge_percent = s_sample_battery };
}
bool connection_service_peek_pebble_app_connection(void) { return true; }
HealthServiceAccessibilityMask health_service_metric_accessible(HealthMetric m, time_t a, time_t b) {
  return HealthServiceAccessibilityMaskAvailable;
}
HealthValue health_service_peek_current_value(HealthMetric m) { return 72; }
HealthValue health_service_sum_today(HealthMetric m) { return s_sample_steps; }
MeasurementSystem health_service_get_measurement_system_for_display(HealthMetric m) { return 0; }
time_t time_start_of_today(void) { return s_time - (s_tm.tm_hour * 60 + s_tm.tm_min) * 60 - s_tm.tm_sec; }
bool health_service_events_subscribe(HealthEventHandler h, void *c) { return true; }
bool health_service_events_unsubscribe(void) { return true; }
void light_set_system_color(void) {}
void light_set_color_rgb888(uint32_t rgb) {}
AppMessageResult app_message_outbox_begin(DictionaryIterator **it) { return 1; }  // never sends
AppMessageResult app_message_outbox_send(void) { return APP_MSG_OK; }
int dict_write_uint8(DictionaryIterator *it, uint32_t key, uint8_t v) { return 0; }
void tick_timer_service_subscribe(TimeUnits u, TickHandler h) {}
void tick_timer_service_unsubscribe(void) {}
void accel_tap_service_subscribe(AccelTapHandler h) {}
void accel_tap_service_unsubscribe(void) {}
void layer_mark_dirty(Layer *l) {}
void battery_state_service_subscribe(void (*h)(BatteryChargeState)) {}
void battery_state_service_unsubscribe(void) {}
void vibes_short_pulse(void) {}
void vibes_long_pulse(void) {}
void vibes_double_pulse(void) {}
void vibes_enqueue_custom_pattern(VibePattern p) {}
bool speaker_play_tone(uint32_t a, uint32_t b, uint8_t c, SpeakerWaveform w) { return false; }
bool speaker_play_notes(const SpeakerNote *n, uint32_t c, uint8_t v) { return false; }
void *app_timer_register(uint32_t ms, void (*cb)(void *), void *data) { return NULL; }
Layer *window_get_root_layer(Window *w) { return NULL; }
Layer *layer_create(GRect r) { return NULL; }
void layer_set_update_proc(Layer *l, LayerUpdateProc p) {}
void layer_add_child(Layer *a, Layer *b) {}
void layer_destroy(Layer *l) {}
int persist_read_data(uint32_t key, void *buf, size_t n) { return -1; }  // nothing saved: the defaults
bool persist_exists(uint32_t key) { return false; }
int persist_get_size(uint32_t key) { return -1; }
int persist_write_data(uint32_t key, const void *buf, size_t n) { return (int)n; }
Window *window_create(void) { return NULL; }
void window_destroy(Window *w) {}
void window_stack_push(Window *w, bool animated) {}
void window_set_window_handlers(Window *w, WindowHandlers h) {}
void app_focus_service_subscribe(void (*h)(bool)) {}
void app_focus_service_unsubscribe(void) {}
void connection_service_subscribe(ConnectionHandlers h) {}
void connection_service_unsubscribe(void) {}
void app_message_register_inbox_received(AppMessageInboxReceived h) {}
void app_message_open(uint32_t in, uint32_t out) {}
uint32_t dict_calc_buffer_size(uint8_t n, ...) { return 0; }
void app_event_loop(void) {}

// ---------------------------------------------------------------------------
// The message keys, numbered here, and a settings message built up by the page.

enum {
#define K(name) KEY_##name,
#include "keys.h"
#undef K
  KEY_COUNT
};
#define K(name) uint32_t MESSAGE_KEY_##name = KEY_##name + 1;
#include "keys.h"
#undef K
static const char *const KEY_NAMES[] = {
#define K(name) #name,
#include "keys.h"
#undef K
};

#define MSG_MAX 64
#define STR_MAX 128
// Each entry: a Tuple header, then its value (a number or up to STR_MAX bytes of text).
static uint32_t s_msg[MSG_MAX][(sizeof(Tuple) + STR_MAX + 3) / 4];
static int s_msg_n;
static char s_str[STR_MAX * 4];  // the page writes key names and text values here

Tuple *dict_find(const DictionaryIterator *it, uint32_t key) {
  for (int i = 0; i < s_msg_n; i++) {
    if (((Tuple *)s_msg[i])->key == key) return (Tuple *)s_msg[i];
  }
  return NULL;
}

static Tuple *add_entry(void) {
  for (int k = 0; k < KEY_COUNT; k++) {
    if (strcmp(KEY_NAMES[k], s_str) != 0) continue;
    if (s_msg_n == MSG_MAX) return NULL;
    Tuple *t = (Tuple *)s_msg[s_msg_n++];
    t->key = k + 1;
    return t;
  }
  return NULL;  // a setting the watch doesn't read
}

// ---------------------------------------------------------------------------
// For the page.

__attribute__((export_name("preview_str"))) char *preview_str(void) { return s_str; }
__attribute__((export_name("preview_fb"))) uint8_t *preview_fb(void) { return &FB[0][0]; }

// Other sample readings (for screenshots and tests), before preview_init().
__attribute__((export_name("preview_sample"))) void preview_sample(int battery, int steps) {
  s_sample_battery = battery;
  s_sample_steps = steps;
}

// Starts the face as on the watch with nothing saved, with a sample temperature and range.
__attribute__((export_name("preview_init"))) void preview_init(void) {
  init();
  s_weather = (Weather){ .temp = 174, .temp_min = -32, .temp_max = 214, .has_range = 1, .updated = s_time };
}

// Other sample weather (tenths of a degree Celsius), after preview_init().
__attribute__((export_name("preview_weather"))) void preview_weather(int temp, int temp_min, int temp_max) {
  s_weather.temp = temp;
  s_weather.temp_min = temp_min;
  s_weather.temp_max = temp_max;
}

// The page's local time; `is_24h` stands in for the watch's own 12/24-hour setting.
__attribute__((export_name("preview_time"))) void preview_time(
    double now, int year, int mon, int mday, int wday, int yday, int hour, int min, int sec, int dst,
    int is_24h) {
  s_time = (time_t)now;
  s_tm = (struct tm){ .tm_sec = sec, .tm_min = min, .tm_hour = hour, .tm_mday = mday, .tm_mon = mon,
                      .tm_year = year - 1900, .tm_wday = wday, .tm_yday = yday, .tm_isdst = dst };
  s_now = s_tm;
  s_24h = is_24h;
  s_weather.updated = s_time;
}

// A settings message: begin, then one add per setting (the key's name in preview_str()), then
// send, which runs main.c's inbox handler.
__attribute__((export_name("preview_begin"))) void preview_begin(void) { s_msg_n = 0; }
__attribute__((export_name("preview_add_int"))) void preview_add_int(int value) {
  Tuple *t = add_entry();
  if (!t) return;
  t->type = TUPLE_INT;
  t->length = 4;
  t->value->int32 = value;
}
// The text value follows the key's name in preview_str(), after its terminating zero.
__attribute__((export_name("preview_add_str"))) void preview_add_str(void) {
  Tuple *t = add_entry();
  if (!t) return;
  const char *v = s_str + strlen(s_str) + 1;
  size_t n = strlen(v);
  if (n > STR_MAX - 1) n = STR_MAX - 1;
  memcpy(t->value->cstring, v, n);
  t->value->cstring[n] = '\0';
  t->type = TUPLE_CSTRING;
  t->length = n + 1;
}
__attribute__((export_name("preview_send"))) void preview_send(void) { inbox_handler(NULL, NULL); }

__attribute__((export_name("preview_draw"))) void preview_draw(void) {
  memset(FB, 0, sizeof(FB));
  canvas_update(NULL, NULL);
}

// A stand-in for the Pebble SDK's pebble.h, just enough for src/c/main.c to build as
// WebAssembly for the settings page's live preview (see preview.c and build.sh).
// Only the drawing is real; the services the preview has no use for do nothing.
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdarg.h>

// The bits of the C library main.c uses (preview.c has them).
void *memset(void *, int, size_t); void *memcpy(void *, const void *, size_t);
int strcmp(const char *, const char *); size_t strlen(const char *); char *strncpy(char *, const char *, size_t);
int atoi(const char *); int abs(int);
void *malloc(size_t); void free(void *);
int snprintf(char *, size_t, const char *, ...);
typedef long long time_t;
struct tm { int tm_sec, tm_min, tm_hour, tm_mday, tm_mon, tm_year, tm_wday, tm_yday, tm_isdst; };
time_t time(time_t *); struct tm *localtime(const time_t *);

#define PBL_DISPLAY_HEIGHT 228
#define PBL_DISPLAY_WIDTH 200
#define PBL_HEALTH 1
#define PBL_RGB_BACKLIGHT 1
#define ARRAY_LENGTH(a) (sizeof(a)/sizeof(a[0]))
typedef union { uint8_t argb; struct { uint8_t b:2, g:2, r:2, a:2; }; } GColor;
#define GColorBlackARGB8 0xC0
#define GColorWhiteARGB8 0xFF
#define GColorLightGrayARGB8 0xEA
#define GColorDarkGrayARGB8 0xD5
#define GColorBlack ((GColor){.argb=0xC0})
#define GColorWhite ((GColor){.argb=0xFF})
#define GColorLightGray ((GColor){.argb=0xEA})
#define GColorDarkGray ((GColor){.argb=0xD5})
GColor GColorFromHEX(uint32_t);
typedef struct { int16_t x, y; } GPoint; typedef struct { int16_t w, h; } GSize;
typedef struct { GPoint origin; GSize size; } GRect;
#define GRect(x,y,w,h) ((GRect){{(x),(y)},{(w),(h)}})
typedef struct GBitmap GBitmap; typedef struct GContext GContext; typedef struct Window Window; typedef struct Layer Layer;
typedef struct { uint8_t *data; int16_t min_x, max_x; } GBitmapDataRowInfo;
GBitmapDataRowInfo gbitmap_get_data_row_info(const GBitmap*, uint16_t);
typedef enum { GTextAlignmentLeft, GTextAlignmentCenter, GTextAlignmentRight } GTextAlignment;
typedef enum { GTextOverflowModeTrailingEllipsis } GTextOverflowMode;
typedef void *GFont; GFont fonts_get_system_font(const char*);
#define FONT_KEY_GOTHIC_18_BOLD "a"
#define FONT_KEY_GOTHIC_24_BOLD "b"
void graphics_context_set_text_color(GContext*, GColor); void graphics_context_set_fill_color(GContext*, GColor);
void graphics_context_set_stroke_color(GContext*, GColor);
void graphics_draw_text(GContext*, const char*, GFont, GRect, GTextOverflowMode, GTextAlignment, void*);
GSize graphics_text_layout_get_content_size(const char*, GFont, GRect, GTextOverflowMode, GTextAlignment);
typedef enum { GCornerNone } GCornerMask;
void graphics_fill_rect(GContext*, GRect, uint16_t, GCornerMask); void graphics_draw_round_rect(GContext*, GRect, uint16_t);
GBitmap *graphics_capture_frame_buffer(GContext*); bool graphics_release_frame_buffer(GContext*, GBitmap*);
typedef enum { HealthMetricHeartRateBPM, HealthMetricStepCount, HealthMetricWalkedDistanceMeters } HealthMetric;
typedef int32_t HealthValue; typedef enum { HealthServiceAccessibilityMaskAvailable = 1 } HealthServiceAccessibilityMask;
HealthServiceAccessibilityMask health_service_metric_accessible(HealthMetric, time_t, time_t);
HealthValue health_service_peek_current_value(HealthMetric); HealthValue health_service_sum_today(HealthMetric);
time_t time_start_of_today(void);
typedef enum { MeasurementSystemImperial = 2 } MeasurementSystem;
MeasurementSystem health_service_get_measurement_system_for_display(HealthMetric);
typedef enum { HealthEventHeartRateUpdate } HealthEventType;
typedef void (*HealthEventHandler)(HealthEventType, void*);
bool health_service_events_subscribe(HealthEventHandler, void*); bool health_service_events_unsubscribe(void);
void light_set_system_color(void); void light_set_color_rgb888(uint32_t);
bool clock_is_24h_style(void); bool quiet_time_is_active(void); bool speaker_is_muted(void);
typedef struct DictionaryIterator DictionaryIterator;
typedef enum { APP_MSG_OK } AppMessageResult;
AppMessageResult app_message_outbox_begin(DictionaryIterator**); AppMessageResult app_message_outbox_send(void);
int dict_write_uint8(DictionaryIterator*, uint32_t, uint8_t);
typedef enum { TUPLE_BYTE_ARRAY, TUPLE_CSTRING, TUPLE_UINT, TUPLE_INT } TupleType;
typedef struct { uint32_t key; TupleType type; uint16_t length; union { char cstring[1]; int32_t int32; } value[]; } Tuple;
Tuple *dict_find(const DictionaryIterator*, uint32_t);
typedef enum { SECOND_UNIT = 1, MINUTE_UNIT = 2, HOUR_UNIT = 4 } TimeUnits;
typedef void (*TickHandler)(struct tm*, TimeUnits);
void tick_timer_service_subscribe(TimeUnits, TickHandler); void tick_timer_service_unsubscribe(void);
typedef int AccelAxisType; typedef void (*AccelTapHandler)(AccelAxisType, int32_t);
void accel_tap_service_subscribe(AccelTapHandler); void accel_tap_service_unsubscribe(void);
void layer_mark_dirty(Layer*);
typedef struct { uint8_t charge_percent; bool is_charging, is_plugged; } BatteryChargeState;
BatteryChargeState battery_state_service_peek(void); void battery_state_service_subscribe(void (*)(BatteryChargeState)); void battery_state_service_unsubscribe(void);
void vibes_short_pulse(void); void vibes_long_pulse(void); void vibes_double_pulse(void);
typedef struct { const uint32_t *durations; uint32_t num_segments; } VibePattern; void vibes_enqueue_custom_pattern(VibePattern);
typedef enum { SpeakerWaveformSquare, SpeakerWaveformSine } SpeakerWaveform;
typedef struct { uint8_t midi_note; SpeakerWaveform waveform; uint16_t duration_ms; uint8_t a, b; } SpeakerNote;
bool speaker_play_tone(uint32_t, uint32_t, uint8_t, SpeakerWaveform); bool speaker_play_notes(const SpeakerNote*, uint32_t, uint8_t);
void *app_timer_register(uint32_t, void (*)(void*), void*);
typedef void (*LayerUpdateProc)(Layer*, GContext*);
Layer *window_get_root_layer(Window*); Layer *layer_create(GRect); GRect layer_get_bounds(Layer*);
void layer_set_update_proc(Layer*, LayerUpdateProc); void layer_add_child(Layer*, Layer*); void layer_destroy(Layer*);
int persist_read_data(uint32_t, void*, size_t); int persist_write_data(uint32_t, const void*, size_t);
Window *window_create(void); void window_destroy(Window*); void window_stack_push(Window*, bool);
typedef struct { void (*load)(Window*); void (*unload)(Window*); } WindowHandlers; void window_set_window_handlers(Window*, WindowHandlers);
void app_focus_service_subscribe(void (*)(bool)); void app_focus_service_unsubscribe(void);
typedef struct { void (*pebble_app_connection_handler)(bool); } ConnectionHandlers;
void connection_service_subscribe(ConnectionHandlers); void connection_service_unsubscribe(void); bool connection_service_peek_pebble_app_connection(void);
typedef void (*AppMessageInboxReceived)(DictionaryIterator*, void*);
void app_message_register_inbox_received(AppMessageInboxReceived); void app_message_open(uint32_t, uint32_t);
uint32_t dict_calc_buffer_size(uint8_t, ...);
void app_event_loop(void);

typedef void *ResHandle;
ResHandle resource_get_handle(uint32_t);
size_t resource_size(ResHandle);
size_t resource_load(ResHandle, uint8_t *, size_t);
#define RESOURCE_ID_FONT_OXANIUM 1
#define RESOURCE_ID_FONT_CHAKRAPETCH 2
#define RESOURCE_ID_FONT_ORBITRON 3
void graphics_draw_rect(GContext*, GRect);

// The message keys (keys.h is made by build.sh from the names main.c uses).
#define K(name) extern uint32_t MESSAGE_KEY_##name;
#include "keys.h"
#undef K

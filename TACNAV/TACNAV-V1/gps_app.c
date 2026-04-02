/*
 * TacNav GPS — Tactical GPS display for Neo-6M on Flipper Zero
 * UART pins: 13 (RX from GPS TX), 14 (TX to GPS RX), 9 (3.3V), 11 (GND)
 */
#include "gps_app.h"
#include "gps_uart.h"
#include "minmea.h"

#include <furi.h>
#include <gui/gui.h>
#include <gui/view_port.h>
#include <input/input.h>
#include <notification/notification_messages.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ── App state ─────────────────────────────────────────────────────────── */
typedef struct {
    GpsData     gps;
    FuriMutex*  mutex;
    FuriMessageQueue* event_queue;
    ViewPort*   view_port;
    Gui*        gui;
    GpsUart*    uart;
    GpsScreen   screen;
    uint8_t     baud_idx;
    uint32_t    tick;         /* increments every draw, used for blink */
} TacNavApp;

/* ── Drawing helpers ────────────────────────────────────────────────────── */
#define W 128
#define H 64

/* Thin horizontal rule */
static void draw_hline(Canvas* c, uint8_t y) {
    canvas_draw_line(c, 0, y, W - 1, y);
}

/* Right-aligned string */
static void draw_str_right(Canvas* c, uint8_t x_right, uint8_t y, const char* s) {
    uint16_t sw = canvas_string_width(c, s);
    canvas_draw_str(c, x_right - sw, y, s);
}

/* Cardinal direction from degrees */
static const char* course_to_cardinal(float deg) {
    static const char* dirs[] = {
        "N","NNE","NE","ENE","E","ESE","SE","SSE",
        "S","SSW","SW","WSW","W","WNW","NW","NNW"
    };
    int idx = (int)((deg + 11.25f) / 22.5f) % 16;
    return dirs[idx];
}

static const char* hdop_label(float hdop) {
    if(hdop <= 1.0f) return "IDEAL";
    if(hdop <= 2.0f) return "EXLNT";
    if(hdop <= 5.0f) return "GOOD ";
    if(hdop <= 10.0f) return "MOD  ";
    return "POOR ";
}

/* ── Screen 0 — Main Tactical HUD ──────────────────────────────────────── */
/*
 *  ┌────────────────────────────────────────────────────────────────────┐
 *  │ TACNAV GPS            ●                                           │  row 0-9
 *  ├────────────────────────────────────────────────────────────────────┤
 *  │ LAT  37.3874°N                                                    │  row 11-20
 *  │ LON 121.9726°W                                                    │  row 21-30
 *  ├────────────────────────────────────────────────────────────────────┤
 *  │ ALT 0042m  SPD 00.0kt HDG 000° NNW                               │  row 32-41
 *  │ SAT 07    HDOP 1.2 GOOD                                           │  row 43-52
 *  ├────────────────────────────────────────────────────────────────────┤
 *  │ ◄ MOVE  [OK] CYCLE  SIG ►                                        │  row 54-63
 *  └────────────────────────────────────────────────────────────────────┘
 */
static void draw_screen_hud(Canvas* canvas, TacNavApp* app) {
    const GpsData* g = &app->gps;
    char buf[32];

    /* ── Header bar ── */
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 9, "TACNAV GPS");

    /* Fix indicator — blinks when no fix */
    bool blink_on = (app->tick % 4) < 2;
    if(g->valid || blink_on) {
        canvas_draw_disc(canvas, 120, 5, 3);
    }
    if(!g->valid) {
        /* hollow ring to show it's blinking "no fix" */
        canvas_draw_circle(canvas, 120, 5, 3);
    }

    draw_hline(canvas, 11);

    /* ── Coordinates ── */
    canvas_set_font(canvas, FontSecondary);

    /* Latitude */
    float lat = g->latitude;
    char lat_hemi = lat >= 0 ? 'N' : 'S';
    if(lat < 0) lat = -lat;
    snprintf(buf, sizeof(buf), "LAT %09.4f\xB0%c", (double)lat, lat_hemi);
    canvas_draw_str(canvas, 2, 21, buf);

    /* Longitude */
    float lon = g->longitude;
    char lon_hemi = lon >= 0 ? 'E' : 'W';
    if(lon < 0) lon = -lon;
    snprintf(buf, sizeof(buf), "LON %09.4f\xB0%c", (double)lon, lon_hemi);
    canvas_draw_str(canvas, 2, 31, buf);

    draw_hline(canvas, 33);

    /* ── Speed / Alt / Heading row ── */
    snprintf(buf, sizeof(buf), "ALT%4.0fm", (double)g->altitude_m);
    canvas_draw_str(canvas, 2, 42, buf);

    snprintf(buf, sizeof(buf), "SPD%4.1fkt", (double)g->speed_knots);
    canvas_draw_str(canvas, 55, 42, buf);

    snprintf(buf, sizeof(buf), "HDG%3.0f\xB0%s", (double)g->course_deg,
             course_to_cardinal(g->course_deg));
    canvas_draw_str(canvas, 2, 52, buf);

    snprintf(buf, sizeof(buf), "SAT%02d", g->satellites);
    canvas_draw_str(canvas, 86, 52, buf);

    draw_hline(canvas, 54);

    /* ── Nav hint ── */
    canvas_draw_str(canvas, 2, 63, "\x11MOVE");
    canvas_draw_str(canvas, 47, 63, "OK:NEXT");
    draw_str_right(canvas, W - 2, 63, "SIG\x10");
}

/* ── Screen 1 — Movement Data ──────────────────────────────────────────── */
static void draw_screen_move(Canvas* canvas, TacNavApp* app) {
    const GpsData* g = &app->gps;
    char buf[32];

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 9, "MOVEMENT DATA");
    draw_hline(canvas, 11);

    canvas_set_font(canvas, FontSecondary);

    float kph = g->speed_knots * 1.852f;
    float mph = g->speed_knots * 1.15078f;

    snprintf(buf, sizeof(buf), "SPD: %6.2f KTS", (double)g->speed_knots);
    canvas_draw_str(canvas, 2, 21, buf);
    snprintf(buf, sizeof(buf), "     %6.2f KM/H", (double)kph);
    canvas_draw_str(canvas, 2, 30, buf);
    snprintf(buf, sizeof(buf), "     %6.2f MPH", (double)mph);
    canvas_draw_str(canvas, 2, 39, buf);
    snprintf(buf, sizeof(buf), "HDG: %5.1f\xB0 (%s)",
             (double)g->course_deg, course_to_cardinal(g->course_deg));
    canvas_draw_str(canvas, 2, 48, buf);

    draw_hline(canvas, 54);
    canvas_draw_str(canvas, 2, 63, "\x11HUD");
    canvas_draw_str(canvas, 47, 63, "OK:NEXT");
    draw_str_right(canvas, W - 2, 63, "SIG\x10");
}

/* ── Screen 2 — Signal / Satellite ─────────────────────────────────────── */
static void draw_screen_signal(Canvas* canvas, TacNavApp* app) {
    const GpsData* g = &app->gps;
    char buf[32];

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 9, "SIGNAL STATUS");
    draw_hline(canvas, 11);

    canvas_set_font(canvas, FontSecondary);

    snprintf(buf, sizeof(buf), "SATS: %02d", g->satellites);
    canvas_draw_str(canvas, 2, 21, buf);

    snprintf(buf, sizeof(buf), "HDOP: %.1f (%s)", (double)g->hdop, hdop_label(g->hdop));
    canvas_draw_str(canvas, 2, 30, buf);

    snprintf(buf, sizeof(buf), "FIX:  %s",
             g->fix_quality == 0 ? "NONE" :
             g->fix_quality == 1 ? "GPS " : "DGPS");
    canvas_draw_str(canvas, 2, 39, buf);

    snprintf(buf, sizeof(buf), "ALT:  %6.1fm MSL", (double)g->altitude_m);
    canvas_draw_str(canvas, 2, 48, buf);

    /* Baud rate top-right in header */
    uint32_t baud = GPS_BAUD_RATES[app->baud_idx];
    snprintf(buf, sizeof(buf), "%lub", (unsigned long)baud);
    draw_str_right(canvas, W - 2, 9, buf);

    draw_hline(canvas, 54);
    canvas_draw_str(canvas, 2, 63, "\x11HUD");
    canvas_draw_str(canvas, 47, 63, "OK:NEXT");
    draw_str_right(canvas, W - 2, 63, "SIG\x10");
}

/* ── Master draw callback ───────────────────────────────────────────────── */
static void draw_callback(Canvas* canvas, void* ctx) {
    TacNavApp* app = (TacNavApp*)ctx;

    if(furi_mutex_acquire(app->mutex, 25) != FuriStatusOk) return;

    app->tick++;
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);

    /* Outer border — tactical feel */
    canvas_draw_frame(canvas, 0, 0, W, H);

    switch(app->screen) {
    case ScreenHud:    draw_screen_hud(canvas, app);    break;
    case ScreenMove:   draw_screen_move(canvas, app);   break;
    case ScreenSignal: draw_screen_signal(canvas, app); break;
    default: break;
    }

    furi_mutex_release(app->mutex);
}

/* ── Input callback ─────────────────────────────────────────────────────── */
static void input_callback(InputEvent* event, void* ctx) {
    TacNavApp* app = (TacNavApp*)ctx;
    furi_message_queue_put(app->event_queue, event, 0);
}

/* ── NMEA line callback (called from UART worker thread) ────────────────── */
static void nmea_line_callback(const char* line, void* ctx) {
    TacNavApp* app = (TacNavApp*)ctx;

    if(line[0] != '$') return;

    enum minmea_sentence_id id = minmea_sentence_id(line, false);

    if(furi_mutex_acquire(app->mutex, 10) != FuriStatusOk) return;

    if(id == MINMEA_SENTENCE_RMC) {
        struct minmea_sentence_rmc rmc;
        if(minmea_parse_rmc(&rmc, line)) {
            app->gps.valid       = rmc.valid;
            app->gps.latitude    = minmea_tocoord(&rmc.latitude);
            app->gps.longitude   = minmea_tocoord(&rmc.longitude);
            app->gps.speed_knots = minmea_tofloat(&rmc.speed);
            app->gps.course_deg  = minmea_tofloat(&rmc.course);
            app->gps.utc_h       = rmc.time.hours;
            app->gps.utc_m       = rmc.time.minutes;
            app->gps.utc_s       = rmc.time.seconds;
            app->gps.date_d      = rmc.date.day;
            app->gps.date_mo     = rmc.date.month;
            app->gps.date_yr     = rmc.date.year;
            if(app->gps.speed_knots > app->gps.max_speed_knots)
                app->gps.max_speed_knots = app->gps.speed_knots;
        }
    } else if(id == MINMEA_SENTENCE_GGA) {
        struct minmea_sentence_gga gga;
        if(minmea_parse_gga(&gga, line)) {
            app->gps.fix_quality  = gga.fix_quality;
            app->gps.satellites   = gga.satellites_tracked;
            app->gps.hdop         = minmea_tofloat(&gga.hdop);
            app->gps.altitude_m   = minmea_tofloat(&gga.altitude);
            if(app->gps.altitude_m > app->gps.max_altitude_m)
                app->gps.max_altitude_m = app->gps.altitude_m;
            /* Also update position from GGA when RMC not valid */
            if(!app->gps.valid && gga.fix_quality > 0) {
                app->gps.latitude  = minmea_tocoord(&gga.latitude);
                app->gps.longitude = minmea_tocoord(&gga.longitude);
            }
        }
    }

    furi_mutex_release(app->mutex);
    view_port_update(app->view_port);
}

/* ── App lifecycle ──────────────────────────────────────────────────────── */
int32_t gps_tacsys_app(void* p) {
    UNUSED(p);

    TacNavApp* app = malloc(sizeof(TacNavApp));
    memset(app, 0, sizeof(TacNavApp));

    app->mutex       = furi_mutex_alloc(FuriMutexTypeNormal);
    app->event_queue = furi_message_queue_alloc(8, sizeof(InputEvent));
    app->screen      = ScreenHud;
    app->baud_idx    = 1; /* default 9600 */
    app->tick        = 0;

    /* GUI setup */
    app->view_port = view_port_alloc();
    view_port_draw_callback_set(app->view_port, draw_callback, app);
    view_port_input_callback_set(app->view_port, input_callback, app);

    app->gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);

    /* UART / GPS */
    app->uart = gps_uart_alloc(nmea_line_callback, app);

    /* ── Main event loop ── */
    InputEvent event;
    bool running = true;

    while(running) {
        if(furi_message_queue_get(app->event_queue, &event, 100) == FuriStatusOk) {
            if(event.type == InputTypeShort || event.type == InputTypeRepeat) {
                switch(event.key) {
                case InputKeyBack:
                    running = false;
                    break;

                case InputKeyOk:
                    /* Cycle screens forward */
                    if(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk) {
                        app->screen = (GpsScreen)((app->screen + 1) % ScreenCount);
                        furi_mutex_release(app->mutex);
                    }
                    break;

                case InputKeyRight:
                    if(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk) {
                        app->screen = (GpsScreen)((app->screen + 1) % ScreenCount);
                        furi_mutex_release(app->mutex);
                    }
                    break;

                case InputKeyLeft:
                    if(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk) {
                        app->screen = (GpsScreen)((app->screen + ScreenCount - 1) % ScreenCount);
                        furi_mutex_release(app->mutex);
                    }
                    break;

                case InputKeyUp:
                    /* Increase baud rate */
                    if(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk) {
                        app->baud_idx = (app->baud_idx + 1) % GPS_BAUD_COUNT;
                        gps_uart_set_baud(app->uart, GPS_BAUD_RATES[app->baud_idx]);
                        furi_mutex_release(app->mutex);
                    }
                    break;

                case InputKeyDown:
                    /* Decrease baud rate */
                    if(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk) {
                        app->baud_idx = (app->baud_idx + GPS_BAUD_COUNT - 1) % GPS_BAUD_COUNT;
                        gps_uart_set_baud(app->uart, GPS_BAUD_RATES[app->baud_idx]);
                        furi_mutex_release(app->mutex);
                    }
                    break;

                default:
                    break;
                }
            }
            view_port_update(app->view_port);
        }
    }

    /* ── Teardown ── */
    gps_uart_free(app->uart);

    gui_remove_view_port(app->gui, app->view_port);
    furi_record_close(RECORD_GUI);
    view_port_free(app->view_port);

    furi_message_queue_free(app->event_queue);
    furi_mutex_free(app->mutex);
    free(app);

    return 0;
}

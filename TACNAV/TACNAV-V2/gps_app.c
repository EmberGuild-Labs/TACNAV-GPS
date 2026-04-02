/*
 * TacNav GPS v2 — Tactical GPS display for Neo-6M on Flipper Zero
 * New in v2: no-fix splash, trip odometer, satellite SNR bar chart
 *
 * Wiring: Pin 9 = 3.3V, Pin 11 = GND, Pin 13 = RX (GPS TX), Pin 14 = TX (GPS RX)
 */
#include "gps_app.h"
#include "gps_uart.h"
#include "minmea.h"

#include <furi.h>
#include <gui/gui.h>
#include <gui/view_port.h>
#include <input/input.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ── App state ─────────────────────────────────────────────────────────── */
typedef struct {
    GpsData           gps;
    FuriMutex*        mutex;
    FuriMessageQueue* event_queue;
    ViewPort*         view_port;
    Gui*              gui;
    GpsUart*          uart;
    GpsScreen         screen;
    uint8_t           baud_idx;
    uint32_t          tick;
} TacNavApp;

/* ── Constants ──────────────────────────────────────────────────────────── */
#define W 128
#define H 64

#define DEG_TO_RAD 0.017453292519943f
#define EARTH_R_KM 6371.0f

/* ── Helpers ────────────────────────────────────────────────────────────── */
static void draw_hline(Canvas* c, uint8_t y) {
    canvas_draw_line(c, 0, y, W - 1, y);
}

static void draw_str_right(Canvas* c, uint8_t x_right, uint8_t y, const char* s) {
    canvas_draw_str(c, x_right - (uint8_t)canvas_string_width(c, s), y, s);
}

static const char* course_to_cardinal(float deg) {
    static const char* dirs[] = {
        "N","NNE","NE","ENE","E","ESE","SE","SSE",
        "S","SSW","SW","WSW","W","WNW","NW","NNW"
    };
    return dirs[((int)((deg + 11.25f) / 22.5f)) % 16];
}

static const char* hdop_label(float hdop) {
    if(hdop <= 1.0f)  return "IDEAL";
    if(hdop <= 2.0f)  return "EXLNT";
    if(hdop <= 5.0f)  return "GOOD ";
    if(hdop <= 10.0f) return "MOD  ";
    return "POOR ";
}

static float haversine_km(float lat1, float lon1, float lat2, float lon2) {
    float dlat = (lat2 - lat1) * DEG_TO_RAD;
    float dlon = (lon2 - lon1) * DEG_TO_RAD;
    float a = sinf(dlat * 0.5f) * sinf(dlat * 0.5f) +
              cosf(lat1 * DEG_TO_RAD) * cosf(lat2 * DEG_TO_RAD) *
              sinf(dlon * 0.5f) * sinf(dlon * 0.5f);
    return EARTH_R_KM * 2.0f * atan2f(sqrtf(a), sqrtf(1.0f - a));
}

/* Bottom nav bar shared by all non-splash screens.
 * prev/next are short labels for the screens to the left/right. */
static void draw_nav(Canvas* canvas, const char* prev, const char* next) {
    draw_hline(canvas, 54);
    canvas_set_font(canvas, FontSecondary);
    char buf[12];
    snprintf(buf, sizeof(buf), "\x11%s", prev);
    canvas_draw_str(canvas, 2, 63, buf);
    canvas_draw_str(canvas, 47, 63, "OK:NEXT");
    snprintf(buf, sizeof(buf), "%s\x10", next);
    draw_str_right(canvas, W - 2, 63, buf);
}

/* ── Screen: No-Fix Splash ──────────────────────────────────────────────── */
/*
 *  Full-screen inverted (black bg, white elements) with animated dots
 *  and corner crosshair brackets.
 *
 *  ██████████████████████████████████████████████████
 *  █  ╔══════════════════════════════════════╗      █
 *  █  ║  + TACNAV GPS v2.0              +   ║      █
 *  █  ║                                     ║      █
 *  █  ║    ACQUIRING SIGNAL ...             ║      █
 *  █  ║    SATS IN VIEW: 03                 ║      █
 *  █  ╚══════════════════════════════════════╝      █
 *  ██████████████████████████████████████████████████
 */
static void draw_splash(Canvas* canvas, TacNavApp* app) {
    const GpsData* g = &app->gps;

    /* Inverted background */
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_box(canvas, 0, 0, W, H);

    canvas_set_color(canvas, ColorWhite);

    /* Double border */
    canvas_draw_frame(canvas, 1, 1, W - 2, H - 2);
    canvas_draw_frame(canvas, 3, 3, W - 6, H - 6);

    /* Corner crosshair brackets (inside inner border) */
    /* top-left */
    canvas_draw_line(canvas, 7, 6,  13, 6);
    canvas_draw_line(canvas, 7, 6,  7,  12);
    /* top-right */
    canvas_draw_line(canvas, W - 8, 6,  W - 14, 6);
    canvas_draw_line(canvas, W - 8, 6,  W - 8,  12);
    /* bottom-left */
    canvas_draw_line(canvas, 7, H - 7,  13, H - 7);
    canvas_draw_line(canvas, 7, H - 7,  7,  H - 13);
    /* bottom-right */
    canvas_draw_line(canvas, W - 8, H - 7,  W - 14, H - 7);
    canvas_draw_line(canvas, W - 8, H - 7,  W - 8,  H - 13);

    /* Title */
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, W / 2, 16, AlignCenter, AlignCenter, "TACNAV GPS v2.0");

    canvas_set_font(canvas, FontSecondary);

    /* Animated acquisition dots — cycles every ~4 ticks */
    static const char* const dot_frames[] = {
        "ACQUIRING SIGNAL   ",
        "ACQUIRING SIGNAL .  ",
        "ACQUIRING SIGNAL .. ",
        "ACQUIRING SIGNAL ...",
    };
    canvas_draw_str_aligned(
        canvas, W / 2, 31, AlignCenter, AlignCenter,
        dot_frames[(app->tick / 4) % 4]);

    /* Satellite count */
    char buf[24];
    snprintf(buf, sizeof(buf), "SATS IN VIEW: %02d", g->satellites);
    canvas_draw_str_aligned(canvas, W / 2, 44, AlignCenter, AlignCenter, buf);

    /* Restore default draw color for next screen */
    canvas_set_color(canvas, ColorBlack);
}

/* ── Screen 1: Main Tactical HUD ────────────────────────────────────────── */
static void draw_screen_hud(Canvas* canvas, TacNavApp* app) {
    const GpsData* g = &app->gps;
    char buf[32];

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 9, "TACNAV GPS");

    /* Fix dot — solid when valid, blinking hollow when searching */
    bool blink_on = (app->tick % 4) < 2;
    if(g->valid) {
        canvas_draw_disc(canvas, 120, 5, 3);
    } else if(blink_on) {
        canvas_draw_circle(canvas, 120, 5, 3);
    }

    draw_hline(canvas, 11);
    canvas_set_font(canvas, FontSecondary);

    float lat = g->latitude,  lon = g->longitude;
    char lat_h = lat >= 0 ? 'N' : 'S'; if(lat < 0) lat = -lat;
    char lon_h = lon >= 0 ? 'E' : 'W'; if(lon < 0) lon = -lon;

    snprintf(buf, sizeof(buf), "LAT %09.4f\xB0%c", (double)lat, lat_h);
    canvas_draw_str(canvas, 2, 21, buf);
    snprintf(buf, sizeof(buf), "LON %09.4f\xB0%c", (double)lon, lon_h);
    canvas_draw_str(canvas, 2, 31, buf);

    draw_hline(canvas, 33);

    snprintf(buf, sizeof(buf), "ALT%4.0fm", (double)g->altitude_m);
    canvas_draw_str(canvas, 2, 42, buf);
    snprintf(buf, sizeof(buf), "SPD%4.1fkt", (double)g->speed_knots);
    canvas_draw_str(canvas, 55, 42, buf);
    snprintf(buf, sizeof(buf), "HDG%3.0f\xB0%s", (double)g->course_deg,
             course_to_cardinal(g->course_deg));
    canvas_draw_str(canvas, 2, 52, buf);
    snprintf(buf, sizeof(buf), "SAT%02d", g->satellites);
    canvas_draw_str(canvas, 86, 52, buf);

    draw_nav(canvas, "SATS", "MOVE");
}

/* ── Screen 2: Movement + Trip Odometer ─────────────────────────────────── */
static void draw_screen_move(Canvas* canvas, TacNavApp* app) {
    const GpsData* g = &app->gps;
    char buf[32];

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 9, "MOVEMENT DATA");
    draw_hline(canvas, 11);
    canvas_set_font(canvas, FontSecondary);

    snprintf(buf, sizeof(buf), "SPD: %6.2f KTS", (double)g->speed_knots);
    canvas_draw_str(canvas, 2, 21, buf);

    snprintf(buf, sizeof(buf), "     %6.2f KM/H", (double)(g->speed_knots * 1.852f));
    canvas_draw_str(canvas, 2, 30, buf);

    snprintf(buf, sizeof(buf), "HDG: %5.1f\xB0 (%s)",
             (double)g->course_deg, course_to_cardinal(g->course_deg));
    canvas_draw_str(canvas, 2, 39, buf);

    /* Trip odometer — show in m below 1 km, km above */
    if(g->trip_distance_km < 1.0f) {
        snprintf(buf, sizeof(buf), "TRP: %5.0fm",
                 (double)(g->trip_distance_km * 1000.0f));
    } else {
        snprintf(buf, sizeof(buf), "TRP: %5.3fkm", (double)g->trip_distance_km);
    }
    canvas_draw_str(canvas, 2, 48, buf);

    /* Long-press OK hint in footer area */
    draw_hline(canvas, 54);
    canvas_set_font(canvas, FontSecondary);
    char left_buf[10], right_buf[10];
    snprintf(left_buf, sizeof(left_buf), "\x11HUD");
    snprintf(right_buf, sizeof(right_buf), "SIG\x10");
    canvas_draw_str(canvas, 2, 63, left_buf);
    canvas_draw_str(canvas, 38, 63, "OK:NEXT");
    canvas_draw_str(canvas, 90, 63, "RST\x7f");
    draw_str_right(canvas, W - 2, 63, right_buf);
}

/* ── Screen 3: Signal Quality ───────────────────────────────────────────── */
static void draw_screen_signal(Canvas* canvas, TacNavApp* app) {
    const GpsData* g = &app->gps;
    char buf[32];

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 9, "SIGNAL STATUS");

    /* Baud rate top-right */
    canvas_set_font(canvas, FontSecondary);
    snprintf(buf, sizeof(buf), "%lub", (unsigned long)GPS_BAUD_RATES[app->baud_idx]);
    draw_str_right(canvas, W - 2, 9, buf);

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

    draw_nav(canvas, "MOVE", "SATS");
}

/* ── Screen 4: Satellite SNR Bar Chart ──────────────────────────────────── */
/*
 *  Each bar represents one satellite. Height maps SNR (0-50 dBHz).
 *  Bars are sorted strongest-first. Tracked sats (SNR>0) draw solid,
 *  untracked draw a single-pixel stub.
 *
 *  Header: "SAT SIGNAL"    count top-right
 *  Bar area: y=13..50 (38px tall), baseline at y=50
 *  Bar width: 7px, gap: 2px = 9px/bar → up to 13 bars in 117px
 *  SNR scale: 50 dBHz → 38px
 */
#define BAR_W        7
#define BAR_GAP      2
#define BAR_STEP     (BAR_W + BAR_GAP)
#define BAR_BASELINE 50
#define BAR_MAX_H    36   /* px for SNR=50 */
#define BAR_SNR_MAX  50
#define MAX_BARS     12

static void draw_screen_sats(Canvas* canvas, TacNavApp* app) {
    const GpsData* g = &app->gps;
    char buf[24];

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 9, "SAT SIGNAL");

    canvas_set_font(canvas, FontSecondary);
    snprintf(buf, sizeof(buf), "%d SAT", g->sat_count_gsv);
    draw_str_right(canvas, W - 2, 9, buf);

    draw_hline(canvas, 11);

    /* Baseline */
    canvas_draw_line(canvas, 0, BAR_BASELINE, W - 1, BAR_BASELINE);

    int n = g->sat_count_gsv;
    if(n > MAX_BARS) n = MAX_BARS;

    /* Sort a local copy descending by SNR (simple insertion sort — small n) */
    int prn[MAX_BARS], snr[MAX_BARS];
    for(int i = 0; i < n; i++) { prn[i] = g->sat_prn[i]; snr[i] = g->sat_snr[i]; }
    for(int i = 1; i < n; i++) {
        int kp = prn[i], ks = snr[i], j = i - 1;
        while(j >= 0 && snr[j] < ks) {
            prn[j + 1] = prn[j]; snr[j + 1] = snr[j]; j--;
        }
        prn[j + 1] = kp; snr[j + 1] = ks;
    }

    /* Center the bar group horizontally */
    int total_w = n * BAR_STEP - BAR_GAP;
    int start_x = (W - total_w) / 2;

    for(int i = 0; i < n; i++) {
        int x = start_x + i * BAR_STEP;
        int s = snr[i];

        if(s > 0) {
            /* Filled bar scaled to SNR */
            int bar_h = (s * BAR_MAX_H) / BAR_SNR_MAX;
            if(bar_h < 1) bar_h = 1;
            canvas_draw_box(canvas, x, BAR_BASELINE - bar_h, BAR_W, bar_h);

            /* SNR label above taller bars (saves clutter on short ones) */
            if(bar_h >= 10) {
                snprintf(buf, sizeof(buf), "%d", s);
                /* tiny centered label above bar */
                int lx = x + BAR_W / 2 - (int)canvas_string_width(canvas, buf) / 2;
                canvas_draw_str(canvas, lx, BAR_BASELINE - bar_h - 1, buf);
            }
        } else {
            /* Stub for satellites in view but not tracking */
            canvas_draw_box(canvas, x, BAR_BASELINE - 2, BAR_W, 2);
        }
    }

    /* "NO DATA" placeholder when GSV hasn't arrived yet */
    if(n == 0) {
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str_aligned(canvas, W / 2, 32, AlignCenter, AlignCenter, "NO DATA");
    }

    draw_nav(canvas, "SIG", "HUD");
}

/* ── Master draw callback ───────────────────────────────────────────────── */
static void draw_callback(Canvas* canvas, void* ctx) {
    TacNavApp* app = (TacNavApp*)ctx;
    if(furi_mutex_acquire(app->mutex, 25) != FuriStatusOk) return;

    app->tick++;
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_frame(canvas, 0, 0, W, H);

    /* Auto-switch to splash when no fix and user is on HUD */
    GpsScreen draw_screen = app->screen;
    if(!app->gps.valid && draw_screen == ScreenHud) draw_screen = ScreenSplash;

    switch(draw_screen) {
    case ScreenSplash: draw_splash(canvas, app);       break;
    case ScreenHud:    draw_screen_hud(canvas, app);   break;
    case ScreenMove:   draw_screen_move(canvas, app);  break;
    case ScreenSignal: draw_screen_signal(canvas, app); break;
    case ScreenSats:   draw_screen_sats(canvas, app);  break;
    default: break;
    }

    furi_mutex_release(app->mutex);
}

/* ── Input callback ─────────────────────────────────────────────────────── */
static void input_callback(InputEvent* event, void* ctx) {
    TacNavApp* app = (TacNavApp*)ctx;
    furi_message_queue_put(app->event_queue, event, 0);
}

/* ── NMEA line callback (UART worker thread) ────────────────────────────── */
static void nmea_line_callback(const char* line, void* ctx) {
    TacNavApp* app = (TacNavApp*)ctx;
    if(line[0] != '$') return;

    enum minmea_sentence_id id = minmea_sentence_id(line, false);
    if(furi_mutex_acquire(app->mutex, 10) != FuriStatusOk) return;

    GpsData* g = &app->gps;

    if(id == MINMEA_SENTENCE_RMC) {
        struct minmea_sentence_rmc rmc;
        if(minmea_parse_rmc(&rmc, line)) {
            bool was_valid = g->valid;
            g->valid       = rmc.valid;
            g->latitude    = minmea_tocoord(&rmc.latitude);
            g->longitude   = minmea_tocoord(&rmc.longitude);
            g->speed_knots = minmea_tofloat(&rmc.speed);
            g->course_deg  = minmea_tofloat(&rmc.course);
            g->utc_h = rmc.time.hours;
            g->utc_m = rmc.time.minutes;
            g->utc_s = rmc.time.seconds;
            g->date_d  = rmc.date.day;
            g->date_mo = rmc.date.month;
            g->date_yr = rmc.date.year;

            if(g->speed_knots > g->max_speed_knots)
                g->max_speed_knots = g->speed_knots;

            /* Trip odometer — only accumulate with a valid fix and movement */
            if(g->valid && g->speed_knots > 0.1f) {
                if(was_valid && g->odo_has_last) {
                    float dist = haversine_km(
                        g->odo_last_lat, g->odo_last_lon,
                        g->latitude, g->longitude);
                    if(dist > 0.002f) { /* ignore jumps < 2m (GPS noise) */
                        g->trip_distance_km += dist;
                    }
                }
                g->odo_last_lat = g->latitude;
                g->odo_last_lon = g->longitude;
                g->odo_has_last = true;
            }
        }

    } else if(id == MINMEA_SENTENCE_GGA) {
        struct minmea_sentence_gga gga;
        if(minmea_parse_gga(&gga, line)) {
            g->fix_quality       = gga.fix_quality;
            g->satellites        = gga.satellites_tracked;
            g->hdop              = minmea_tofloat(&gga.hdop);
            g->altitude_m        = minmea_tofloat(&gga.altitude);
            if(g->altitude_m > g->max_altitude_m)
                g->max_altitude_m = g->altitude_m;
            if(!g->valid && gga.fix_quality > 0) {
                g->latitude  = minmea_tocoord(&gga.latitude);
                g->longitude = minmea_tocoord(&gga.longitude);
            }
        }

    } else if(id == MINMEA_SENTENCE_GSV) {
        struct minmea_sentence_gsv gsv;
        if(minmea_parse_gsv(&gsv, line)) {
            /* msg_num == 1 → start of a new GSV sequence, reset temp buffer */
            if(gsv.msg_num == 1) {
                g->gsv_tmp_count  = 0;
                g->gsv_total_msgs = gsv.total_msgs;
            }
            /* Append up to 4 sats from this message */
            for(int i = 0; i < 4 && g->gsv_tmp_count < GPS_MAX_SATS; i++) {
                if(gsv.sats[i].prn == 0) break;
                g->gsv_prn_tmp[g->gsv_tmp_count] = gsv.sats[i].prn;
                g->gsv_snr_tmp[g->gsv_tmp_count] = gsv.sats[i].snr;
                g->gsv_tmp_count++;
            }
            /* Last message in sequence → commit to live arrays */
            if(gsv.msg_num == gsv.total_msgs) {
                g->sat_count_gsv = g->gsv_tmp_count;
                for(int i = 0; i < g->gsv_tmp_count; i++) {
                    g->sat_prn[i] = g->gsv_prn_tmp[i];
                    g->sat_snr[i] = g->gsv_snr_tmp[i];
                }
            }
        }
    }

    furi_mutex_release(app->mutex);
    view_port_update(app->view_port);
}

/* ── App entry point ────────────────────────────────────────────────────── */
int32_t gps_tacsys_app(void* p) {
    UNUSED(p);

    TacNavApp* app = malloc(sizeof(TacNavApp));
    memset(app, 0, sizeof(TacNavApp));

    app->mutex       = furi_mutex_alloc(FuriMutexTypeNormal);
    app->event_queue = furi_message_queue_alloc(8, sizeof(InputEvent));
    app->screen      = ScreenHud; /* splash auto-shows if no fix */
    app->baud_idx    = 1;         /* 9600 default */

    app->view_port = view_port_alloc();
    view_port_draw_callback_set(app->view_port, draw_callback, app);
    view_port_input_callback_set(app->view_port, input_callback, app);

    app->gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);

    app->uart = gps_uart_alloc(nmea_line_callback, app);

    /* ── Main event loop ── */
    InputEvent event;
    bool running = true;

    while(running) {
        if(furi_message_queue_get(app->event_queue, &event, 100) == FuriStatusOk) {

            /* Screen navigation: OK and RIGHT go forward, LEFT goes back */
            if(event.type == InputTypeShort) {
                switch(event.key) {
                case InputKeyBack: running = false; break;

                case InputKeyOk:
                case InputKeyRight:
                    if(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk) {
                        /* Skip ScreenSplash — it's auto, not manually navigable */
                        app->screen = (GpsScreen)(
                            (app->screen % (ScreenCount - 1)) + 1);
                        furi_mutex_release(app->mutex);
                    }
                    break;

                case InputKeyLeft:
                    if(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk) {
                        int s = (int)app->screen - 1;
                        if(s < ScreenHud) s = ScreenCount - 1;
                        app->screen = (GpsScreen)s;
                        furi_mutex_release(app->mutex);
                    }
                    break;

                case InputKeyUp:
                    if(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk) {
                        app->baud_idx = (app->baud_idx + 1) % GPS_BAUD_COUNT;
                        gps_uart_set_baud(app->uart, GPS_BAUD_RATES[app->baud_idx]);
                        furi_mutex_release(app->mutex);
                    }
                    break;

                case InputKeyDown:
                    if(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk) {
                        app->baud_idx =
                            (app->baud_idx + GPS_BAUD_COUNT - 1) % GPS_BAUD_COUNT;
                        gps_uart_set_baud(app->uart, GPS_BAUD_RATES[app->baud_idx]);
                        furi_mutex_release(app->mutex);
                    }
                    break;

                default: break;
                }
            }

            /* Long-press OK on MOVE screen — reset trip odometer */
            if(event.type == InputTypeLong && event.key == InputKeyOk) {
                if(app->screen == ScreenMove) {
                    if(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk) {
                        app->gps.trip_distance_km = 0.0f;
                        app->gps.odo_has_last     = false;
                        furi_mutex_release(app->mutex);
                    }
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

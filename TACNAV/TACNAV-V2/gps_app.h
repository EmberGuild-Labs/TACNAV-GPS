#pragma once
#include <stdint.h>
#include <stdbool.h>

#define GPS_BAUD_COUNT 6
static const uint32_t GPS_BAUD_RATES[GPS_BAUD_COUNT] = {
    4800, 9600, 19200, 38400, 57600, 115200
};

#define GPS_MAX_SATS 16 /* max satellites tracked in GSV data */

typedef enum {
    ScreenSplash = 0, /* acquiring-signal splash — auto-shown when no fix */
    ScreenHud    = 1, /* main tactical HUD */
    ScreenMove   = 2, /* movement + odometer */
    ScreenSignal = 3, /* signal quality */
    ScreenSats   = 4, /* satellite SNR bar chart */
    ScreenCount  = 5,
} GpsScreen;

typedef struct {
    /* Position */
    float latitude;
    float longitude;
    float altitude_m;
    bool  valid;

    /* Movement */
    float speed_knots;
    float course_deg;
    float max_speed_knots;

    /* Trip odometer */
    float trip_distance_km;
    float odo_last_lat;
    float odo_last_lon;
    bool  odo_has_last;

    /* Signal */
    int   fix_quality;
    int   satellites;
    float hdop;
    float max_altitude_m;

    /* UTC time */
    int utc_h, utc_m, utc_s;
    int date_d, date_mo, date_yr;

    /* Satellite SNR bars (assembled from GSV sentences) */
    int sat_prn[GPS_MAX_SATS];
    int sat_snr[GPS_MAX_SATS];
    int sat_count_gsv;

    /* GSV multi-message assembly buffers */
    int gsv_prn_tmp[GPS_MAX_SATS];
    int gsv_snr_tmp[GPS_MAX_SATS];
    int gsv_tmp_count;
    int gsv_total_msgs;
} GpsData;

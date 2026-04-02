#pragma once
#include <stdint.h>
#include <stdbool.h>

/* Max baud rates selectable via button */
#define GPS_BAUD_COUNT 6
static const uint32_t GPS_BAUD_RATES[GPS_BAUD_COUNT] = {
    4800, 9600, 19200, 38400, 57600, 115200
};

/* App screens */
typedef enum {
    ScreenHud    = 0, /* Main tactical HUD */
    ScreenMove   = 1, /* Movement / speed */
    ScreenSignal = 2, /* Signal / satellite info */
    ScreenCount  = 3,
} GpsScreen;

/* GPS data shared between UART worker and GUI */
typedef struct {
    float latitude;       /* decimal degrees, + = N */
    float longitude;      /* decimal degrees, + = E */
    float altitude_m;     /* meters MSL */
    float speed_knots;
    float course_deg;     /* true north, 0-360 */
    float hdop;
    int   satellites;
    int   fix_quality;    /* 0=none, 1=GPS, 2=DGPS */
    bool  valid;          /* RMC 'A' status */

    /* UTC time from last sentence */
    int   utc_h, utc_m, utc_s;
    int   date_d, date_mo, date_yr;

    /* running stats */
    float max_speed_knots;
    float max_altitude_m;
} GpsData;

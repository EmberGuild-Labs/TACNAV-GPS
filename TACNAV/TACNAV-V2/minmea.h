#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

struct minmea_float {
    int32_t value;
    int32_t scale;
};

struct minmea_date {
    int day;
    int month;
    int year;
};

struct minmea_time {
    int hours;
    int minutes;
    int seconds;
    int microseconds;
};

struct minmea_sentence_rmc {
    struct minmea_time time;
    bool valid;
    struct minmea_float latitude;
    struct minmea_float longitude;
    struct minmea_float speed;
    struct minmea_float course;
    struct minmea_date date;
    struct minmea_float variation;
    char variation_dir;
    char mode;
};

struct minmea_sentence_gga {
    struct minmea_time time;
    struct minmea_float latitude;
    struct minmea_float longitude;
    int fix_quality;
    int satellites_tracked;
    struct minmea_float hdop;
    struct minmea_float altitude;
    char altitude_units;
    struct minmea_float height;
    char height_units;
    int dgps_age;
};

struct minmea_sentence_gsa {
    char mode;
    int fix_type;
    int sats[12];
    struct minmea_float pdop;
    struct minmea_float hdop;
    struct minmea_float vdop;
};

/* Per-satellite info from GSV */
struct minmea_sat_info {
    int prn;        /* satellite PRN number */
    int elevation;  /* degrees */
    int azimuth;    /* degrees */
    int snr;        /* dB-Hz, 0 = not tracking */
};

struct minmea_sentence_gsv {
    int total_msgs;
    int msg_num;
    int total_sats;
    struct minmea_sat_info sats[4]; /* up to 4 per sentence */
};

enum minmea_sentence_id {
    MINMEA_INVALID = -1,
    MINMEA_UNKNOWN = 0,
    MINMEA_SENTENCE_RMC,
    MINMEA_SENTENCE_GGA,
    MINMEA_SENTENCE_GSA,
    MINMEA_SENTENCE_GSV,
};

enum minmea_sentence_id minmea_sentence_id(const char* sentence, bool strict);

bool minmea_parse_rmc(struct minmea_sentence_rmc* frame, const char* sentence);
bool minmea_parse_gga(struct minmea_sentence_gga* frame, const char* sentence);
bool minmea_parse_gsa(struct minmea_sentence_gsa* frame, const char* sentence);
bool minmea_parse_gsv(struct minmea_sentence_gsv* frame, const char* sentence);

float minmea_tocoord(const struct minmea_float* f);
float minmea_tofloat(const struct minmea_float* f);

#ifdef __cplusplus
}
#endif

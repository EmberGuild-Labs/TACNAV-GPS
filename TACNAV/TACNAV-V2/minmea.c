#include "minmea.h"
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

static int hex2int(char c) {
    if(c >= '0' && c <= '9') return c - '0';
    if(c >= 'A' && c <= 'F') return c - 'A' + 10;
    if(c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static bool minmea_check(const char* sentence, bool strict) {
    uint8_t checksum = 0x00;
    const char* p = sentence;
    if(*p++ != '$') return false;
    while(*p && *p != '*' && *p != '\r' && *p != '\n')
        checksum ^= (uint8_t)*p++;
    if(strict) {
        if(*p++ != '*') return false;
        int upper = hex2int(*p++);
        int lower = hex2int(*p++);
        if(upper == -1 || lower == -1) return false;
        if(checksum != (uint8_t)((upper << 4) | lower)) return false;
    }
    return true;
}

static bool minmea_scan(const char* sentence, const char* format, ...) {
    bool result = false;
    va_list ap;
    va_start(ap, format);

    const char* p = sentence;
    if(*p++ != '$') goto end;
    while(*p && *p != ',') p++;
    if(*p++ != ',') goto end;

    while(*format) {
        char type = *format++;
        if(type == ';') break;

        const char* field = p;
        while(*p && *p != ',' && *p != '*' && *p != '\r' && *p != '\n') p++;
        size_t field_len = (size_t)(p - field);
        if(*p == ',') p++;

        switch(type) {
        case 'c': {
            char* c = va_arg(ap, char*);
            *c = field_len == 1 ? field[0] : '\0';
        } break;
        case 'd': {
            int* i = va_arg(ap, int*);
            if(field_len > 0) {
                char tmp[16];
                size_t len = field_len < 15 ? field_len : 15;
                memcpy(tmp, field, len);
                tmp[len] = '\0';
                *i = atoi(tmp);
            } else {
                *i = 0;
            }
        } break;
        case 'f': {
            struct minmea_float* f = va_arg(ap, struct minmea_float*);
            f->value = 0; f->scale = 0;
            if(field_len > 0) {
                const char* fp = field;
                bool neg = false;
                if(*fp == '-') { neg = true; fp++; } else if(*fp == '+') { fp++; }
                int32_t integer_part = 0;
                while(fp < field + field_len && *fp != '.') {
                    integer_part = integer_part * 10 + (*fp - '0');
                    fp++;
                }
                f->scale = 1;
                int32_t frac_part = 0;
                if(fp < field + field_len && *fp == '.') {
                    fp++;
                    while(fp < field + field_len) {
                        frac_part = frac_part * 10 + (*fp - '0');
                        f->scale *= 10;
                        fp++;
                    }
                }
                f->value = integer_part * f->scale + frac_part;
                if(neg) f->value = -f->value;
            }
        } break;
        case 't': {
            struct minmea_time* t = va_arg(ap, struct minmea_time*);
            t->hours = t->minutes = t->seconds = t->microseconds = 0;
            if(field_len >= 6) {
                char tmp[3] = {0};
                tmp[0] = field[0]; tmp[1] = field[1]; t->hours   = atoi(tmp);
                tmp[0] = field[2]; tmp[1] = field[3]; t->minutes = atoi(tmp);
                tmp[0] = field[4]; tmp[1] = field[5]; t->seconds = atoi(tmp);
                if(field_len > 7 && field[6] == '.') {
                    int32_t us = 0, scale = 1000000;
                    for(size_t i = 7; i < field_len && scale > 1; i++) {
                        scale /= 10;
                        us += (field[i] - '0') * scale;
                    }
                    t->microseconds = us;
                }
            }
        } break;
        case 'D': {
            struct minmea_date* d = va_arg(ap, struct minmea_date*);
            d->day = d->month = d->year = 0;
            if(field_len >= 6) {
                char tmp[3] = {0};
                tmp[0] = field[0]; tmp[1] = field[1]; d->day   = atoi(tmp);
                tmp[0] = field[2]; tmp[1] = field[3]; d->month = atoi(tmp);
                tmp[0] = field[4]; tmp[1] = field[5]; d->year  = atoi(tmp);
            }
        } break;
        case '_': break; /* skip */
        default: goto end;
        }
    }
    result = true;
end:
    va_end(ap);
    return result;
}

enum minmea_sentence_id minmea_sentence_id(const char* sentence, bool strict) {
    if(!minmea_check(sentence, strict)) return MINMEA_INVALID;
    const char* id = sentence + 3; /* skip $GP / $GN / $GL */
    if(strncmp(id, "RMC", 3) == 0) return MINMEA_SENTENCE_RMC;
    if(strncmp(id, "GGA", 3) == 0) return MINMEA_SENTENCE_GGA;
    if(strncmp(id, "GSA", 3) == 0) return MINMEA_SENTENCE_GSA;
    if(strncmp(id, "GSV", 3) == 0) return MINMEA_SENTENCE_GSV;
    return MINMEA_UNKNOWN;
}

bool minmea_parse_rmc(struct minmea_sentence_rmc* frame, const char* sentence) {
    char status, lat_dir, lon_dir;
    bool ok = minmea_scan(sentence, "tcfcfcffDfc",
        &frame->time, &status,
        &frame->latitude, &lat_dir,
        &frame->longitude, &lon_dir,
        &frame->speed, &frame->course,
        &frame->date, &frame->variation, &frame->variation_dir);
    if(!ok) return false;
    frame->valid = (status == 'A');
    if(lat_dir == 'S') frame->latitude.value  = -frame->latitude.value;
    if(lon_dir == 'W') frame->longitude.value = -frame->longitude.value;
    return true;
}

bool minmea_parse_gga(struct minmea_sentence_gga* frame, const char* sentence) {
    char lat_dir, lon_dir;
    bool ok = minmea_scan(sentence, "tfcfcddfcfc__",
        &frame->time,
        &frame->latitude,  &lat_dir,
        &frame->longitude, &lon_dir,
        &frame->fix_quality, &frame->satellites_tracked,
        &frame->hdop, &frame->altitude, &frame->altitude_units,
        &frame->height, &frame->height_units);
    if(!ok) return false;
    if(lat_dir == 'S') frame->latitude.value  = -frame->latitude.value;
    if(lon_dir == 'W') frame->longitude.value = -frame->longitude.value;
    return true;
}

bool minmea_parse_gsa(struct minmea_sentence_gsa* frame, const char* sentence) {
    return minmea_scan(sentence, "cddddddddddddddfff",
        &frame->mode, &frame->fix_type,
        &frame->sats[0],  &frame->sats[1],  &frame->sats[2],  &frame->sats[3],
        &frame->sats[4],  &frame->sats[5],  &frame->sats[6],  &frame->sats[7],
        &frame->sats[8],  &frame->sats[9],  &frame->sats[10], &frame->sats[11],
        &frame->pdop, &frame->hdop, &frame->vdop);
}

/* GSV uses a hand-rolled parser to handle variable satellite record count */
static const char* gsv_next_field(const char* p) {
    while(*p && *p != ',' && *p != '*') p++;
    return (*p == ',') ? p + 1 : p;
}

static int gsv_field_int(const char* p) {
    if(!p || *p == ',' || *p == '*' || *p == '\0') return 0;
    return atoi(p);
}

bool minmea_parse_gsv(struct minmea_sentence_gsv* frame, const char* sentence) {
    memset(frame, 0, sizeof(*frame));
    if(!minmea_check(sentence, false)) return false;

    /* skip $GP...GSV, */
    const char* p = sentence + 1;
    while(*p && *p != ',') p++;
    if(*p++ != ',') return false;

    frame->total_msgs = gsv_field_int(p); p = gsv_next_field(p);
    frame->msg_num    = gsv_field_int(p); p = gsv_next_field(p);
    frame->total_sats = gsv_field_int(p); p = gsv_next_field(p);

    for(int i = 0; i < 4; i++) {
        if(!*p || *p == '*') break;
        frame->sats[i].prn       = gsv_field_int(p); p = gsv_next_field(p);
        if(!*p || *p == '*') break;
        frame->sats[i].elevation = gsv_field_int(p); p = gsv_next_field(p);
        if(!*p || *p == '*') break;
        frame->sats[i].azimuth   = gsv_field_int(p); p = gsv_next_field(p);
        if(!*p || *p == '*') break;
        frame->sats[i].snr       = gsv_field_int(p); p = gsv_next_field(p);
    }
    return true;
}

float minmea_tofloat(const struct minmea_float* f) {
    if(f->scale == 0) return 0.0f;
    return (float)f->value / (float)f->scale;
}

float minmea_tocoord(const struct minmea_float* f) {
    if(f->scale == 0) return 0.0f;
    float val = (float)f->value / (float)f->scale;
    bool neg = val < 0;
    if(neg) val = -val;
    int degrees = (int)(val / 100);
    float minutes = val - (float)(degrees * 100);
    float coord = (float)degrees + minutes / 60.0f;
    return neg ? -coord : coord;
}

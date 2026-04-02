#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <furi.h>

#define GPS_UART_BAUD_DEFAULT 9600
#define GPS_NMEA_LINE_MAX     96

typedef void (*GpsUartLineCallback)(const char* line, void* context);

typedef struct GpsUart GpsUart;

GpsUart* gps_uart_alloc(GpsUartLineCallback callback, void* context);
void gps_uart_free(GpsUart* uart);
void gps_uart_set_baud(GpsUart* uart, uint32_t baud);
uint32_t gps_uart_get_baud(GpsUart* uart);

#include "gps_uart.h"
#include <furi_hal_serial.h>
#include <furi_hal_serial_control.h>
#include <string.h>

#define WORKER_EVENTS_MASK (WorkerEventStop | WorkerEventRxData)

typedef enum {
    WorkerEventStop  = (1 << 0),
    WorkerEventRxData = (1 << 1),
} WorkerEvent;

struct GpsUart {
    FuriHalSerialHandle* serial_handle;
    FuriThread* worker_thread;
    FuriStreamBuffer* rx_stream;
    GpsUartLineCallback line_callback;
    void* callback_context;
    uint32_t baud;
    char line_buf[GPS_NMEA_LINE_MAX];
    size_t line_pos;
};

/* ISR context — just push byte into stream and signal thread */
static void gps_uart_rx_callback(
    FuriHalSerialHandle* handle,
    FuriHalSerialRxEvent event,
    void* ctx) {
    GpsUart* uart = (GpsUart*)ctx;
    if(event == FuriHalSerialRxEventData) {
        uint8_t byte = furi_hal_serial_async_rx(handle);
        furi_stream_buffer_send(uart->rx_stream, &byte, 1, 0);
        furi_thread_flags_set(furi_thread_get_id(uart->worker_thread), WorkerEventRxData);
    }
}

static int32_t gps_uart_worker(void* ctx) {
    GpsUart* uart = (GpsUart*)ctx;

    while(true) {
        uint32_t events = furi_thread_flags_wait(
            WORKER_EVENTS_MASK, FuriFlagWaitAny, FuriWaitForever);

        if(events & WorkerEventStop) break;

        if(events & WorkerEventRxData) {
            uint8_t byte;
            while(furi_stream_buffer_receive(uart->rx_stream, &byte, 1, 0) == 1) {
                if(byte == '\n' || byte == '\r') {
                    if(uart->line_pos > 0) {
                        uart->line_buf[uart->line_pos] = '\0';
                        if(uart->line_callback) {
                            uart->line_callback(uart->line_buf, uart->callback_context);
                        }
                        uart->line_pos = 0;
                    }
                } else {
                    if(uart->line_pos < GPS_NMEA_LINE_MAX - 1) {
                        uart->line_buf[uart->line_pos++] = (char)byte;
                    } else {
                        // overflow — discard and reset
                        uart->line_pos = 0;
                    }
                }
            }
        }
    }

    return 0;
}

GpsUart* gps_uart_alloc(GpsUartLineCallback callback, void* context) {
    GpsUart* uart = malloc(sizeof(GpsUart));
    memset(uart, 0, sizeof(GpsUart));

    uart->line_callback    = callback;
    uart->callback_context = context;
    uart->baud             = GPS_UART_BAUD_DEFAULT;
    uart->line_pos         = 0;

    uart->rx_stream = furi_stream_buffer_alloc(256, 1);

    uart->worker_thread = furi_thread_alloc_ex("GpsUartWorker", 1024, gps_uart_worker, uart);
    furi_thread_start(uart->worker_thread);

    uart->serial_handle = furi_hal_serial_control_acquire(FuriHalSerialIdUsart);
    furi_hal_serial_init(uart->serial_handle, uart->baud);
    furi_hal_serial_async_rx_start(uart->serial_handle, gps_uart_rx_callback, uart, false);

    return uart;
}

void gps_uart_free(GpsUart* uart) {
    furi_hal_serial_async_rx_stop(uart->serial_handle);
    furi_hal_serial_deinit(uart->serial_handle);
    furi_hal_serial_control_release(uart->serial_handle);

    furi_thread_flags_set(furi_thread_get_id(uart->worker_thread), WorkerEventStop);
    furi_thread_join(uart->worker_thread);
    furi_thread_free(uart->worker_thread);

    furi_stream_buffer_free(uart->rx_stream);

    free(uart);
}

void gps_uart_set_baud(GpsUart* uart, uint32_t baud) {
    uart->baud = baud;
    furi_hal_serial_set_br(uart->serial_handle, baud);
}

uint32_t gps_uart_get_baud(GpsUart* uart) {
    return uart->baud;
}

#ifndef RPICO_BRIDGE_CDC_BENCH_H
#define RPICO_BRIDGE_CDC_BENCH_H

#include <stdbool.h>
#include <stdint.h>

#include "usb_bridge.h"

/* Read/write one raw cable-zero USB-MIDI packet on the CDC virtual port. */
bool cdc_bench_read_packet(usb_bridge_packet_t *packet);
bool cdc_bench_write_packet(const usb_bridge_packet_t *packet);

/* Reset parser state and optionally discard buffered CDC session data. */
void cdc_bench_reset_session(bool clear_cdc_buffers);

/* Preserve the exact BOOTSEL command across CDC disconnect, then reset in main. */
bool cdc_bench_bootsel_pending(void);
void cdc_bench_service_reset(uint32_t now_ms);
void cdc_bench_request_bootsel(void);

#endif

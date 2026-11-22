#include "cdc_bench.h"

#include <stddef.h>
#include <string.h>

#include "pico/bootrom.h"
#include "pico/stdlib.h"
#include "tusb.h"
#include "midi_router.h"
#include "route_control.h"

#define CDC_CONTROL_MAX_BYTES_PER_TASK 32u
#define CDC_BENCH_MAX_LINE 96u
#define CDC_BENCH_PACKET_LINE_SIZE 10u
#define CDC_BENCH_PACKET_TEXT_SIZE 11u

static char input_line[CDC_BENCH_MAX_LINE];
static uint8_t input_length;
static bool discard_line;
static bool discard_route_line;
static bool swallow_lf;
static bool bootsel_pending;
static uint32_t bootsel_deadline_ms;

static int hex_value(uint8_t character);

static void process_character(uint8_t character, usb_bridge_packet_t *packet,
                              bool *packet_ready)
{
    if (character == '\n' && swallow_lf) {
        swallow_lf = false;
        return;
    }
    if (character != '\r' && character != '\n') {
        swallow_lf = false;
        if (!discard_line) {
            if (input_length >= sizeof(input_line)) {
                discard_route_line = input_length >= 2u &&
                                     input_line[0] == 'R' && input_line[1] == ' ';
                input_length = 0;
                discard_line = true;
            } else {
                input_line[input_length++] = (char)character;
            }
        }
        return;
    }
    swallow_lf = character == '\r';

    const bool bootsel_line = !discard_line && (input_length == 7u) &&
                              (memcmp(input_line, "BOOTSEL", 7u) == 0);
    const bool route_line = (!discard_line && input_length >= 2u &&
                             input_line[0] == 'R' && input_line[1] == ' ') ||
                            discard_route_line;
    const bool midi_line = !discard_line && input_length >= 2u &&
                           input_line[0] == 'M' && input_line[1] == ' ';
    if (route_line) {
        static const char malformed[] = "R malformed";
        route_control_feed_line(discard_route_line ? malformed : input_line,
                                discard_route_line ? (uint8_t)(sizeof(malformed) - 1u)
                                                    : input_length);
    }
    if (bootsel_line) {
        cdc_bench_request_bootsel();
    }

    if (!bootsel_line && !route_line && midi_line && !discard_line &&
        (packet != NULL) &&
        (input_length == CDC_BENCH_PACKET_LINE_SIZE) &&
        (input_line[0] == 'M') && (input_line[1] == ' ')) {
        bool valid = true;
        for (uint8_t index = 0; index < MIDI_ROUTER_PACKET_SIZE; ++index) {
            const int high = hex_value((uint8_t)input_line[2u + 2u * index]);
            const int low = hex_value((uint8_t)input_line[3u + 2u * index]);
            if ((high < 0) || (low < 0)) {
                valid = false;
                break;
            }
            packet->bytes[index] = (uint8_t)((high << 4) | low);
        }
        /* The CDC bench is one logical cable. */
        *packet_ready = valid && ((packet->bytes[0] >> 4) == 0);
    }
    if (!bootsel_line && !route_line && !midi_line && !discard_line) {
        route_control_feed_human_line(input_line, input_length);
    }

    input_length = 0;
    discard_line = false;
    discard_route_line = false;
}

static int hex_value(uint8_t character)
{
    if ((character >= '0') && (character <= '9')) {
        return (int)(character - '0');
    }
    if ((character >= 'a') && (character <= 'f')) {
        return (int)(character - 'a') + 10;
    }
    if ((character >= 'A') && (character <= 'F')) {
        return (int)(character - 'A') + 10;
    }
    return -1;
}

bool cdc_bench_read_packet(usb_bridge_packet_t *packet)
{
    if ((packet == NULL) || bootsel_pending || !tud_cdc_connected()) {
        return false;
    }

    uint32_t consumed = 0;
    while ((tud_cdc_available() > 0u) &&
           (consumed < CDC_CONTROL_MAX_BYTES_PER_TASK)) {
        uint8_t character;
        if (tud_cdc_read(&character, 1u) != 1u) {
            return false;
        }
        ++consumed;
        bool packet_ready = false;
        process_character(character, packet, &packet_ready);
        if (bootsel_pending || packet_ready) {
            return packet_ready;
        }
    }

    return false;
}

bool cdc_bench_write_packet(const usb_bridge_packet_t *packet)
{
    static const char hex[] = "0123456789ABCDEF";
    if ((packet == NULL) || !tud_cdc_connected() ||
        ((packet->bytes[0] >> 4) != 0)) {
        return false;
    }

    char line[CDC_BENCH_PACKET_TEXT_SIZE];
    line[0] = 'M';
    line[1] = ' ';
    for (uint8_t index = 0; index < MIDI_ROUTER_PACKET_SIZE; ++index) {
        line[2u + 2u * index] = hex[packet->bytes[index] >> 4];
        line[3u + 2u * index] = hex[packet->bytes[index] & 0x0fu];
    }
    line[10] = '\n';
    return usb_bridge_cdc_write(line, sizeof(line)) == sizeof(line);
}

void cdc_bench_reset_session(bool clear_cdc_buffers)
{
    input_length = 0;
    discard_line = false;
    discard_route_line = false;
    swallow_lf = false;
    memset(input_line, 0, sizeof(input_line));
    route_control_reset_session();
    if (tud_cdc_connected()) {
        route_control_start_session();
    }
    if (clear_cdc_buffers) {
        tud_cdc_read_flush();
        (void)tud_cdc_write_clear();
    }
}

bool cdc_bench_bootsel_pending(void)
{
    return bootsel_pending;
}

void cdc_bench_service_reset(uint32_t now_ms)
{
    if (bootsel_pending && (int32_t)(now_ms - bootsel_deadline_ms) >= 0) {
        reset_usb_boot(0, 0);
    }
}

void cdc_bench_request_bootsel(void)
{
    if (!bootsel_pending) {
        bootsel_pending = true;
        bootsel_deadline_ms = to_ms_since_boot(get_absolute_time()) + 200u;
    }
}

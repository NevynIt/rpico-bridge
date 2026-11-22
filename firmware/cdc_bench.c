#include "cdc_bench.h"

#include <stddef.h>
#include <string.h>

#include "midi_router.h"
#include "pico/bootrom.h"
#include "tusb.h"

#define CDC_BENCH_MAX_LINE 10u
#define CDC_BENCH_PACKET_TEXT_SIZE 11u

static char input_line[CDC_BENCH_MAX_LINE];
static uint8_t input_length;
static bool discard_line;

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

static bool parse_complete_line(usb_bridge_packet_t *packet)
{
    if ((input_length == 7u) && (memcmp(input_line, "BOOTSEL", 7u) == 0)) {
        reset_usb_boot(0, 0);
    }

    if ((input_length != CDC_BENCH_MAX_LINE) ||
        (input_line[0] != 'M') || (input_line[1] != ' ')) {
        return false;
    }

    for (uint8_t index = 0; index < MIDI_ROUTER_PACKET_SIZE; ++index) {
        const int high = hex_value((uint8_t)input_line[2u + 2u * index]);
        const int low = hex_value((uint8_t)input_line[3u + 2u * index]);
        if ((high < 0) || (low < 0)) {
            return false;
        }
        packet->bytes[index] = (uint8_t)((high << 4) | low);
    }

    /* The CDC bench is one logical cable. */
    return (packet->bytes[0] >> 4) == 0;
}

bool cdc_bench_read_packet(usb_bridge_packet_t *packet)
{
    if ((packet == NULL) || !tud_cdc_connected()) {
        return false;
    }

    while (tud_cdc_available() > 0u) {
        uint8_t character;
        if (tud_cdc_read(&character, 1u) != 1u) {
            return false;
        }

        if (character == '\n') {
            const bool parsed = !discard_line && parse_complete_line(packet);
            input_length = 0;
            discard_line = false;
            if (parsed) {
                return true;
            }
            continue;
        }

        if (discard_line) {
            continue;
        }
        if (input_length >= CDC_BENCH_MAX_LINE) {
            input_length = 0;
            discard_line = true;
            continue;
        }
        input_line[input_length++] = (char)character;
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
    memset(input_line, 0, sizeof(input_line));
    if (clear_cdc_buffers) {
        tud_cdc_read_flush();
        (void)tud_cdc_write_clear();
    }
}

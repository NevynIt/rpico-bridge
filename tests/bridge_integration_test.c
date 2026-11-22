#include "bridge.h"
#include "usb_bridge_mock.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expression)                                                       \
    do {                                                                        \
        if (!(expression)) {                                                    \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, \
                    #expression);                                               \
            exit(EXIT_FAILURE);                                                \
        }                                                                       \
    } while (0)

static void expect_output(uint8_t endpoint, const uint8_t expected[4])
{
    uint8_t actual[4];
    CHECK(usb_mock_pop_output(endpoint, actual));
    CHECK(memcmp(actual, expected, 4) == 0);
}

static void test_concurrent_traffic_no_echo(void)
{
    usb_mock_reset();
    usb_mock_set_endpoint(0, 1, 1, 1);
    usb_mock_set_endpoint(1, 1, 1, 1);
    bridge_t bridge;
    bridge_init(&bridge);

    const uint8_t from_native[4] = {0x09, 0x92, 0x21, 0x31};
    const uint8_t from_host[4] = {0x08, 0xC1, 0x35, 0x00};
    usb_mock_push_input(0, from_native);
    usb_mock_push_input(1, from_host);
    bridge_step(&bridge);

    expect_output(0, from_host);
    expect_output(1, from_native);
    uint8_t packet[4];
    CHECK(!usb_mock_pop_output(0, packet));
    CHECK(!usb_mock_pop_output(1, packet));
    CHECK(bridge.packets_received == 2);
    CHECK(bridge.packets_sent == 2);
    CHECK(usb_mock_flush_count() == 1);
}

static void test_write_backpressure_and_reconnect(void)
{
    usb_mock_reset();
    usb_mock_set_endpoint(0, 1, 1, 10);
    usb_mock_set_endpoint(1, 1, 1, 20);
    bridge_t bridge;
    bridge_init(&bridge);

    usb_mock_set_write_ready(0, false);
    const uint8_t stale[4] = {0x09, 0x90, 0x44, 0x55};
    usb_mock_push_input(1, stale);
    bridge_step(&bridge); /* packet remains queued while endpoint 0 is blocked */
    uint8_t packet[4];
    CHECK(!usb_mock_pop_output(0, packet));

    usb_mock_set_endpoint(1, 0, 0, 0);
    bridge_step(&bridge); /* unmount purges queued source traffic */
    usb_mock_set_endpoint(1, 1, 1, 21);
    usb_mock_set_write_ready(0, true);
    bridge_step(&bridge); /* remount with reused logical slot, fresh generation */
    CHECK(!usb_mock_pop_output(0, packet));

    const uint8_t fresh[4] = {0x09, 0x90, 0x66, 0x77};
    usb_mock_push_input(1, fresh);
    bridge_step(&bridge);
    expect_output(0, fresh);
    CHECK(!usb_mock_pop_output(1, packet));
}

int main(void)
{
    test_concurrent_traffic_no_echo();
    test_write_backpressure_and_reconnect();
    puts("bridge_integration_test: passed");
    return EXIT_SUCCESS;
}

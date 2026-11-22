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
    if (!usb_mock_pop_output(endpoint, actual)) {
        fprintf(stderr, "%s:%d: no packet queued for endpoint %u\n",
                __FILE__, __LINE__, endpoint);
        exit(EXIT_FAILURE);
    }
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

static void test_native_last_host_and_cdc_fan_out(void)
{
    const uint8_t last_host_endpoint = USB_BRIDGE_HOST_ENDPOINT_FIRST +
                                       USB_BRIDGE_HOST_ENDPOINT_COUNT - 1u;
    usb_mock_reset();
    usb_mock_set_endpoint(0, 1, 1, 100);
    usb_mock_set_endpoint(last_host_endpoint, 1, 1, 108);
    usb_mock_set_endpoint(USB_BRIDGE_CDC_ENDPOINT, 1, 1, 109);
    bridge_t bridge;
    bridge_init(&bridge);

    const uint8_t from_native[4] = {0x09, 0x92, 0x21, 0x31};
    const uint8_t from_last_host[4] = {0x0b, 0xb4, 0x46, 0x57};
    const uint8_t from_cdc[4] = {0x0c, 0xc1, 0x35, 0x00};
    usb_mock_push_input(0, from_native);
    usb_mock_push_input(last_host_endpoint, from_last_host);
    usb_mock_push_input(USB_BRIDGE_CDC_ENDPOINT, from_cdc);

    bridge_step(&bridge);
    bridge_step(&bridge);

    expect_output(0, from_last_host);
    expect_output(0, from_cdc);
    expect_output(last_host_endpoint, from_native);
    expect_output(last_host_endpoint, from_cdc);
    expect_output(USB_BRIDGE_CDC_ENDPOINT, from_native);
    expect_output(USB_BRIDGE_CDC_ENDPOINT, from_last_host);

    uint8_t packet[4];
    CHECK(!usb_mock_pop_output(0, packet));
    CHECK(!usb_mock_pop_output(last_host_endpoint, packet));
    CHECK(!usb_mock_pop_output(USB_BRIDGE_CDC_ENDPOINT, packet));
    CHECK(bridge.packets_received == 3);
    CHECK(bridge.packets_sent == 6);
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

static void test_bridge_ordered_policy(void)
{
    usb_mock_reset();
    usb_mock_set_endpoint(0, 1, 1, 1);
    usb_mock_set_endpoint(1, 1, 1, 1);
    usb_mock_set_endpoint(2, 1, 1, 1);
    bridge_t bridge;
    bridge_init(&bridge);

    route_policy_t policy = {0};
    policy.default_action = ROUTE_DROP;
    policy.count = 1;
    policy.rules[0] = (route_rule_t){3, 0, 1, ROUTE_TO, 1, 0};
    CHECK(bridge_set_policy(&bridge, &policy));
    const uint8_t accepted[4] = {0x09, 0x91, 0x20, 0x30};
    const uint8_t dropped[4] = {0x08, 0x80, 0x20, 0x30};
    usb_mock_push_input(1, accepted);
    usb_mock_push_input(1, dropped);
    bridge_step(&bridge);
    expect_output(0, accepted);
    uint8_t packet[4];
    CHECK(!usb_mock_pop_output(0, packet));
    CHECK(!usb_mock_pop_output(2, packet));

    route_policy_t invalid = {0};
    invalid.default_action = ROUTE_TO;
    CHECK(!bridge_set_policy(&bridge, &invalid));
    CHECK(bridge_set_policy(&bridge, NULL));
    usb_mock_push_input(1, dropped);
    bridge_step(&bridge);
    expect_output(0, dropped);
}

int main(void)
{
    test_concurrent_traffic_no_echo();
    test_native_last_host_and_cdc_fan_out();
    test_write_backpressure_and_reconnect();
    test_bridge_ordered_policy();
    puts("bridge_integration_test: passed");
    return EXIT_SUCCESS;
}

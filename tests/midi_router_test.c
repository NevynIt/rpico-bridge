#include "midi_router.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned tests_run;

#define CHECK(expression)                                                       \
    do {                                                                        \
        ++tests_run;                                                            \
        if (!(expression)) {                                                    \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, \
                    #expression);                                               \
            exit(EXIT_FAILURE);                                                \
        }                                                                       \
    } while (0)

static void connect_endpoint(midi_router_t *router, uint8_t id,
                             uint8_t rx_cables, uint8_t tx_cables,
                             uint32_t generation)
{
    CHECK(midi_router_set_endpoint(router, id, rx_cables, tx_cables,
                                   generation, true));
}

static void take_packet(midi_router_t *router, uint8_t endpoint,
                        uint8_t cable, const uint8_t expected[4])
{
    uint8_t actual[4] = {0};
    CHECK(midi_router_peek(router, endpoint, cable, actual));
    CHECK(memcmp(actual, expected, sizeof(actual)) == 0);
    CHECK(midi_router_consume(router, endpoint, cable));
}

static void test_all_to_all_identity_and_payload(void)
{
    midi_router_t router;
    midi_router_init(&router);
    connect_endpoint(&router, 0, 2, 2, 1);
    connect_endpoint(&router, 1, 2, 2, 1);
    connect_endpoint(&router, 2, 1, 1, 1);

    const uint8_t event[] = {0x19, 0x92, 0x45, 0x67};
    CHECK(midi_router_push(&router, 1, event) == MIDI_ROUTER_PUSH_ROUTED);

    /* Same interface, other cable is a distinct destination port. */
    const uint8_t cable0[] = {0x09, 0x92, 0x45, 0x67};
    const uint8_t cable1[] = {0x19, 0x92, 0x45, 0x67};
    take_packet(&router, 1, 0, cable0);
    take_packet(&router, 0, 0, cable0);
    take_packet(&router, 0, 1, cable1);
    take_packet(&router, 2, 0, cable0);

    /* No event is queued back to the exact source endpoint and cable. */
    uint8_t packet[4];
    CHECK(!midi_router_peek(&router, 1, 1, packet));
}

static void test_direction_counts_and_point_to_point(void)
{
    midi_router_t router;
    midi_router_init(&router);
    connect_endpoint(&router, 0, 0, 1, 1); /* Native output only. */
    connect_endpoint(&router, 1, 1, 0, 1); /* Host input only. */

    const uint8_t event[] = {0x04, 0xC3, 0x2A, 0x00};
    CHECK(midi_router_push(&router, 1, event) == MIDI_ROUTER_PUSH_ROUTED);
    const uint8_t expected[] = {0x04, 0xC3, 0x2A, 0x00};
    take_packet(&router, 0, 0, expected);

    CHECK(midi_router_push(&router, 0, event) == MIDI_ROUTER_PUSH_INVALID);
    CHECK(router.invalid_packets == 1);
    CHECK(!midi_router_set_endpoint(&router, 3, 17, 0, 1, true));
}

static void test_backpressure_peek_consume(void)
{
    midi_router_t router;
    midi_router_init(&router);
    connect_endpoint(&router, 0, 1, 1, 1);
    connect_endpoint(&router, 1, 1, 1, 1);

    const uint8_t first[] = {0x09, 0x90, 0x11, 0x22};
    const uint8_t second[] = {0x08, 0x80, 0x11, 0x00};
    CHECK(midi_router_push(&router, 1, first) == MIDI_ROUTER_PUSH_ROUTED);
    CHECK(midi_router_push(&router, 1, second) == MIDI_ROUTER_PUSH_ROUTED);
    uint8_t packet[4];
    CHECK(midi_router_peek(&router, 0, 0, packet));
    CHECK(memcmp(packet, first, 4) == 0);
    /* A failed transport write leaves the head stable for retry. */
    CHECK(midi_router_peek(&router, 0, 0, packet));
    CHECK(memcmp(packet, first, 4) == 0);
    CHECK(midi_router_consume(&router, 0, 0));
    take_packet(&router, 0, 0, second);
}

static void test_sysex_arbitration_and_realtime(void)
{
    midi_router_t router;
    midi_router_init(&router);
    connect_endpoint(&router, 0, 1, 1, 1);
    connect_endpoint(&router, 1, 1, 1, 1);
    connect_endpoint(&router, 2, 1, 1, 1);

    const uint8_t a_start[] = {0x04, 0xF0, 0x01, 0x02};
    const uint8_t b_start[] = {0x04, 0xF0, 0x11, 0x12};
    const uint8_t clock[] = {0x0F, 0xF8, 0x00, 0x00};
    const uint8_t a_continue[] = {0x04, 0x03, 0x04, 0x05};
    const uint8_t a_end[] = {0x06, 0x06, 0xF7, 0x00};

    CHECK(midi_router_push(&router, 0, a_start) == MIDI_ROUTER_PUSH_ROUTED);
    CHECK(midi_router_push(&router, 1, b_start) == MIDI_ROUTER_PUSH_ROUTED);
    CHECK(midi_router_push(&router, 1, clock) == MIDI_ROUTER_PUSH_ROUTED);
    CHECK(midi_router_push(&router, 0, a_continue) == MIDI_ROUTER_PUSH_ROUTED);
    CHECK(midi_router_push(&router, 0, a_end) == MIDI_ROUTER_PUSH_ROUTED);

    const uint8_t routed_a_start[] = {0x04, 0xF0, 0x01, 0x02};
    const uint8_t routed_clock[] = {0x0F, 0xF8, 0x00, 0x00};
    const uint8_t routed_a_continue[] = {0x04, 0x03, 0x04, 0x05};
    const uint8_t routed_a_end[] = {0x06, 0x06, 0xF7, 0x00};
    const uint8_t routed_b_start[] = {0x04, 0xF0, 0x11, 0x12};

    take_packet(&router, 2, 0, routed_a_start);
    take_packet(&router, 2, 0, routed_clock); /* realtime may pass the lock */
    take_packet(&router, 2, 0, routed_a_continue);
    take_packet(&router, 2, 0, routed_a_end);
    take_packet(&router, 2, 0, routed_b_start); /* second SysEx waits */
}

static void test_overflow_is_bounded_and_drop_newest(void)
{
    midi_router_t router;
    midi_router_init(&router);
    connect_endpoint(&router, 0, 1, 1, 1);
    connect_endpoint(&router, 1, 1, 0, 1);
    connect_endpoint(&router, 2, 1, 1, 1);

    for (uint8_t i = 0; i < MIDI_ROUTER_QUEUE_DEPTH + 2u; ++i) {
        const uint8_t event[] = {0x09, 0x90, i, (uint8_t)(100u + i)};
        const midi_router_push_result_t result =
            midi_router_push(&router, 1, event);
        CHECK(result == MIDI_ROUTER_PUSH_ROUTED ||
              result == MIDI_ROUTER_PUSH_DROPPED);
    }
    CHECK(router.outputs[0][0].count == MIDI_ROUTER_QUEUE_DEPTH);
    CHECK(router.outputs[2][0].count == MIDI_ROUTER_QUEUE_DEPTH);
    CHECK(router.overflow_drops == 4); /* two overflows at each destination */

    for (uint8_t i = 0; i < MIDI_ROUTER_QUEUE_DEPTH; ++i) {
        const uint8_t expected[] = {0x09, 0x90, i, (uint8_t)(100u + i)};
        take_packet(&router, 0, 0, expected);
    }
    uint8_t packet[4];
    CHECK(!midi_router_peek(&router, 0, 0, packet));
}

static void test_disconnect_purges_and_reconnects(void)
{
    midi_router_t router;
    midi_router_init(&router);
    connect_endpoint(&router, 0, 1, 1, 4);
    connect_endpoint(&router, 1, 1, 1, 8);
    const uint8_t old_event[] = {0x09, 0x90, 0x31, 0x40};
    CHECK(midi_router_push(&router, 1, old_event) == MIDI_ROUTER_PUSH_ROUTED);

    midi_router_remove_endpoint(&router, 1, 9);
    uint8_t packet[4];
    CHECK(!midi_router_peek(&router, 0, 0, packet));
    connect_endpoint(&router, 1, 1, 1, 10);
    CHECK(midi_router_push(&router, 1, old_event) == MIDI_ROUTER_PUSH_ROUTED);
    const uint8_t expected[] = {0x09, 0x90, 0x31, 0x40};
    take_packet(&router, 0, 0, expected);
}

static void test_cin_f_raw_data_vs_realtime(void)
{
    midi_router_t router;
    midi_router_init(&router);
    connect_endpoint(&router, 0, 1, 1, 1);
    connect_endpoint(&router, 1, 1, 1, 1);
    connect_endpoint(&router, 2, 1, 1, 1);

    const uint8_t raw_start[] = {0x0F, 0xF0, 0x00, 0x00};
    const uint8_t competing_note[] = {0x09, 0x90, 0x22, 0x33};
    const uint8_t realtime[] = {0x0F, 0xF8, 0x00, 0x00};
    const uint8_t raw_data[] = {0x0F, 0x55, 0x00, 0x00};
    const uint8_t raw_end[] = {0x0F, 0xF7, 0x00, 0x00};
    CHECK(midi_router_push(&router, 0, raw_start) == MIDI_ROUTER_PUSH_ROUTED);
    CHECK(midi_router_push(&router, 1, competing_note) == MIDI_ROUTER_PUSH_ROUTED);
    CHECK(midi_router_push(&router, 1, realtime) == MIDI_ROUTER_PUSH_ROUTED);
    CHECK(midi_router_push(&router, 0, raw_data) == MIDI_ROUTER_PUSH_ROUTED);
    CHECK(midi_router_push(&router, 0, raw_end) == MIDI_ROUTER_PUSH_ROUTED);

    const uint8_t routed_start[] = {0x0F, 0xF0, 0x00, 0x00};
    const uint8_t routed_clock[] = {0x0F, 0xF8, 0x00, 0x00};
    const uint8_t routed_end[] = {0x0F, 0xF7, 0x00, 0x00};
    const uint8_t routed_note[] = {0x09, 0x90, 0x22, 0x33};
    take_packet(&router, 2, 0, routed_start);
    take_packet(&router, 2, 0, routed_clock);
    take_packet(&router, 2, 0, raw_data);
    take_packet(&router, 2, 0, routed_end);
    take_packet(&router, 2, 0, routed_note);
}

static void test_late_destination_joins_only_at_fresh_f0(void)
{
    midi_router_t router;
    midi_router_init(&router);
    connect_endpoint(&router, 0, 1, 0, 1);
    connect_endpoint(&router, 1, 1, 0, 1);
    const uint8_t start[] = {0x04, 0xF0, 0x01, 0x02};
    CHECK(midi_router_push(&router, 0, start) == MIDI_ROUTER_PUSH_NO_DESTINATION);

    connect_endpoint(&router, 2, 1, 1, 1);
    const uint8_t continuation[] = {0x04, 0x03, 0x04, 0x05};
    const uint8_t end[] = {0x06, 0x06, 0xF7, 0x00};
    CHECK(midi_router_push(&router, 0, continuation) ==
          MIDI_ROUTER_PUSH_NO_DESTINATION);
    CHECK(midi_router_push(&router, 0, end) == MIDI_ROUTER_PUSH_NO_DESTINATION);
    uint8_t packet[4];
    CHECK(!midi_router_peek(&router, 2, 0, packet));

    CHECK(midi_router_push(&router, 0, start) == MIDI_ROUTER_PUSH_ROUTED);
    CHECK(midi_router_push(&router, 0, continuation) == MIDI_ROUTER_PUSH_ROUTED);
    CHECK(midi_router_push(&router, 0, end) == MIDI_ROUTER_PUSH_ROUTED);
    take_packet(&router, 2, 0, start);
    take_packet(&router, 2, 0, continuation);
    take_packet(&router, 2, 0, end);
}

static void test_late_destination_receives_realtime_during_sysex(void)
{
    midi_router_t router;
    midi_router_init(&router);
    connect_endpoint(&router, 0, 1, 0, 1);
    connect_endpoint(&router, 1, 1, 0, 1);
    const uint8_t start[] = {0x04, 0xF0, 0x01, 0x02};
    CHECK(midi_router_push(&router, 0, start) == MIDI_ROUTER_PUSH_NO_DESTINATION);
    connect_endpoint(&router, 2, 1, 1, 1);
    const uint8_t clock[] = {0x0F, 0xF8, 0x00, 0x00};
    const uint8_t continuation[] = {0x04, 0x03, 0x04, 0x05};
    CHECK(midi_router_push(&router, 0, clock) == MIDI_ROUTER_PUSH_ROUTED);
    CHECK(midi_router_push(&router, 0, continuation) ==
          MIDI_ROUTER_PUSH_NO_DESTINATION);
    take_packet(&router, 2, 0, clock);
}

static void test_disconnect_closes_started_sysex(void)
{
    midi_router_t router;
    midi_router_init(&router);
    connect_endpoint(&router, 0, 1, 0, 1);
    connect_endpoint(&router, 1, 1, 1, 1);
    connect_endpoint(&router, 2, 1, 0, 1);

    const uint8_t start[] = {0x04, 0xF0, 0x01, 0x02};
    const uint8_t competing[] = {0x09, 0x90, 0x20, 0x30};
    CHECK(midi_router_push(&router, 0, start) == MIDI_ROUTER_PUSH_ROUTED);
    take_packet(&router, 1, 0, start);
    CHECK(midi_router_push(&router, 2, competing) == MIDI_ROUTER_PUSH_ROUTED);
    midi_router_remove_endpoint(&router, 0, 2);

    const uint8_t synthetic_end[] = {0x05, 0xF7, 0x00, 0x00};
    take_packet(&router, 1, 0, synthetic_end);
    take_packet(&router, 1, 0, competing);
}

static void test_overflowed_eox_synthesizes_close(void)
{
    midi_router_t router;
    midi_router_init(&router);
    connect_endpoint(&router, 0, 1, 0, 1);
    connect_endpoint(&router, 1, 1, 1, 1);
    connect_endpoint(&router, 2, 1, 0, 1);

    const uint8_t start[] = {0x04, 0xF0, 0x01, 0x02};
    CHECK(midi_router_push(&router, 0, start) == MIDI_ROUTER_PUSH_ROUTED);
    take_packet(&router, 1, 0, start); /* lock is active at the destination */

    for (uint8_t i = 0; i < MIDI_ROUTER_QUEUE_DEPTH; ++i) {
        const uint8_t event[] = {0x09, 0x90, i, 0x40};
        CHECK(midi_router_push(&router, 2, event) == MIDI_ROUTER_PUSH_ROUTED);
    }
    const uint8_t end[] = {0x06, 0x05, 0xF7, 0x00};
    CHECK(midi_router_push(&router, 0, end) == MIDI_ROUTER_PUSH_DROPPED);
    const uint8_t synthetic_end[] = {0x05, 0xF7, 0x00, 0x00};
    take_packet(&router, 1, 0, synthetic_end);
    const uint8_t first_waiting[] = {0x09, 0x90, 0x00, 0x40};
    take_packet(&router, 1, 0, first_waiting);
}

static void test_back_to_back_sysex_streams(void)
{
    midi_router_t router;
    midi_router_init(&router);
    connect_endpoint(&router, 0, 1, 0, 1);
    connect_endpoint(&router, 1, 1, 1, 1);

    const uint8_t first_start[] = {0x04, 0xF0, 0x01, 0x02};
    const uint8_t first_end[] = {0x06, 0x03, 0xF7, 0x00};
    const uint8_t second_start[] = {0x04, 0xF0, 0x11, 0x12};
    const uint8_t second_end[] = {0x06, 0x13, 0xF7, 0x00};
    CHECK(midi_router_push(&router, 0, first_start) == MIDI_ROUTER_PUSH_ROUTED);
    CHECK(midi_router_push(&router, 0, first_end) == MIDI_ROUTER_PUSH_ROUTED);
    CHECK(midi_router_push(&router, 0, second_start) == MIDI_ROUTER_PUSH_ROUTED);
    CHECK(midi_router_push(&router, 0, second_end) == MIDI_ROUTER_PUSH_ROUTED);

    take_packet(&router, 1, 0, first_start);
    take_packet(&router, 1, 0, first_end);
    take_packet(&router, 1, 0, second_start);
    take_packet(&router, 1, 0, second_end);
}

static void test_overflow_of_unstarted_sysex_purges_f0(void)
{
    midi_router_t router;
    midi_router_init(&router);
    connect_endpoint(&router, 0, 1, 0, 1);
    connect_endpoint(&router, 1, 1, 1, 1);
    connect_endpoint(&router, 2, 1, 0, 1);
    for (uint8_t i = 0; i < MIDI_ROUTER_QUEUE_DEPTH; ++i) {
        const uint8_t event[] = {0x09, 0x90, i, 0x40};
        CHECK(midi_router_push(&router, 2, event) == MIDI_ROUTER_PUSH_ROUTED);
    }
    const uint8_t start[] = {0x04, 0xF0, 0x01, 0x02};
    CHECK(midi_router_push(&router, 0, start) == MIDI_ROUTER_PUSH_DROPPED);
    for (uint8_t i = 0; i < MIDI_ROUTER_QUEUE_DEPTH; ++i) {
        const uint8_t expected[] = {0x09, 0x90, i, 0x40};
        take_packet(&router, 1, 0, expected);
    }
    uint8_t packet[4];
    CHECK(!midi_router_peek(&router, 1, 0, packet));
}

static void test_interrupted_sysex_preserves_prior_complete_stream(void)
{
    midi_router_t router;
    midi_router_init(&router);
    connect_endpoint(&router, 0, 1, 0, 1);
    connect_endpoint(&router, 1, 1, 1, 1);
    const uint8_t first_start[] = {0x04, 0xF0, 0x01, 0x02};
    const uint8_t first_end[] = {0x06, 0x03, 0xF7, 0x00};
    const uint8_t interrupted_start[] = {0x04, 0xF0, 0x11, 0x12};
    const uint8_t note[] = {0x09, 0x90, 0x20, 0x30};
    CHECK(midi_router_push(&router, 0, first_start) == MIDI_ROUTER_PUSH_ROUTED);
    take_packet(&router, 1, 0, first_start);
    CHECK(midi_router_push(&router, 0, first_end) == MIDI_ROUTER_PUSH_ROUTED);
    CHECK(midi_router_push(&router, 0, interrupted_start) ==
          MIDI_ROUTER_PUSH_ROUTED);
    CHECK(midi_router_push(&router, 0, note) == MIDI_ROUTER_PUSH_ROUTED);
    take_packet(&router, 1, 0, first_end);
    take_packet(&router, 1, 0, note);
}

static void test_interrupted_started_sysex_discards_queued_fragments(void)
{
    midi_router_t router;
    midi_router_init(&router);
    connect_endpoint(&router, 0, 1, 0, 1);
    connect_endpoint(&router, 1, 1, 1, 1);
    const uint8_t start[] = {0x04, 0xF0, 0x01, 0x02};
    const uint8_t continuation[] = {0x04, 0x03, 0x04, 0x05};
    const uint8_t replacement[] = {0x07, 0xF0, 0x21, 0xF7};
    const uint8_t synthetic_end[] = {0x05, 0xF7, 0x00, 0x00};

    CHECK(midi_router_push(&router, 0, start) == MIDI_ROUTER_PUSH_ROUTED);
    take_packet(&router, 1, 0, start);
    CHECK(midi_router_push(&router, 0, continuation) == MIDI_ROUTER_PUSH_ROUTED);
    CHECK(midi_router_push(&router, 0, replacement) == MIDI_ROUTER_PUSH_ROUTED);
    take_packet(&router, 1, 0, synthetic_end);
    take_packet(&router, 1, 0, replacement);
    uint8_t packet[4];
    CHECK(!midi_router_peek(&router, 1, 0, packet));
}

static void test_midstream_overflow_aborts_admission(void)
{
    midi_router_t router;
    midi_router_init(&router);
    connect_endpoint(&router, 0, 1, 0, 1);
    connect_endpoint(&router, 1, 1, 1, 1);
    connect_endpoint(&router, 2, 1, 0, 1);
    const uint8_t start[] = {0x04, 0xF0, 0x01, 0x02};
    CHECK(midi_router_push(&router, 0, start) == MIDI_ROUTER_PUSH_ROUTED);
    take_packet(&router, 1, 0, start);

    for (uint8_t i = 0; i < MIDI_ROUTER_QUEUE_DEPTH; ++i) {
        const uint8_t event[] = {0x09, 0x90, i, 0x40};
        CHECK(midi_router_push(&router, 2, event) == MIDI_ROUTER_PUSH_ROUTED);
    }
    const uint8_t continuation[] = {0x04, 0x03, 0x04, 0x05};
    const uint8_t end[] = {0x06, 0x06, 0xF7, 0x00};
    CHECK(midi_router_push(&router, 0, continuation) == MIDI_ROUTER_PUSH_DROPPED);
    const uint8_t synthetic_end[] = {0x05, 0xF7, 0x00, 0x00};
    take_packet(&router, 1, 0, synthetic_end);
    CHECK(midi_router_push(&router, 0, continuation) ==
          MIDI_ROUTER_PUSH_NO_DESTINATION);
    CHECK(midi_router_push(&router, 0, end) == MIDI_ROUTER_PUSH_NO_DESTINATION);
    for (uint8_t i = 0; i < MIDI_ROUTER_QUEUE_DEPTH; ++i) {
        const uint8_t event[] = {0x09, 0x90, i, 0x40};
        take_packet(&router, 1, 0, event);
    }
    uint8_t packet[4];
    CHECK(!midi_router_peek(&router, 1, 0, packet));
}

int main(void)
{
    test_all_to_all_identity_and_payload();
    test_direction_counts_and_point_to_point();
    test_backpressure_peek_consume();
    test_sysex_arbitration_and_realtime();
    test_overflow_is_bounded_and_drop_newest();
    test_disconnect_purges_and_reconnects();
    test_cin_f_raw_data_vs_realtime();
    test_late_destination_joins_only_at_fresh_f0();
    test_late_destination_receives_realtime_during_sysex();
    test_disconnect_closes_started_sysex();
    test_overflowed_eox_synthesizes_close();
    test_back_to_back_sysex_streams();
    test_overflow_of_unstarted_sysex_purges_f0();
    test_interrupted_sysex_preserves_prior_complete_stream();
    test_interrupted_started_sysex_discards_queued_fragments();
    test_midstream_overflow_aborts_admission();
    printf("midi_router_test: %u checks passed\n", tests_run);
    return EXIT_SUCCESS;
}

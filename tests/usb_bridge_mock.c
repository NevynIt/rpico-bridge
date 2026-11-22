#include "usb_bridge_mock.h"

#include <string.h>

#define MOCK_PACKET_QUEUE 64u

typedef struct {
    uint8_t packets[MOCK_PACKET_QUEUE][4];
    uint8_t head;
    uint8_t count;
} packet_queue_t;

static usb_bridge_endpoint_t endpoints[USB_BRIDGE_ENDPOINT_COUNT];
static packet_queue_t input_queues[USB_BRIDGE_ENDPOINT_COUNT];
static packet_queue_t output_queues[USB_BRIDGE_ENDPOINT_COUNT];
static bool write_ready[USB_BRIDGE_ENDPOINT_COUNT];
static uint32_t flushes;

static bool queue_push(packet_queue_t *queue, const uint8_t packet[4])
{
    if (queue->count == MOCK_PACKET_QUEUE) {
        return false;
    }
    uint8_t tail = (uint8_t)((queue->head + queue->count) % MOCK_PACKET_QUEUE);
    memcpy(queue->packets[tail], packet, 4);
    ++queue->count;
    return true;
}

static bool queue_pop(packet_queue_t *queue, uint8_t packet[4])
{
    if (queue->count == 0) {
        return false;
    }
    memcpy(packet, queue->packets[queue->head], 4);
    queue->head = (uint8_t)((queue->head + 1u) % MOCK_PACKET_QUEUE);
    --queue->count;
    return true;
}

void usb_mock_reset(void)
{
    memset(endpoints, 0, sizeof(endpoints));
    memset(input_queues, 0, sizeof(input_queues));
    memset(output_queues, 0, sizeof(output_queues));
    memset(write_ready, 1, sizeof(write_ready));
    flushes = 0;
}

void usb_mock_set_endpoint(uint8_t endpoint, uint8_t rx_cables,
                           uint8_t tx_cables, uint32_t generation)
{
    endpoints[endpoint].rx_cables = generation == 0 ? 0 : rx_cables;
    endpoints[endpoint].tx_cables = generation == 0 ? 0 : tx_cables;
    endpoints[endpoint].generation = generation;
}

void usb_mock_push_input(uint8_t endpoint, const uint8_t packet[4])
{
    (void)queue_push(&input_queues[endpoint], packet);
}

void usb_mock_set_write_ready(uint8_t endpoint, bool ready)
{
    write_ready[endpoint] = ready;
}

bool usb_mock_pop_output(uint8_t endpoint, uint8_t packet[4])
{
    return queue_pop(&output_queues[endpoint], packet);
}

uint32_t usb_mock_flush_count(void)
{
    return flushes;
}

bool usb_bridge_init(void) { return true; }
void usb_bridge_task(void) {}
void usb_bridge_flush(void) { ++flushes; }

bool usb_bridge_endpoint_get(uint8_t endpoint, usb_bridge_endpoint_t *out)
{
    if (endpoint >= USB_BRIDGE_ENDPOINT_COUNT || out == NULL) {
        return false;
    }
    *out = endpoints[endpoint];
    return true;
}

bool usb_bridge_read_packet(uint8_t endpoint, usb_bridge_packet_t *out)
{
    return endpoint < USB_BRIDGE_ENDPOINT_COUNT && out != NULL &&
           queue_pop(&input_queues[endpoint], out->bytes);
}

bool usb_bridge_try_write(uint8_t endpoint, const usb_bridge_packet_t *packet)
{
    return endpoint < USB_BRIDGE_ENDPOINT_COUNT && packet != NULL &&
           endpoints[endpoint].generation != 0 && write_ready[endpoint] &&
           queue_push(&output_queues[endpoint], packet->bytes);
}

uint32_t usb_bridge_cdc_write(const void *data, uint32_t length)
{
    (void)data;
    return length;
}

uint32_t usb_bridge_cdc_read(void *data, uint32_t capacity)
{
    (void)data;
    (void)capacity;
    return 0;
}

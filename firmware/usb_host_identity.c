#include "usb_host_identity.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "route_store.h"
#include "tusb.h"

#define IDENTITY_STRING_BYTES 66u
#define IDENTITY_TOPOLOGY_DEPTH 4u

typedef enum {
    IDLE = 0,
    LANG_PENDING, GET_LANG,
    SERIAL_PENDING, GET_SERIAL,
    PRODUCT_PENDING, GET_PRODUCT,
    READY
} identity_state_t;

typedef struct {
    identity_state_t state;
    uint8_t daddr;
    uint8_t interface_number;
    uint8_t product_index;
    uint8_t serial_index;
    uint16_t language;
    uint16_t vid;
    uint16_t pid;
    uint32_t token;
    uint64_t key;
    uint8_t route_id;
    char product[ROUTE_STORE_NAME_MAX_BYTES + 1u];
    char serial[IDENTITY_STRING_BYTES / 2u];
    uint8_t string_buffer[IDENTITY_STRING_BYTES];
} identity_slot_t;

static identity_slot_t slots[CFG_TUH_MIDI];
static uint32_t next_token = 1;

static uint64_t hash_byte(uint64_t hash, uint8_t byte)
{
    return (hash ^ byte) * UINT64_C(1099511628211);
}

static uint64_t hash_u16(uint64_t hash, uint16_t value)
{
    hash = hash_byte(hash, (uint8_t)value);
    return hash_byte(hash, (uint8_t)(value >> 8u));
}

static uint64_t build_key(const identity_slot_t *slot, const char *serial)
{
    uint64_t hash = UINT64_C(1469598103934665603);
    hash = hash_byte(hash, 0x52u);
    hash = hash_u16(hash, slot->vid);
    hash = hash_u16(hash, slot->pid);
    hash = hash_byte(hash, slot->interface_number);
    if (serial != NULL && serial[0] != '\0') {
        hash = hash_byte(hash, 0x53u);
        while (*serial != '\0') hash = hash_byte(hash, (uint8_t)*serial++);
    } else {
        hash = hash_byte(hash, 0x50u);
        uint8_t daddr = slot->daddr;
        tuh_bus_info_t info;
        uint8_t path[IDENTITY_TOPOLOGY_DEPTH];
        uint8_t count = 0;
        bool have_topology = false;
        while (daddr != 0u && count < IDENTITY_TOPOLOGY_DEPTH &&
               tuh_bus_info_get(daddr, &info)) {
            have_topology = true;
            path[count++] = info.hub_port;
            if (info.hub_addr == 0u) {
                hash = hash_byte(hash, info.rhport);
                break;
            }
            daddr = info.hub_addr;
        }
        hash = hash_byte(hash, count);
        while (count > 0u) hash = hash_byte(hash, path[--count]);
        if (!have_topology) hash = hash_byte(hash, slot->daddr);
    }
    return hash == 0u ? 1u : hash;
}

static uint8_t utf8_put(char *out, uint8_t used, uint32_t cp)
{
    if (cp < 0x80u) out[used++] = (char)cp;
    else if (cp < 0x800u) {
        out[used++] = (char)(0xc0u | (cp >> 6u));
        out[used++] = (char)(0x80u | (cp & 0x3fu));
    } else if (cp < 0x10000u) {
        out[used++] = (char)(0xe0u | (cp >> 12u));
        out[used++] = (char)(0x80u | ((cp >> 6u) & 0x3fu));
        out[used++] = (char)(0x80u | (cp & 0x3fu));
    } else {
        out[used++] = (char)(0xf0u | (cp >> 18u));
        out[used++] = (char)(0x80u | ((cp >> 12u) & 0x3fu));
        out[used++] = (char)(0x80u | ((cp >> 6u) & 0x3fu));
        out[used++] = (char)(0x80u | (cp & 0x3fu));
    }
    return used;
}

static void decode_product(identity_slot_t *slot, uint32_t actual_len)
{
    memset(slot->product, 0, sizeof(slot->product));
    if (actual_len < 2u || slot->string_buffer[1] != TUSB_DESC_STRING) return;
    uint8_t desc_len = slot->string_buffer[0];
    if (desc_len > actual_len) desc_len = (uint8_t)actual_len;
    if (desc_len < 2u || (desc_len & 1u) != 0u) return;
    uint8_t used = 0;
    for (uint8_t i = 2u; i + 1u < desc_len;) {
        uint32_t cp = (uint32_t)slot->string_buffer[i] |
                      ((uint32_t)slot->string_buffer[i + 1u] << 8u);
        i = (uint8_t)(i + 2u);
        if (cp >= 0xd800u && cp <= 0xdbffu) {
            if (i + 1u >= desc_len) { cp = '?'; }
            else {
                uint32_t low = (uint32_t)slot->string_buffer[i] |
                               ((uint32_t)slot->string_buffer[i + 1u] << 8u);
                if (low < 0xdc00u || low > 0xdfffu) cp = '?';
                else {
                    cp = 0x10000u + ((cp - 0xd800u) << 10u) + (low - 0xdc00u);
                    i = (uint8_t)(i + 2u);
                }
            }
        } else if (cp >= 0xdc00u && cp <= 0xdfffu) cp = '?';
        uint8_t bytes = cp < 0x80u ? 1u : cp < 0x800u ? 2u : cp < 0x10000u ? 3u : 4u;
        if ((uint8_t)(used + bytes) > ROUTE_STORE_NAME_MAX_BYTES) break;
        used = utf8_put(slot->product, used, cp);
    }
}

static bool token_matches(uint8_t idx, uintptr_t user_data, const tuh_xfer_t *xfer)
{
    uint8_t callback_idx = (uint8_t)(user_data & 0xffu);
    uint32_t token = (uint32_t)(user_data >> 8u);
    return idx == callback_idx && idx < CFG_TUH_MIDI &&
           slots[idx].token == token && slots[idx].daddr == xfer->daddr &&
           slots[idx].state != IDLE;
}

static uintptr_t slot_token(uint8_t idx)
{
    return ((uintptr_t)slots[idx].token << 8u) | idx;
}

static void descriptor_string_complete(tuh_xfer_t *xfer);

static void start_pending_requests(void)
{
    for (uint8_t idx = 0; idx < CFG_TUH_MIDI; ++idx) {
        identity_slot_t *slot = &slots[idx];
        bool accepted = false;
        if (slot->state == LANG_PENDING) {
            accepted = tuh_descriptor_get_string_langid(slot->daddr,
                slot->string_buffer, sizeof(slot->string_buffer),
                descriptor_string_complete, slot_token(idx));
            if (accepted) slot->state = GET_LANG;
        } else if (slot->state == SERIAL_PENDING) {
            accepted = tuh_descriptor_get_string(slot->daddr, slot->serial_index,
                slot->language, slot->string_buffer, sizeof(slot->string_buffer),
                descriptor_string_complete, slot_token(idx));
            if (accepted) slot->state = GET_SERIAL;
        } else if (slot->state == PRODUCT_PENDING) {
            accepted = tuh_descriptor_get_string(slot->daddr, slot->product_index,
                slot->language, slot->string_buffer, sizeof(slot->string_buffer),
                descriptor_string_complete, slot_token(idx));
            if (accepted) slot->state = GET_PRODUCT;
        }
    }
}

static void resolve_slot(uint8_t idx, const char *serial)
{
    identity_slot_t *slot = &slots[idx];
    uint64_t key = build_key(slot, serial);
    for (uint8_t i = 0; i < CFG_TUH_MIDI; ++i) {
        if (i != idx && slots[i].state == READY && slots[i].key == key) {
            key = build_key(slot, NULL);
            break;
        }
    }
    slot->key = key;
    const char *name = slot->product[0] == '\0' ? NULL : slot->product;
    slot->route_id = route_store_register(key, name);
    slot->state = READY;
}

static void descriptor_string_complete(tuh_xfer_t *xfer)
{
    uint8_t idx = (uint8_t)(xfer->user_data & 0xffu);
    if (idx >= CFG_TUH_MIDI || !token_matches(idx, xfer->user_data, xfer)) return;
    identity_slot_t *slot = &slots[idx];
    bool success = xfer->result == XFER_RESULT_SUCCESS;
    if (slot->state == GET_LANG) {
        if (success && xfer->actual_len >= 4u &&
            slot->string_buffer[1] == TUSB_DESC_STRING &&
            slot->string_buffer[0] >= 4u && slot->string_buffer[0] <= xfer->actual_len) {
            slot->language = (uint16_t)slot->string_buffer[2] |
                             ((uint16_t)slot->string_buffer[3] << 8u);
        } else slot->language = 0x0409u;
        if (slot->serial_index != 0u) slot->state = SERIAL_PENDING;
        else if (slot->product_index != 0u) slot->state = PRODUCT_PENDING;
        else resolve_slot(idx, NULL);
    } else if (slot->state == GET_SERIAL) {
        memset(slot->serial, 0, sizeof(slot->serial));
        if (success && xfer->actual_len >= 2u &&
            slot->string_buffer[1] == TUSB_DESC_STRING) {
            uint8_t length = slot->string_buffer[0];
            if (length > xfer->actual_len) length = (uint8_t)xfer->actual_len;
            if (length >= 2u && (length & 1u) == 0u) {
                uint8_t out = 0;
                for (uint8_t i = 2u; i + 1u < length && out + 1u < sizeof(slot->serial); i += 2u) {
                    uint16_t cp = (uint16_t)slot->string_buffer[i] |
                                  ((uint16_t)slot->string_buffer[i + 1u] << 8u);
                    slot->serial[out++] = (cp >= 0x20u && cp < 0x7fu) ? (char)cp : '?';
                }
            }
        }
        if (slot->product_index != 0u) slot->state = PRODUCT_PENDING;
        else resolve_slot(idx, slot->serial[0] == '\0' ? NULL : slot->serial);
    } else if (slot->state == GET_PRODUCT) {
        if (success) decode_product(slot, xfer->actual_len);
        resolve_slot(idx, slot->serial[0] == '\0' ? NULL : slot->serial);
    }
}

void usb_host_identity_mount(uint8_t idx, uint8_t daddr, uint8_t interface_number)
{
    if (idx >= CFG_TUH_MIDI) return;
    identity_slot_t *slot = &slots[idx];
    memset(slot, 0, sizeof(*slot));
    slot->state = LANG_PENDING;
    slot->daddr = daddr;
    slot->interface_number = interface_number;
    slot->token = next_token++;
    slot->token &= UINT32_C(0x00ffffff);
    if (slot->token == 0u) {
        slot->token = next_token++ & UINT32_C(0x00ffffff);
        if (slot->token == 0u) slot->token = 1u;
    }
    tusb_desc_device_t device;
    if (!tuh_descriptor_get_device_local(daddr, &device)) {
        resolve_slot(idx, NULL);
        return;
    }
    slot->vid = device.idVendor;
    slot->pid = device.idProduct;
    slot->product_index = device.iProduct;
    slot->serial_index = device.iSerialNumber;
    if (slot->product_index == 0u && slot->serial_index == 0u) {
        resolve_slot(idx, NULL);
        return;
    }
    start_pending_requests();
}

void usb_host_identity_unmount(uint8_t idx)
{
    if (idx < CFG_TUH_MIDI) {
        memset(&slots[idx], 0, sizeof(slots[idx]));
    }
}

uint8_t usb_host_identity_route_id(uint8_t idx, uint8_t daddr)
{
    if (idx >= CFG_TUH_MIDI || slots[idx].daddr != daddr || slots[idx].state != READY) return 0;
    return slots[idx].route_id;
}

void usb_host_identity_task(void)
{
    start_pending_requests();
}

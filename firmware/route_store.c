#include "route_store.h"

#include <stddef.h>
#include <string.h>

#include "hardware/flash.h"
#include "hardware/sync.h"
#include "hardware/watchdog.h"
#include "pico/flash.h"
#include "pico/stdlib.h"
#include "usb_bridge.h"

#define STORE_MAGIC UINT32_C(0x52505354)
#define STORE_VERSION 1u
#define STORE_FIRST_ID 3u
#define STORE_SLOT_COUNT (ROUTE_STORE_MAX_ID - STORE_FIRST_ID + 1u)
#define STORE_SAVE_QUIET_MS 2000u
#define STORE_SAVE_MAX_DELAY_MS 12000u
#define STORE_RETRY_MS 5000u
#define STORE_OFFSET_A (PICO_FLASH_SIZE_BYTES - 2u * FLASH_SECTOR_SIZE)
#define STORE_OFFSET_B (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)

typedef struct {
    uint64_t key;
    uint8_t id;
    uint8_t reserved[7];
    char name[ROUTE_STORE_NAME_MAX_BYTES + 1u];
} store_device_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t payload_size;
    uint32_t generation;
    uint32_t crc32;
    route_policy_t policy;
    store_device_t devices[STORE_SLOT_COUNT];
} store_record_t;

typedef struct {
    uint32_t offset;
    const uint8_t *data;
} flash_write_t;

static route_policy_t active_policy;
static store_device_t devices[STORE_SLOT_COUNT];
static bool dirty;
static bool force_save;
static bool dirty_time_valid;
static bool retry_time_valid;
static uint32_t dirty_since_ms;
static uint32_t last_change_ms;
static uint32_t last_midi_ms;
static uint32_t retry_after_ms;
static uint32_t restart_deadline_ms;
static uint32_t active_generation;
static int8_t active_slot = -1;
static bool storage_available;
static bool last_midi_time_valid;
static bool restart_pending;
static _Alignas(4) uint8_t write_sector[FLASH_SECTOR_SIZE];
static const char *last_error = "none";

static uint32_t crc32_record(const uint8_t *data, size_t length)
{
    uint32_t crc = UINT32_MAX;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8u; ++bit) {
            crc = (crc >> 1u) ^ (UINT32_C(0xedb88320) &
                                (uint32_t)-(int32_t)(crc & 1u));
        }
    }
    return ~crc;
}

static bool generation_is_newer(uint32_t a, uint32_t b)
{
    return (int32_t)(a - b) > 0;
}

static const store_record_t *slot_record(uint8_t slot)
{
    uint32_t offset = (slot == 0u) ? STORE_OFFSET_A : STORE_OFFSET_B;
    return (const store_record_t *)(uintptr_t)(XIP_BASE + offset);
}

static bool record_valid(const store_record_t *record)
{
    if (record->magic != STORE_MAGIC || record->version != STORE_VERSION ||
        record->payload_size != sizeof(*record) ||
        !route_policy_validate(&record->policy)) {
        return false;
    }
    store_record_t copy;
    memcpy(&copy, record, sizeof(copy));
    uint32_t expected = copy.crc32;
    copy.crc32 = 0;
    if (crc32_record((const uint8_t *)&copy, sizeof(copy)) != expected) {
        return false;
    }

    bool ids[ROUTE_STORE_MAX_ID + 1u] = {false};
    for (uint8_t i = 0; i < STORE_SLOT_COUNT; ++i) {
        const store_device_t *device = &record->devices[i];
        if (device->key == 0u) {
            if (device->id != 0u) return false;
            continue;
        }
        if (device->id < STORE_FIRST_ID || device->id > ROUTE_STORE_MAX_ID ||
            ids[device->id] || memchr(device->name, '\0', sizeof(device->name)) == NULL) {
            return false;
        }
        ids[device->id] = true;
        for (uint8_t j = 0; j < i; ++j) {
            if (record->devices[j].key == device->key) return false;
        }
    }
    return true;
}

static bool app_leaves_storage_free(void)
{
    extern uint8_t __flash_binary_end;
    uintptr_t end = (uintptr_t)&__flash_binary_end;
    if (end < XIP_BASE) return false;
    return (end - XIP_BASE) <= STORE_OFFSET_A;
}

static void __not_in_flash_func(flash_write_callback)(void *param)
{
    const flash_write_t *write = (const flash_write_t *)param;
    flash_range_erase(write->offset, FLASH_SECTOR_SIZE);
    flash_range_program(write->offset, write->data, FLASH_SECTOR_SIZE);
}

static void mark_dirty(uint32_t now_ms)
{
    if (!dirty) {
        dirty = true;
        dirty_time_valid = false;
    }
    last_change_ms = now_ms;
}

static bool utf8_sequence_length(const uint8_t *s, uint8_t remaining, uint8_t *length)
{
    uint8_t c = s[0];
    if (c < 0x80u) { *length = 1u; return true; }
    if (c >= 0xc2u && c <= 0xdfu) *length = 2u;
    else if (c >= 0xe0u && c <= 0xefu) *length = 3u;
    else if (c >= 0xf0u && c <= 0xf4u) *length = 4u;
    else return false;
    if (*length > remaining) return false;
    for (uint8_t i = 1u; i < *length; ++i) {
        if ((s[i] & 0xc0u) != 0x80u) return false;
    }
    if (c == 0xe0u && s[1] < 0xa0u) return false;
    if (c == 0xedu && s[1] >= 0xa0u) return false;
    if (c == 0xf0u && s[1] < 0x90u) return false;
    if (c == 0xf4u && s[1] >= 0x90u) return false;
    return true;
}

static void copy_clean_name(char out[ROUTE_STORE_NAME_MAX_BYTES + 1u], const char *name)
{
    memset(out, 0, ROUTE_STORE_NAME_MAX_BYTES + 1u);
    if (name == NULL) return;
    uint8_t used = 0;
    const uint8_t *input = (const uint8_t *)name;
    while (*input != 0u) {
        if (used >= ROUTE_STORE_NAME_MAX_BYTES) break;
        uint8_t length = 0;
        uint8_t remaining = 0;
        while (remaining < 4u && input[remaining] != 0u) ++remaining;
        if (!utf8_sequence_length(input, remaining, &length)) {
            uint8_t c = *input;
            out[used++] = (c < 0x20u || c == 0x7fu) ? ' ' : '?';
            ++input;
        } else if ((uint8_t)(used + length) <= ROUTE_STORE_NAME_MAX_BYTES) {
            if (length == 1u && (*input < 0x20u || *input == 0x7fu)) {
                out[used++] = ' ';
            } else {
                memcpy(out + used, input, length);
                used = (uint8_t)(used + length);
            }
            input += length;
        } else {
            break;
        }
    }
}

void route_store_init(void)
{
    memset(&active_policy, 0, sizeof(active_policy));
    memset(devices, 0, sizeof(devices));
    dirty = false;
    force_save = false;
    dirty_time_valid = false;
    retry_time_valid = false;
    active_generation = 0;
    active_slot = -1;
    storage_available = app_leaves_storage_free();
    last_error = "none";
    last_midi_time_valid = false;
    restart_pending = false;

    const store_record_t *a = slot_record(0);
    const store_record_t *b = slot_record(1);
    bool valid_a = record_valid(a);
    bool valid_b = record_valid(b);
    const store_record_t *selected = NULL;
    if (valid_a && valid_b) {
        active_slot = generation_is_newer(b->generation, a->generation) ? 1 : 0;
        selected = (active_slot == 0) ? a : b;
    } else if (valid_a) {
        active_slot = 0;
        selected = a;
    } else if (valid_b) {
        active_slot = 1;
        selected = b;
    }
    if (selected != NULL) {
        active_generation = selected->generation;
        active_policy = selected->policy;
        memcpy(devices, selected->devices, sizeof(devices));
    }
}

const route_policy_t *route_store_policy(void)
{
    return &active_policy;
}

bool route_store_save_policy(const route_policy_t *policy)
{
    last_error = "none";
    if (!route_policy_validate(policy)) {
        last_error = "policy_invalid";
        return false;
    }
    route_policy_t previous = active_policy;
    bool policy_changed = memcmp(&active_policy, policy, sizeof(active_policy)) != 0;
    if (!policy_changed && !dirty) return true;
    bool prior_dirty = dirty;
    uint32_t prior_dirty_since = dirty_since_ms;
    uint32_t prior_last_change = last_change_ms;
    bool prior_dirty_time_valid = dirty_time_valid;
    if (policy_changed) {
        active_policy = *policy;
        mark_dirty(to_ms_since_boot(get_absolute_time()));
    }
    force_save = true;
    if (!storage_available) {
        last_error = "storage_guard";
        active_policy = previous;
        dirty = prior_dirty;
        dirty_since_ms = prior_dirty_since;
        last_change_ms = prior_last_change;
        dirty_time_valid = prior_dirty_time_valid;
        force_save = false;
        return false;
    }
    /* Explicit Apply is acknowledged only after a verified durable commit. */
    uint32_t now_ms = to_ms_since_boot(get_absolute_time());
    bool was_dirty = dirty;
    route_store_task(now_ms, false);
    if (!dirty) return true;
    if (last_error == NULL || strcmp(last_error, "none") == 0) {
        last_error = "commit_failed";
    }
    active_policy = previous;
    dirty = was_dirty;
    if (!was_dirty) dirty_time_valid = false;
    else {
        dirty_since_ms = prior_dirty_since;
        last_change_ms = prior_last_change;
        dirty_time_valid = prior_dirty_time_valid;
    }
    force_save = false;
    return false;
}

uint8_t route_store_register(uint64_t key, const char *name)
{
    if (key == 0u) return 0u;
    char cleaned[ROUTE_STORE_NAME_MAX_BYTES + 1u];
    copy_clean_name(cleaned, name);
    uint8_t free_slot = UINT8_MAX;
    for (uint8_t i = 0; i < STORE_SLOT_COUNT; ++i) {
        if (devices[i].key == key) {
            /* A later failed string fetch must not replace a known product name. */
            if (cleaned[0] != '\0' && strcmp(devices[i].name, cleaned) != 0) {
                memcpy(devices[i].name, cleaned, sizeof(cleaned));
                mark_dirty(to_ms_since_boot(get_absolute_time()));
            }
            return devices[i].id;
        }
        if (devices[i].key == 0u && free_slot == UINT8_MAX) free_slot = i;
    }
    if (free_slot == UINT8_MAX || !storage_available) return 0u;
    bool used[ROUTE_STORE_MAX_ID + 1u] = {false};
    for (uint8_t i = 0; i < STORE_SLOT_COUNT; ++i) {
        if (devices[i].id <= ROUTE_STORE_MAX_ID) used[devices[i].id] = true;
    }
    uint8_t id = 0;
    for (uint8_t candidate = STORE_FIRST_ID; candidate <= ROUTE_STORE_MAX_ID; ++candidate) {
        if (!used[candidate]) { id = candidate; break; }
    }
    if (id == 0u) return 0u;
    devices[free_slot].key = key;
    devices[free_slot].id = id;
    if (cleaned[0] == '\0') {
        memcpy(cleaned, "USB MIDI", sizeof("USB MIDI"));
    }
    memcpy(devices[free_slot].name, cleaned, sizeof(cleaned));
    mark_dirty(to_ms_since_boot(get_absolute_time()));
    return id;
}

const char *route_store_device_name(uint8_t id)
{
    switch (id) {
    case 0: return "Any / unknown";
    case 1: return "Native USB MIDI";
    case 2: return "CDC MIDI";
    default: break;
    }
    if (id < STORE_FIRST_ID || id > ROUTE_STORE_MAX_ID) return NULL;
    for (uint8_t i = 0; i < STORE_SLOT_COUNT; ++i) {
        if (devices[i].id == id && devices[i].key != 0u) return devices[i].name;
    }
    return NULL;
}

void route_store_task(uint32_t now_ms, bool midi_active)
{
    if (midi_active) {
        last_midi_ms = now_ms;
        last_midi_time_valid = true;
    }
    if (!dirty) return;
    if (!dirty_time_valid) {
        dirty_since_ms = now_ms;
        dirty_time_valid = true;
    }
    if (!force_save && retry_time_valid &&
        (int32_t)(now_ms - retry_after_ms) < 0) return;
    bool max_delay = (uint32_t)(now_ms - dirty_since_ms) >= STORE_SAVE_MAX_DELAY_MS;
    uint32_t quiet_since_ms = last_change_ms;
    if (last_midi_time_valid &&
        (int32_t)(last_midi_ms - quiet_since_ms) > 0) {
        quiet_since_ms = last_midi_ms;
    }
    bool quiet = !midi_active &&
                 (uint32_t)(now_ms - quiet_since_ms) >= STORE_SAVE_QUIET_MS;
    if (!force_save && !quiet && !max_delay) return;
    if (!storage_available) return;

    memset(write_sector, 0xff, sizeof(write_sector));
    store_record_t record;
    memset(&record, 0, sizeof(record));
    record.magic = STORE_MAGIC;
    record.version = STORE_VERSION;
    record.payload_size = sizeof(record);
    record.generation = active_generation + 1u;
    record.policy = active_policy;
    memcpy(record.devices, devices, sizeof(devices));
    record.crc32 = 0;
    record.crc32 = crc32_record((const uint8_t *)&record, sizeof(record));
    memcpy(write_sector, &record, sizeof(record));

    uint8_t target_slot = (active_slot == 0) ? 1u : 0u;
    flash_write_t write = {
        .offset = target_slot == 0u ? STORE_OFFSET_A : STORE_OFFSET_B,
        .data = write_sector
    };
    bool pio_connected = usb_bridge_pause_pio_host();
    int result = flash_safe_execute(flash_write_callback, &write, 1000u);
    const store_record_t *written = slot_record(target_slot);
    if (result == 0 && record_valid(written) &&
        memcmp(written, &record, sizeof(record)) == 0) {
        active_slot = (int8_t)target_slot;
        active_generation = record.generation;
        dirty = false;
        force_save = false;
        dirty_time_valid = false;
        retry_time_valid = false;
        last_error = "none";
        if (pio_connected && !restart_pending) {
            restart_deadline_ms = to_ms_since_boot(get_absolute_time()) + 350u;
            restart_pending = true;
        } else {
            usb_bridge_resume_pio_host();
        }
    } else {
        usb_bridge_resume_pio_host();
        last_error = (result != 0) ? "flash_safe_execute" : "readback_verify";
        retry_after_ms = now_ms + STORE_RETRY_MS;
        retry_time_valid = true;
    }
}

const char *route_store_last_error(void)
{
    return last_error;
}

bool route_store_restart_pending(void)
{
    return restart_pending;
}

void route_store_service_restart(uint32_t now_ms)
{
    if (restart_pending && (int32_t)(now_ms - restart_deadline_ms) >= 0) {
        watchdog_reboot(0, 0, 0);
    }
}

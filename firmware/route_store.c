#include "route_store.h"

#include <stddef.h>
#include <string.h>

#include "hardware/flash.h"
#include "hardware/sync.h"
#include "hardware/watchdog.h"
#include "led_config.h"
#include "pico/flash.h"
#include "pico/stdlib.h"
#include "third_party/littlefs/lfs.h"
#include "usb_bridge.h"

#define STORE_MAGIC UINT32_C(0x52505354)
#define STORE_VERSION 1u
#define SETTINGS_MAGIC UINT32_C(0x52505332)
#define SETTINGS_VERSION 1u
#define STORE_FIRST_ID 3u
#define STORE_SLOT_COUNT (ROUTE_STORE_MAX_ID - STORE_FIRST_ID + 1u)
#define STORE_SAVE_QUIET_MS 2000u
#define STORE_SAVE_MAX_DELAY_MS 12000u
#define STORE_RETRY_MS 5000u

/* Keep the legacy dual-slot records intact for first-boot migration. */
#define LEGACY_OFFSET_A (PICO_FLASH_SIZE_BYTES - 2u * FLASH_SECTOR_SIZE)
#define LEGACY_OFFSET_B (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)
#define SETTINGS_RESERVED_SECTORS 8u
#define FORMAT_MARKER_OFFSET (PICO_FLASH_SIZE_BYTES - (SETTINGS_RESERVED_SECTORS + 2u) * FLASH_SECTOR_SIZE)
#define SETTINGS_FS_OFFSET (FORMAT_MARKER_OFFSET + FLASH_SECTOR_SIZE)
#define SETTINGS_FS_BLOCK_COUNT (SETTINGS_RESERVED_SECTORS - 1u)
#define SETTINGS_FILE "/settings"
#define SETTINGS_TEMP_FILE "/settings.tmp"
#define FORMAT_MARKER_MAGIC UINT32_C(0x4c465346)

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
} legacy_record_v1_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t payload_size;
    uint32_t generation;
    uint32_t crc32;
    route_policy_t policy;
    store_device_t devices[STORE_SLOT_COUNT];
    led_config_t led;
} settings_record_t;

typedef struct {
    uint32_t offset;
    const uint8_t *data;
    uint32_t size;
    bool erase;
} flash_operation_t;

typedef struct {
    uint32_t magic;
    uint32_t inverse;
} format_marker_t;

static route_policy_t active_policy;
static store_device_t devices[STORE_SLOT_COUNT];
static led_config_t active_led;
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
static bool storage_available;
static bool filesystem_mounted;
static bool format_marker_pending;
static bool last_midi_time_valid;
static bool restart_pending;
static _Alignas(4) uint8_t littlefs_read_cache[256];
static _Alignas(4) uint8_t littlefs_program_cache[256];
static _Alignas(4) uint8_t littlefs_lookahead[16];
static _Alignas(4) uint8_t littlefs_file_cache[256];
static lfs_t settings_fs;
static struct lfs_config settings_fs_config;
static struct lfs_file_config settings_file_config;
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

static bool app_leaves_storage_free(void)
{
    extern uint8_t __flash_binary_end;
    uintptr_t end = (uintptr_t)&__flash_binary_end;
    return end >= XIP_BASE &&
           (end - XIP_BASE) <= FORMAT_MARKER_OFFSET;
}

static void __not_in_flash_func(flash_operation_callback)(void *parameter)
{
    const flash_operation_t *operation = (const flash_operation_t *)parameter;
    if (operation->erase) {
        flash_range_erase(operation->offset, operation->size);
    } else {
        flash_range_program(operation->offset, operation->data, operation->size);
    }
}

static int safe_flash_operation(uint32_t offset, const void *data, uint32_t size,
                                bool erase)
{
    flash_operation_t operation = {
        .offset = offset,
        .data = (const uint8_t *)data,
        .size = size,
        .erase = erase,
    };
    return flash_safe_execute(flash_operation_callback, &operation, 1000u);
}

static int littlefs_read(const struct lfs_config *config, lfs_block_t block,
                         lfs_off_t offset, void *buffer, lfs_size_t size)
{
    (void)config;
    if (block >= SETTINGS_FS_BLOCK_COUNT || offset > FLASH_SECTOR_SIZE ||
        size > FLASH_SECTOR_SIZE - offset) return LFS_ERR_CORRUPT;
    uint32_t address = SETTINGS_FS_OFFSET + block * FLASH_SECTOR_SIZE + offset;
    memcpy(buffer, (const void *)(uintptr_t)(XIP_BASE + address), size);
    return 0;
}

static int littlefs_program(const struct lfs_config *config, lfs_block_t block,
                            lfs_off_t offset, const void *buffer, lfs_size_t size)
{
    (void)config;
    if (block >= SETTINGS_FS_BLOCK_COUNT || offset > FLASH_SECTOR_SIZE ||
        size > FLASH_SECTOR_SIZE - offset || offset % FLASH_PAGE_SIZE != 0u ||
        size == 0u || size % FLASH_PAGE_SIZE != 0u) return LFS_ERR_CORRUPT;
    uint32_t address = SETTINGS_FS_OFFSET + block * FLASH_SECTOR_SIZE + offset;
    return safe_flash_operation(address, buffer, size, false) == 0 ? 0 : LFS_ERR_IO;
}

static int littlefs_erase(const struct lfs_config *config, lfs_block_t block)
{
    (void)config;
    if (block >= SETTINGS_FS_BLOCK_COUNT) return LFS_ERR_CORRUPT;
    uint32_t address = SETTINGS_FS_OFFSET + block * FLASH_SECTOR_SIZE;
    return safe_flash_operation(address, NULL, FLASH_SECTOR_SIZE, true) == 0 ? 0 : LFS_ERR_IO;
}

static int littlefs_sync(const struct lfs_config *config)
{
    (void)config;
    return 0;
}

static void configure_filesystem(void)
{
    memset(&settings_fs_config, 0, sizeof(settings_fs_config));
    settings_fs_config.read = littlefs_read;
    settings_fs_config.prog = littlefs_program;
    settings_fs_config.erase = littlefs_erase;
    settings_fs_config.sync = littlefs_sync;
    settings_fs_config.read_size = 1u;
    settings_fs_config.prog_size = FLASH_PAGE_SIZE;
    settings_fs_config.block_size = FLASH_SECTOR_SIZE;
    settings_fs_config.block_count = SETTINGS_FS_BLOCK_COUNT;
    settings_fs_config.block_cycles = 100u;
    settings_fs_config.cache_size = sizeof(littlefs_read_cache);
    settings_fs_config.lookahead_size = sizeof(littlefs_lookahead);
    settings_fs_config.read_buffer = littlefs_read_cache;
    settings_fs_config.prog_buffer = littlefs_program_cache;
    settings_fs_config.lookahead_buffer = littlefs_lookahead;
    memset(&settings_file_config, 0, sizeof(settings_file_config));
    settings_file_config.buffer = littlefs_file_cache;
}

static bool flash_range_is_erased(uint32_t offset, uint32_t length)
{
    const volatile uint8_t *bytes =
        (const volatile uint8_t *)(uintptr_t)(XIP_BASE + offset);
    for (uint32_t i = 0; i < length; ++i) {
        if (bytes[i] != 0xffu) return false;
    }
    return true;
}

static bool settings_area_is_erased(void)
{
    return flash_range_is_erased(SETTINGS_FS_OFFSET,
                                 SETTINGS_FS_BLOCK_COUNT * FLASH_SECTOR_SIZE);
}

static bool marker_valid(void)
{
    const format_marker_t *marker = (const format_marker_t *)(uintptr_t)
        (XIP_BASE + FORMAT_MARKER_OFFSET);
    return marker->magic == FORMAT_MARKER_MAGIC &&
           marker->inverse == ~FORMAT_MARKER_MAGIC;
}

static bool write_format_marker(void)
{
    _Alignas(4) uint8_t page[FLASH_PAGE_SIZE];
    memset(page, 0xff, sizeof(page));
    format_marker_t marker = {FORMAT_MARKER_MAGIC, ~FORMAT_MARKER_MAGIC};
    memcpy(page, &marker, sizeof(marker));
    return safe_flash_operation(FORMAT_MARKER_OFFSET, NULL,
                                FLASH_SECTOR_SIZE, true) == 0 &&
           safe_flash_operation(FORMAT_MARKER_OFFSET, page,
                                FLASH_PAGE_SIZE, false) == 0 && marker_valid();
}

static bool clear_format_marker(void)
{
    return safe_flash_operation(FORMAT_MARKER_OFFSET, NULL,
                                FLASH_SECTOR_SIZE, true) == 0 &&
           flash_range_is_erased(FORMAT_MARKER_OFFSET, FLASH_SECTOR_SIZE);
}

static bool device_catalog_valid(const store_device_t *catalog)
{
    bool ids[ROUTE_STORE_MAX_ID + 1u] = {false};
    for (uint8_t i = 0; i < STORE_SLOT_COUNT; ++i) {
        const store_device_t *device = &catalog[i];
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
            if (catalog[j].key == device->key) return false;
        }
    }
    return true;
}

static bool legacy_record_valid(const legacy_record_v1_t *record)
{
    if (record->magic != STORE_MAGIC || record->version != STORE_VERSION ||
        record->payload_size != sizeof(*record) ||
        !route_policy_validate(&record->policy) ||
        !device_catalog_valid(record->devices)) return false;
    legacy_record_v1_t copy;
    memcpy(&copy, record, sizeof(copy));
    uint32_t expected = copy.crc32;
    copy.crc32 = 0;
    return crc32_record((const uint8_t *)&copy, sizeof(copy)) == expected;
}

static bool settings_record_valid(const settings_record_t *record)
{
    if (record->magic != SETTINGS_MAGIC || record->version != SETTINGS_VERSION ||
        record->payload_size != sizeof(*record) ||
        !route_policy_validate(&record->policy) ||
        !device_catalog_valid(record->devices) || !led_config_validate(&record->led)) {
        return false;
    }
    settings_record_t copy;
    memcpy(&copy, record, sizeof(copy));
    uint32_t expected = copy.crc32;
    copy.crc32 = 0;
    return crc32_record((const uint8_t *)&copy, sizeof(copy)) == expected;
}

static bool led_configs_equal(const led_config_t *left, const led_config_t *right)
{
    return memcmp(left->rgb, right->rgb, sizeof(left->rgb)) == 0 &&
           left->midi_pulse_ms == right->midi_pulse_ms &&
           left->mouse_phase_ms == right->mouse_phase_ms &&
           left->mouse_hold_ms == right->mouse_hold_ms;
}

static void led_config_copy(led_config_t *destination, const led_config_t *source)
{
    memcpy(destination->rgb, source->rgb, sizeof(destination->rgb));
    destination->midi_pulse_ms = source->midi_pulse_ms;
    destination->mouse_phase_ms = source->mouse_phase_ms;
    destination->mouse_hold_ms = source->mouse_hold_ms;
}

static const legacy_record_v1_t *legacy_slot(uint8_t slot)
{
    uint32_t offset = slot == 0u ? LEGACY_OFFSET_A : LEGACY_OFFSET_B;
    return (const legacy_record_v1_t *)(uintptr_t)(XIP_BASE + offset);
}

static const legacy_record_v1_t *select_legacy_record(uint32_t *generation)
{
    const legacy_record_v1_t *a = legacy_slot(0u);
    const legacy_record_v1_t *b = legacy_slot(1u);
    bool valid_a = legacy_record_valid(a);
    bool valid_b = legacy_record_valid(b);
    if (valid_a && valid_b) {
        const legacy_record_v1_t *selected = generation_is_newer(b->generation, a->generation) ? b : a;
        if (generation != NULL) *generation = selected->generation;
        return selected;
    }
    if (valid_a || valid_b) {
        const legacy_record_v1_t *selected = valid_a ? a : b;
        if (generation != NULL) *generation = selected->generation;
        return selected;
    }
    return NULL;
}

static int read_record_file(const char *path, settings_record_t *record)
{
    struct lfs_info info;
    int error = lfs_stat(&settings_fs, path, &info);
    if (error != 0) return error;
    if (info.type != LFS_TYPE_REG || info.size != sizeof(*record)) return LFS_ERR_CORRUPT;
    lfs_file_t file;
    error = lfs_file_opencfg(&settings_fs, &file, path, LFS_O_RDONLY,
                             &settings_file_config);
    if (error != 0) return error;
    lfs_ssize_t total = 0;
    while (total < (lfs_ssize_t)sizeof(*record)) {
        lfs_ssize_t count = lfs_file_read(&settings_fs, &file,
            (uint8_t *)record + total, sizeof(*record) - (lfs_size_t)total);
        if (count <= 0) { error = count < 0 ? (int)count : LFS_ERR_CORRUPT; break; }
        total += count;
    }
    if (error == 0 && total != (lfs_ssize_t)sizeof(*record)) error = LFS_ERR_CORRUPT;
    int close_error = lfs_file_close(&settings_fs, &file);
    if (error == 0 && close_error != 0) error = close_error;
    if (error == 0 && !settings_record_valid(record)) error = LFS_ERR_CORRUPT;
    return error;
}

static bool write_record_file(const char *path, const settings_record_t *record)
{
    lfs_file_t file;
    int error = lfs_file_opencfg(&settings_fs, &file, path,
                                 LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC,
                                 &settings_file_config);
    if (error != 0) return false;
    lfs_ssize_t written = lfs_file_write(&settings_fs, &file, record, sizeof(*record));
    if (written != (lfs_ssize_t)sizeof(*record)) error = written < 0 ? (int)written : LFS_ERR_IO;
    if (error == 0) error = lfs_file_sync(&settings_fs, &file);
    int close_error = lfs_file_close(&settings_fs, &file);
    if (error == 0 && close_error != 0) error = close_error;
    return error == 0;
}

static bool persist_settings(void)
{
    if (!storage_available || !filesystem_mounted) {
        last_error = "storage_unavailable";
        return false;
    }
    settings_record_t record;
    memset(&record, 0, sizeof(record));
    record.magic = SETTINGS_MAGIC;
    record.version = SETTINGS_VERSION;
    record.payload_size = sizeof(record);
    record.generation = active_generation + 1u;
    record.policy = active_policy;
    memcpy(record.devices, devices, sizeof(devices));
    led_config_copy(&record.led, &active_led);
    record.crc32 = 0;
    record.crc32 = crc32_record((const uint8_t *)&record, sizeof(record));

    if (!write_record_file(SETTINGS_TEMP_FILE, &record)) {
        last_error = "settings_write";
        return false;
    }
    settings_record_t verify;
    if (read_record_file(SETTINGS_TEMP_FILE, &verify) != 0 ||
        memcmp(&verify, &record, sizeof(record)) != 0) {
        last_error = "settings_temp_verify";
        return false;
    }
    int rename_error = lfs_rename(&settings_fs, SETTINGS_TEMP_FILE, SETTINGS_FILE);
    if (rename_error != 0) {
        last_error = "settings_rename";
        return false;
    }
    if (read_record_file(SETTINGS_FILE, &verify) != 0 ||
        memcmp(&verify, &record, sizeof(record)) != 0) {
        last_error = "settings_readback";
        return false;
    }
    if (format_marker_pending) {
        if (!clear_format_marker()) {
            last_error = "format_marker_clear";
            return false;
        }
        format_marker_pending = false;
    }
    active_generation = record.generation;
    last_error = "none";
    return true;
}

static void mark_dirty(uint32_t now_ms)
{
    if (!dirty) dirty_time_valid = false;
    dirty = true;
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
    while (*input != 0u && used < ROUTE_STORE_NAME_MAX_BYTES) {
        uint8_t length = 0;
        uint8_t remaining = 0;
        while (remaining < 4u && input[remaining] != 0u) ++remaining;
        if (!utf8_sequence_length(input, remaining, &length)) {
            uint8_t c = *input;
            out[used++] = (c < 0x20u || c == 0x7fu) ? ' ' : '?';
            ++input;
        } else if ((uint8_t)(used + length) <= ROUTE_STORE_NAME_MAX_BYTES) {
            if (length == 1u && (*input < 0x20u || *input == 0x7fu)) out[used++] = ' ';
            else { memcpy(out + used, input, length); used = (uint8_t)(used + length); }
            input += length;
        } else break;
    }
}

static void load_legacy(const legacy_record_v1_t *legacy)
{
    if (legacy == NULL) return;
    active_policy = legacy->policy;
    memcpy(devices, legacy->devices, sizeof(devices));
    dirty = true;
    dirty_time_valid = false;
    last_change_ms = 0u;
}

static bool format_and_mount(void)
{
    if (!write_format_marker()) {
        last_error = "format_marker";
        return false;
    }
    int error = lfs_format(&settings_fs, &settings_fs_config);
    if (error != 0) {
        last_error = "storage_format";
        return false;
    }
    error = lfs_mount(&settings_fs, &settings_fs_config);
    if (error != 0) {
        last_error = "storage_mount";
        return false;
    }
    filesystem_mounted = true;
    format_marker_pending = true;
    return true;
}

void route_store_init(void)
{
    memset(&active_policy, 0, sizeof(active_policy));
    memset(devices, 0, sizeof(devices));
    led_config_default(&active_led);
    dirty = false;
    force_save = false;
    dirty_time_valid = false;
    retry_time_valid = false;
    active_generation = 0u;
    storage_available = app_leaves_storage_free();
    filesystem_mounted = false;
    format_marker_pending = false;
    last_error = "none";
    last_midi_time_valid = false;
    restart_pending = false;
    restart_deadline_ms = 0u;

    uint32_t legacy_generation = 0u;
    const legacy_record_v1_t *legacy = select_legacy_record(&legacy_generation);
    if (!storage_available) {
        last_error = "storage_guard";
        load_legacy(legacy);
        return;
    }

    configure_filesystem();
    int error = lfs_mount(&settings_fs, &settings_fs_config);
    if (error != 0) {
        bool erased_fs = settings_area_is_erased();
        bool interrupted_format = marker_valid();
        if (erased_fs || interrupted_format) {
            if (!format_and_mount()) {
                storage_available = false;
                load_legacy(legacy);
                return;
            }
        } else {
            storage_available = false;
            last_error = "storage_corrupt";
            load_legacy(legacy);
            return;
        }
    } else {
        filesystem_mounted = true;
        /* A persistent intent marker means format/mount was interrupted. */
        format_marker_pending = marker_valid();
    }

    settings_record_t record;
    int file_error = read_record_file(SETTINGS_FILE, &record);
    if (file_error == 0) {
        active_generation = record.generation;
        active_policy = record.policy;
        memcpy(devices, record.devices, sizeof(devices));
        led_config_copy(&active_led, &record.led);
        if (format_marker_pending) {
            if (!clear_format_marker()) {
                storage_available = false;
                last_error = "format_marker_clear";
            } else {
                format_marker_pending = false;
            }
        }
    } else if (file_error == LFS_ERR_NOENT) {
        if (format_marker_pending) {
            load_legacy(legacy);
            if (legacy != NULL) active_generation = legacy_generation;
            else { dirty = true; last_change_ms = 0u; }
            if (last_error[0] == 'n') last_error = "migration_pending";
        } else {
            storage_available = false;
            last_error = "settings_missing";
            load_legacy(legacy);
        }
    } else {
        if (format_marker_pending && clear_format_marker()) {
            format_marker_pending = false;
        }
        storage_available = false;
        last_error = "settings_corrupt";
        load_legacy(legacy);
    }
}

const route_policy_t *route_store_policy(void)
{
    return &active_policy;
}

const led_config_t *route_store_led(void)
{
    return &active_led;
}

static bool save_now(void)
{
    bool pio_connected = usb_bridge_pause_pio_host();
    bool success = persist_settings();
    if (success) {
        dirty = false;
        force_save = false;
        dirty_time_valid = false;
        retry_time_valid = false;
        if (pio_connected && !restart_pending) {
            restart_deadline_ms = to_ms_since_boot(get_absolute_time()) + 350u;
            restart_pending = true;
        } else if (!pio_connected) {
            usb_bridge_resume_pio_host();
        }
    } else if (!restart_pending) {
        usb_bridge_resume_pio_host();
    }
    return success;
}

static bool commit_explicit(void)
{
    if (!storage_available || !filesystem_mounted) {
        last_error = "storage_unavailable";
        return false;
    }
    force_save = true;
    bool success = save_now() && !dirty;
    if (!success) {
        force_save = false;
        retry_after_ms = to_ms_since_boot(get_absolute_time()) + STORE_RETRY_MS;
        retry_time_valid = true;
    }
    return success;
}

bool route_store_save_policy(const route_policy_t *policy)
{
    if (!route_policy_validate(policy)) {
        last_error = "policy_invalid";
        return false;
    }
    route_policy_t previous = active_policy;
    bool was_dirty = dirty;
    bool was_dirty_time_valid = dirty_time_valid;
    uint32_t previous_dirty_since = dirty_since_ms;
    uint32_t previous_last_change = last_change_ms;
    if (memcmp(&active_policy, policy, sizeof(active_policy)) == 0 && !dirty) {
        if (storage_available && filesystem_mounted) return true;
        last_error = "storage_unavailable";
        return false;
    }
    if (memcmp(&active_policy, policy, sizeof(active_policy)) != 0) {
        active_policy = *policy;
        mark_dirty(to_ms_since_boot(get_absolute_time()));
    }
    if (commit_explicit()) return true;
    active_policy = previous;
    dirty = was_dirty;
    dirty_time_valid = was_dirty_time_valid;
    dirty_since_ms = previous_dirty_since;
    last_change_ms = previous_last_change;
    force_save = false;
    return false;
}

bool route_store_save_led(const led_config_t *config)
{
    if (!led_config_validate(config)) {
        last_error = "led_invalid";
        return false;
    }
    led_config_t previous = active_led;
    bool was_dirty = dirty;
    bool was_dirty_time_valid = dirty_time_valid;
    uint32_t previous_dirty_since = dirty_since_ms;
    uint32_t previous_last_change = last_change_ms;
    if (led_configs_equal(&active_led, config) && !dirty) {
        if (storage_available && filesystem_mounted) return true;
        last_error = "storage_unavailable";
        return false;
    }
    if (!led_configs_equal(&active_led, config)) {
        led_config_copy(&active_led, config);
        mark_dirty(to_ms_since_boot(get_absolute_time()));
    }
    if (commit_explicit()) return true;
    active_led = previous;
    dirty = was_dirty;
    dirty_time_valid = was_dirty_time_valid;
    dirty_since_ms = previous_dirty_since;
    last_change_ms = previous_last_change;
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
    uint8_t id = 0u;
    for (uint8_t candidate = STORE_FIRST_ID; candidate <= ROUTE_STORE_MAX_ID; ++candidate) {
        if (!used[candidate]) { id = candidate; break; }
    }
    if (id == 0u) return 0u;
    devices[free_slot].key = key;
    devices[free_slot].id = id;
    if (cleaned[0] == '\0') memcpy(cleaned, "USB MIDI", sizeof("USB MIDI"));
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
    if (last_midi_time_valid && (int32_t)(last_midi_ms - quiet_since_ms) > 0) {
        quiet_since_ms = last_midi_ms;
    }
    bool quiet = !midi_active &&
                 (uint32_t)(now_ms - quiet_since_ms) >= STORE_SAVE_QUIET_MS;
    if (!force_save && !quiet && !max_delay) return;
    if (!storage_available || !filesystem_mounted) return;
    if (!save_now()) {
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

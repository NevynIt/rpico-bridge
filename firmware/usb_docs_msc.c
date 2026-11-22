#include <string.h>

#include "tusb.h"

#if defined(RPICO_DOCS_DRIVE)

#define DOCS_SECTOR_SIZE 512u

extern const uint8_t rpico_docs_image[];
extern const uint8_t rpico_docs_image_end[];

static uint32_t docs_image_size(void)
{
    return (uint32_t)((uintptr_t)rpico_docs_image_end - (uintptr_t)rpico_docs_image);
}

void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor_id[8], uint8_t product_id[16],
                        uint8_t product_rev[4])
{
    (void)lun;
    memset(vendor_id, ' ', 8u);
    memset(product_id, ' ', 16u);
    memset(product_rev, ' ', 4u);
    memcpy(vendor_id, "RPICO", 5u);
    memcpy(product_id, "Bridge Documents", 16u);
    memcpy(product_rev, "1.00", 4u);
}

bool tud_msc_test_unit_ready_cb(uint8_t lun)
{
    return lun == 0u && docs_image_size() != 0u &&
           docs_image_size() % DOCS_SECTOR_SIZE == 0u;
}

void tud_msc_capacity_cb(uint8_t lun, uint32_t *block_count, uint16_t *block_size)
{
    if (lun != 0u) {
        *block_count = 0u;
        *block_size = DOCS_SECTOR_SIZE;
        return;
    }
    *block_count = docs_image_size() / DOCS_SECTOR_SIZE;
    *block_size = DOCS_SECTOR_SIZE;
}

int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset,
                          void *buffer, uint32_t bufsize)
{
    uint32_t image_size = docs_image_size();
    if (lun != 0u || buffer == NULL || bufsize == 0u ||
        offset >= DOCS_SECTOR_SIZE || bufsize > DOCS_SECTOR_SIZE - offset) {
        (void)tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x24u, 0x00u);
        return TUD_MSC_RET_ERROR;
    }
    uint64_t address = (uint64_t)lba * DOCS_SECTOR_SIZE + offset;
    if (address + bufsize > image_size) {
        (void)tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x21u, 0x00u);
        return TUD_MSC_RET_ERROR;
    }
    memcpy(buffer, rpico_docs_image + (size_t)address, bufsize);
    return (int32_t)bufsize;
}

int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset,
                           uint8_t *buffer, uint32_t bufsize)
{
    (void)lba;
    (void)offset;
    (void)buffer;
    (void)bufsize;
    (void)tud_msc_set_sense(lun, SCSI_SENSE_DATA_PROTECT, 0x27u, 0x00u);
    return TUD_MSC_RET_ERROR;
}

int32_t tud_msc_scsi_cb(uint8_t lun, uint8_t const scsi_cmd[16],
                        void *buffer, uint16_t bufsize)
{
    (void)scsi_cmd;
    (void)buffer;
    (void)bufsize;
    (void)tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20u, 0x00u);
    return TUD_MSC_RET_ERROR;
}

bool tud_msc_is_writable_cb(uint8_t lun)
{
    (void)lun;
    return false;
}

bool tud_msc_start_stop_cb(uint8_t lun, uint8_t power_condition, bool start,
                           bool load_eject)
{
    (void)power_condition;
    (void)start;
    (void)load_eject;
    return lun == 0u;
}

bool tud_msc_prevent_allow_medium_removal_cb(uint8_t lun, uint8_t prohibit_removal,
                                             uint8_t control)
{
    (void)prohibit_removal;
    (void)control;
    return lun == 0u;
}

#endif

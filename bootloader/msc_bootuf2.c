/*
 * Copyright (c) 2024, sakumisu
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Adapted from CherryDAP/projects/HSLink-Pro/bootloader/msc_bootuf2.c for the
 * hpm bootloader. Flash access goes through the FAL partition API
 * instead of direct ROM API calls.
 */
#include "bootuf2.h"
#include "usbd_core.h"
#include "usbd_msc.h"

#include <fal.h>
#include <hpm_clock_drv.h>
#include <hpm_romapi.h>
#include <hpm_soc_feature.h>
#include <hpm_usb_drv.h>
#include "bootuf2_config.h"

#define OTP_CHIP_UUID_IDX_START (88U)
#define OTP_CHIP_UUID_IDX_END   (91U)

#define MSC_IN_EP  0x81
#define MSC_OUT_EP 0x02

#define USBD_LANGID_STRING 1033

#define USB_CONFIG_SIZE (9 + MSC_DESCRIPTOR_LEN)

#ifdef CONFIG_USB_HS
#define MSC_MAX_MPS 512
#else
#define MSC_MAX_MPS 64
#endif

/* Descriptor layout: device(18) + config(9) + MSC(23) + langid(4) +
 * string1(16) + string2(16) + string3(66) + [device qualifier(10)].
 * String3 holds a 32-character chip id; its character data starts at index
 * 88 (2 bytes of bLength/bDescriptorType are skipped).
 */
#define STRING3_CHAR_INDEX 88
static uint8_t msc_bootuf2_descriptor[] = {
    USB_DEVICE_DESCRIPTOR_INIT(USB_2_0, 0x00, 0x00, 0x00, USBD_VID, USBD_PID, 0x0200, 0x01),
    USB_CONFIG_DESCRIPTOR_INIT(USB_CONFIG_SIZE, 0x01, 0x01, USB_CONFIG_BUS_POWERED, USBD_MAX_POWER),
    MSC_DESCRIPTOR_INIT(0x00, MSC_OUT_EP, MSC_IN_EP, MSC_MAX_MPS, 0x02),
    ///////////////////////////////////////
    /// string0 descriptor
    ///////////////////////////////////////
    USB_LANGID_INIT(USBD_LANGID_STRING),
    ///////////////////////////////////////
    /// string1 descriptor
    ///////////////////////////////////////
    0x10,                       /* bLength */
    USB_DESCRIPTOR_TYPE_STRING, /* bDescriptorType */
    'H', 0x00,                  /* wcChar0 */
    'P', 0x00,                  /* wcChar1 */
    'M', 0x00,                  /* wcChar2 */
    ' ', 0x00,                  /* wcChar3 */
    'U', 0x00,                  /* wcChar4 */
    'S', 0x00,                  /* wcChar5 */
    'B', 0x00,                  /* wcChar6 */
    ///////////////////////////////////////
    /// string2 descriptor
    ///////////////////////////////////////
    0x10,                       /* bLength */
    USB_DESCRIPTOR_TYPE_STRING, /* bDescriptorType */
    'H', 0x00,                  /* wcChar0 */
    'P', 0x00,                  /* wcChar1 */
    'M', 0x00,                  /* wcChar2 */
    ' ', 0x00,                  /* wcChar3 */
    'U', 0x00,                  /* wcChar4 */
    'F', 0x00,                  /* wcChar5 */
    '2', 0x00,                  /* wcChar6 */
    ///////////////////////////////////////
    /// string3 descriptor (32-digit chip id, patched at init)
    ///////////////////////////////////////
    0x42,                       /* bLength */
    USB_DESCRIPTOR_TYPE_STRING, /* bDescriptorType */
    '0', 0x00,                  /* wcChar0 */
    '0', 0x00,                  /* wcChar1 */
    '0', 0x00,                  /* wcChar2 */
    '0', 0x00,                  /* wcChar3 */
    '0', 0x00,                  /* wcChar4 */
    '0', 0x00,                  /* wcChar5 */
    '0', 0x00,                  /* wcChar6 */
    '0', 0x00,                  /* wcChar7 */
    '0', 0x00,                  /* wcChar8 */
    '0', 0x00,                  /* wcChar9 */
    '0', 0x00,                  /* wcChar10 */
    '0', 0x00,                  /* wcChar11 */
    '0', 0x00,                  /* wcChar12 */
    '0', 0x00,                  /* wcChar13 */
    '0', 0x00,                  /* wcChar14 */
    '0', 0x00,                  /* wcChar15 */
    '0', 0x00,                  /* wcChar16 */
    '0', 0x00,                  /* wcChar17 */
    '0', 0x00,                  /* wcChar18 */
    '0', 0x00,                  /* wcChar19 */
    '0', 0x00,                  /* wcChar20 */
    '0', 0x00,                  /* wcChar21 */
    '0', 0x00,                  /* wcChar22 */
    '0', 0x00,                  /* wcChar23 */
    '0', 0x00,                  /* wcChar24 */
    '0', 0x00,                  /* wcChar25 */
    '0', 0x00,                  /* wcChar26 */
    '0', 0x00,                  /* wcChar27 */
    '0', 0x00,                  /* wcChar28 */
    '0', 0x00,                  /* wcChar29 */
    '0', 0x00,                  /* wcChar30 */
    '0', 0x00,                  /* wcChar31 */
#ifdef CONFIG_USB_HS
    ///////////////////////////////////////
    /// device qualifier descriptor
    ///////////////////////////////////////
    0x0a,
    USB_DESCRIPTOR_TYPE_DEVICE_QUALIFIER,
    0x00,
    0x02,
    0x00,
    0x00,
    0x00,
    0x40,
    0x01,
    0x00,
#endif
    0x00};

/**
 * @brief Patch the chip id (string3 descriptor) from OTP shadow before USB
 *        enumeration.
 */
_Static_assert(STRING3_CHAR_INDEX + 32 * 2 <= sizeof(msc_bootuf2_descriptor),
               "string3 chip id out of descriptor bounds");
static void modify_descriptor_chip_id(void)
{
    uint32_t uuid_words[4];

    uint32_t word_idx = 0;
    for (uint32_t i = OTP_CHIP_UUID_IDX_START; i <= OTP_CHIP_UUID_IDX_END; i++)
    {
        uuid_words[word_idx++] = ROM_API_TABLE_ROOT->otp_driver_if->read_from_shadow(i);
    }

    char chip_id[32];
    sprintf(chip_id, "%08X%08X%08X%08X", uuid_words[0], uuid_words[1], uuid_words[2], uuid_words[3]);
    for (size_t i = 0; i < 32; i++)
    {
        msc_bootuf2_descriptor[STRING3_CHAR_INDEX + i * 2] = chip_id[i];
    }
}

static void usbd_event_handler(uint8_t busid, uint8_t event)
{
    switch (event)
    {
    case USBD_EVENT_RESET:
        break;
    case USBD_EVENT_CONNECTED:
        break;
    case USBD_EVENT_DISCONNECTED:
        break;
    case USBD_EVENT_RESUME:
        break;
    case USBD_EVENT_SUSPEND:
        break;
    case USBD_EVENT_CONFIGURED:
        bootuf2_init();
        break;
    case USBD_EVENT_SET_REMOTE_WAKEUP:
        break;
    case USBD_EVENT_CLR_REMOTE_WAKEUP:
        break;

    default:
        break;
    }
}

void usbd_msc_get_cap(uint8_t busid, uint8_t lun, uint32_t *block_num, uint32_t *block_size)
{
    *block_num = bootuf2_get_sector_count();
    *block_size = bootuf2_get_sector_size();

    USB_LOG_INFO("sector count:%d, sector size:%d\n", *block_num, *block_size);
}
int usbd_msc_sector_read(uint8_t busid, uint8_t lun, uint32_t sector, uint8_t *buffer, uint32_t length)
{
    boot2uf2_read_sector(sector, buffer, length / bootuf2_get_sector_size());
    return 0;
}

int usbd_msc_sector_write(uint8_t busid, uint8_t lun, uint32_t sector, uint8_t *buffer, uint32_t length)
{
    bootuf2_write_sector(sector, buffer, length / bootuf2_get_sector_size());
    return 0;
}

static struct usbd_interface intf0;

void msc_bootuf2_init(uint8_t busid, uint32_t reg_base)
{
    clock_add_to_group(clock_usb0, 0);
    /* HPM6364IPA2 (eLQFP144) has no USBVBUS pin, so the PHY cannot detect
     * VBUS and the device controller stays inactive until vbus valid/sess
     * valid are forced high. See DS pin list (USBVBUS only exists on
     * BGA_172) and SDK hpm5301evklite board_init_usb(). */
    usb_phy_using_internal_vbus(HPM_USB0);
    modify_descriptor_chip_id();
    boot2uf2_flash_init();
    usbd_desc_register(busid, msc_bootuf2_descriptor);
    usbd_add_interface(busid, usbd_msc_init_intf(busid, &intf0, MSC_OUT_EP, MSC_IN_EP));

    usbd_initialize(busid, reg_base, usbd_event_handler);
}

void boot2uf2_flash_init(void)
{
    /* FAL is initialized in main(); this call is kept for symmetry with the
     * HSLink reference and is idempotent. */
    (void)fal_init();
}

int bootuf2_flash_write(uint32_t address, const uint8_t *data, size_t size)
{
    const struct fal_partition *part = fal_partition_find("app");
    if (part == NULL)
    {
        return 1;
    }

    uint32_t const addr = address - FLASH_BASE - part->offset;
    USB_LOG_INFO("address:%08x, size:%d\n", address, size);

    if (fal_partition_erase(part, addr, size) < 0)
    {
        USB_LOG_ERR("Erase failed at offset %08x!\r\n", addr);
        return 1;
    }

    if (fal_partition_write(part, addr, data, size) < 0)
    {
        USB_LOG_ERR("Program failed at offset %08x!\r\n", addr);
        return 1;
    }

    return 0;
}

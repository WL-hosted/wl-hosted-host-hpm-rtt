/*
 * FAL XPI NOR flash port for the HPM6360 bootloader.
 *
 * Copied from App's board/fal_flash_port.c (HPMicro BSD-3-Clause, see the
 * Change Logs below) and adapted for the bare-metal bootloader:
 *  - dropped RT-Thread / board.h dependencies (no rt_* APIs, no RTT headers)
 *  - flash device symbol kept as "onchip_flash" (see config/fal_cfg.h)
 *  - RAM functions go to ".fast" (ILM, copied by start.S) instead of
 *    ".isr_vector", which is reserved for the vector table in the BL layout
 *  - XPI config option comes from xpi0_option[] in src/main.c
 *    (.nor_cfg_option section, the same encoding the ROM uses to boot)
 *
 * Change Logs (from the App version):
 * Date         Author      Notes
 * 2022-03-09   hpmicro     First implementation
 * 2022-08-01   hpmicro     Fixed random crashing during kvdb_init
 * 2022-08-03   hpmicro     Improved erase speed
 * 2023-01-31   hpmicro     Fix random crashing issue if the global interrupt is
 * always enabled
 *
 */
#include <stdint.h>
#include <string.h>
#include <hpm_common.h>
#include <hpm_l1c_drv.h>
#include <hpm_romapi.h>
#include <fal.h>
#include "bootuf2_config.h"

#define FAL_ENTER_CRITICAL()                                                   \
    do {                                                                       \
        disable_global_irq(CSR_MSTATUS_MIE_MASK);                              \
    } while (0)

#define FAL_EXIT_CRITICAL()                                                    \
    do {                                                                       \
        enable_global_irq(CSR_MSTATUS_MIE_MASK);                               \
    } while (0)

#define FAL_RAMFUNC __attribute__((section(".fast"), noinline))

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

/* XPI0 ROM option encoding, defined in src/main.c (.nor_cfg_option section) */
extern const uint32_t xpi0_option[4];

/***************************************************************************************************
 *      FAL Porting Guide
 *
 *      1. Most FLASH devices do not support RWW (Read-while-Write), the codes
 * to access the FLASH must be placed at RAM or ROM code
 *      2. During FLASH erase/program, it is recommended to disable the
 * interrupt, or place the interrupt related codes to RAM
 *
 ***************************************************************************************************/

static int init(void);
static int read(long offset, uint8_t *buf, size_t size);
static int write(long offset, const uint8_t *buf, size_t size);
static int erase(long offset, size_t size);

static xpi_nor_config_t s_flashcfg;
static bool flash_ready;

/**
 * @brief FAL Flash device context
 */
struct fal_flash_dev onchip_flash = {
    .name = NOR_FLASH_DEV_NAME,
    /* .addr/.len 为运行时值，在 init() 中设置：
     *  - addr 取链接脚本导出的 FLASH_BASE（ORIGIN(XPI0)，XIP1 接 flash 时改链接脚本即可）
     *  - len  取 ROM API 探测到的芯片实际容量 */
    .addr = 0,
    .len = 0,
    .blk_size = 4096,
    .ops = {.init = init, .read = read, .write = write, .erase = erase},
    .write_gran = 1
};

/**
 * @brief FAL initialization
 *        This function probes the FLASH using the ROM API
 */
FAL_RAMFUNC static int init(void) {
    int ret = 0;
    xpi_nor_config_option_t cfg_option;
    cfg_option.header.U = xpi0_option[0];
    cfg_option.option0.U = xpi0_option[1];
    cfg_option.option1.U = xpi0_option[2];

    FAL_ENTER_CRITICAL();
    hpm_stat_t status = rom_xpi_nor_auto_config(HPM_XPI0, &s_flashcfg, &cfg_option);
    FAL_EXIT_CRITICAL();
    if (status != status_success) {
        ret = -1;
    } else {
        s_flashcfg.device_info.clk_freq_for_non_read_cmd = 0U;
        /* update the flash chip information */
        uint32_t sector_size;
        rom_xpi_nor_get_property(
            HPM_XPI0,
            &s_flashcfg,
            xpi_nor_property_sector_size,
            &sector_size
        );
        uint32_t flash_size;
        rom_xpi_nor_get_property(
            HPM_XPI0,
            &s_flashcfg,
            xpi_nor_property_total_size,
            &flash_size
        );
        if (sector_size == 0U || flash_size == 0U) {
            return -1;
        }
        onchip_flash.addr = FLASH_BASE;
        onchip_flash.blk_size = sector_size;
        onchip_flash.len = flash_size;
        flash_ready = true;
    }

    return ret;
}

/**
 * @brief FAL read function
 *        Read data from FLASH
 * @param offset FLASH offset
 * @param buf Buffer to hold data read by this API
 * @param size Size of data to be read
 * @return actual read bytes
 */
FAL_RAMFUNC static int read(long offset, uint8_t *buf, size_t size) {
    if (!flash_ready || offset < 0 || (uint32_t)offset + size > onchip_flash.len) {
        return -1;
    }
    uint32_t flash_addr = onchip_flash.addr + offset;
    uint32_t aligned_start = HPM_L1C_CACHELINE_ALIGN_DOWN(flash_addr);
    uint32_t aligned_end = HPM_L1C_CACHELINE_ALIGN_UP(flash_addr + size);
    uint32_t aligned_size = aligned_end - aligned_start;
    FAL_ENTER_CRITICAL();
    l1c_dc_invalidate(aligned_start, aligned_size);
    FAL_EXIT_CRITICAL();

    (void)memcpy(buf, (void *)flash_addr, size);

    return (int)size;
}

/**
 * @brief Write unaligned data to the page
 * @param offset FLASH offset
 * @param buf Data buffer
 * @param size Size of data to be written
 * @return actual size of written data or error code
 */
FAL_RAMFUNC static int write_unaligned_page_data(
    long offset, const uint32_t *buf, size_t size
) {
    hpm_stat_t status;

    FAL_ENTER_CRITICAL();
    status = rom_xpi_nor_program(
        HPM_XPI0,
        xpi_xfer_channel_auto,
        &s_flashcfg,
        buf,
        offset,
        size
    );
    FAL_EXIT_CRITICAL();

    if (status != status_success) {
        return -1;
    }

    return (int)size;
}

/**
 * @brief FAL write function
 *        Write data to specified FLASH address
 * @param offset FLASH offset
 * @param buf Data buffer
 * @param size Size of data to be written
 * @return actual size of written data or error code
 */
FAL_RAMFUNC static int write(
    long offset, const uint8_t *buf, size_t size
) {
    if (!flash_ready || offset < 0 || (uint32_t)offset + size > onchip_flash.len ||
        size == 0U) {
        return -1;
    }
    uint32_t *src = NULL;
    uint32_t buf_32[64];
    uint32_t write_size;
    size_t remaining_size = size;
    int ret = (int)size;

    uint32_t page_size;
    rom_xpi_nor_get_property(
        HPM_XPI0,
        &s_flashcfg,
        xpi_nor_property_page_size,
        &page_size
    );
    uint32_t offset_in_page = (uint32_t)offset % page_size;
    if (offset_in_page != 0) {
        uint32_t write_size_in_page = page_size - offset_in_page;
        uint32_t write_page_size = MIN(write_size_in_page, (uint32_t)size);
        (void)memcpy(buf_32, buf, write_page_size);
        write_size = write_unaligned_page_data(offset, buf_32, write_page_size);
        if (write_size < 0) {
            ret = -1;
            goto write_quit;
        }

        remaining_size -= write_page_size;
        offset += write_page_size;
        buf += write_page_size;
    }

    while (remaining_size > 0) {
        write_size = MIN((uint32_t)remaining_size, sizeof(buf_32));
        memcpy(buf_32, buf, write_size);
        src = &buf_32[0];

        FAL_ENTER_CRITICAL();
        hpm_stat_t status = rom_xpi_nor_program(
            HPM_XPI0,
            xpi_xfer_channel_auto,
            &s_flashcfg,
            src,
            offset,
            write_size
        );
        FAL_EXIT_CRITICAL();

        if (status != status_success) {
            ret = -1;
            break;
        }

        remaining_size -= write_size;
        buf += write_size;
        offset += write_size;
    }

write_quit:
    return ret;
}

/**
 * @brief FAL erase function
 *        Erase specified FLASH region
 * @param offset the start FLASH address to be erased
 * @param size size of the region to be erased
 * @ret 0 Erase operation is successful
 * @retval -1 Erase operation failed
 */
FAL_RAMFUNC static int erase(long offset, size_t size) {
    if (!flash_ready || offset < 0 || (uint32_t)offset + size > onchip_flash.len ||
        size == 0U) {
        return -1;
    }
    uint32_t aligned_size =
        (size + onchip_flash.blk_size - 1U) & ~(onchip_flash.blk_size - 1U);
    hpm_stat_t status;
    int ret = (int)size;

    uint32_t block_size;
    uint32_t sector_size;
    (void)rom_xpi_nor_get_property(
        HPM_XPI0,
        &s_flashcfg,
        xpi_nor_property_sector_size,
        &sector_size
    );
    (void)rom_xpi_nor_get_property(
        HPM_XPI0,
        &s_flashcfg,
        xpi_nor_property_block_size,
        &block_size
    );
    uint32_t erase_unit;
    while (aligned_size > 0) {
        FAL_ENTER_CRITICAL();
        if (((uint32_t)offset % block_size == 0) && (aligned_size >= block_size)) {
            erase_unit = block_size;
            status = rom_xpi_nor_erase_block(
                HPM_XPI0,
                xpi_xfer_channel_auto,
                &s_flashcfg,
                offset
            );
        } else {
            erase_unit = sector_size;
            status = rom_xpi_nor_erase_sector(
                HPM_XPI0,
                xpi_xfer_channel_auto,
                &s_flashcfg,
                offset
            );
        }
        FAL_EXIT_CRITICAL();

        if (status != status_success) {
            ret = -1;
            break;
        }
        offset += erase_unit;
        aligned_size -= erase_unit;
    }

    return ret;
}

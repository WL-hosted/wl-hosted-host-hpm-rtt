/*
 * Copyright (c) 2024, sakumisu
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef BOOTUF2_CONFIG_H
#define BOOTUF2_CONFIG_H

/* Baseline: CherryDAP/projects/HSLink-Pro/bootloader/bootuf2/bootuf2_config.h,
 * values matched to the HPM6364 flash layout. */

/* Flash XIP 基址：由链接脚本 bootloader/linker/flash_xip.ld 导出（ORIGIN(XPI0)），
 * 不在此处硬编码。仅可用作运行期右值（如 FAL 设备 addr、跳转地址），
 * 不可用于静态初始化。 */
#include <stdint.h>
extern const uint32_t __flash_base__[];
#define FLASH_BASE ((uint32_t)(uintptr_t)__flash_base__)

#define CONFIG_PRODUCT            "HPM"
#define CONFIG_BOARD              "HPM6364"
#define CONFIG_BOOTUF2_INDEX_URL  "https://github.com/cherry-embedded/CherryUSB"
#define CONFIG_BOOTUF2_JOIN_URL   ""
#define DEFAULT_REASON "DEFAULT_REASON_DEFAULT_REASON_DEFAULT_REASON\r\n"

#define CONFIG_BOOTUF2_CACHE_SIZE         4096
#define CONFIG_BOOTUF2_SECTOR_SIZE        512
#define CONFIG_BOOTUF2_SECTOR_PER_CLUSTER 2
#define CONFIG_BOOTUF2_SECTOR_RESERVED    1
#define CONFIG_BOOTUF2_NUM_OF_FAT         2
#define CONFIG_BOOTUF2_ROOT_ENTRIES       64

#define CONFIG_BOOTUF2_FAMILYID      0x0A4D5048UL
#define CONFIG_BOOTUF2_FLASHMAX      0x00360000UL
#define CONFIG_BOOTUF2_PAGE_COUNTMAX (CONFIG_BOOTUF2_FLASHMAX / 4096UL)

#endif /* BOOTUF2_CONFIG_H */

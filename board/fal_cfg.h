/*
 * Copyright (c) 2022-2023 HPMicro
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */

#ifndef _FAL_CFG_H_
#define _FAL_CFG_H_

#include <board.h>
#include <rtconfig.h>

#ifdef RT_USING_FAL
#define NOR_FLASH_DEV_NAME "onchip"

/* Flash XIP 基址：由链接脚本（flash_rtt_uf2.ld / flash_rtt.ld）导出
 * 的 __flash_base__ 提供，不在此硬编码；仅运行期右值，不可用于静态初始化。
 * Flash 容量由 FAL init() 时 ROM API 探测。 */
#include <stdint.h>
extern const uint32_t __flash_base__[];
#define FLASH_BASE ((uint32_t)(uintptr_t)__flash_base__)

/* ===================== Flash device Configuration ========================= */
extern struct fal_flash_dev onchip_flash;

#ifdef FAL_USING_SFUD_PORT
/* SFUD 端口设备，定义于 fal_flash_sfud_port.c（FAL_USING_SFUD_PORT=y 时编译） */
extern struct fal_flash_dev nor_flash0;
#endif

/* flash device table */
#define FAL_FLASH_DEV_TABLE                                                    \
    {                                                                          \
        &onchip_flash,                                                           \
        &nor_flash0,                                                             \
    }
#endif /* RT_USING_FAL */

#endif /* _FAL_CFG_H_ */

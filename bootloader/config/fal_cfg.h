/*
 * Copyright (c) 2022-2023 HPMicro
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */

#ifndef _FAL_CFG_H_
#define _FAL_CFG_H_

#define NOR_FLASH_DEV_NAME "onchip"

/* ===================== Flash device Configuration ========================= */
extern struct fal_flash_dev onchip_flash;

/* flash device table */
#define FAL_FLASH_DEV_TABLE                                                    \
    {                                                                          \
        &onchip_flash,                                                           \
    }
/* ====================== Partition Configuration ========================== */
#define FAL_PART_HAS_TABLE_CFG
/* FAL 分区表：由 part_table/part_table.md 经 tools/gen_part_table.py 生成，
 * 输出 part_table/part_table.h，勿手改 */
#include "part_table.h"
#endif /* _FAL_CFG_H_ */

/*
 * part_table.h — 生成文件，请勿手改（DO NOT EDIT）。
 * 来源: /Users/kai/Downloads/wlh-hpm-rtt/wlh-hpm-rtt/part_table/part_table.md
 * 生成器: tools/gen_part_table.py
 * 分区布局: onchip: 4 MiB, norflash0: 16 MiB
 */

#ifndef PART_TABLE_H
#define PART_TABLE_H

/* FAL 分区表（BL 落盘分区表，由 part_table.md 生成，经 fal_cfg.h 引入） */
#define FAL_PART_TABLE                                                          \
    {                                                                           \
        {FAL_PART_MAGIC_WORD, "bl"     , "onchip"   , 0        , 128*1024 , 0}, \
        {FAL_PART_MAGIC_WORD, "app"    , "onchip"   , 128*1024 , 3456*1024, 0}, \
        {FAL_PART_MAGIC_WORD, "flashdb", "onchip"   , 3584*1024, 32*1024  , 0}, \
        {FAL_PART_MAGIC_WORD, "resv"   , "onchip"   , 3616*1024, 480*1024 , 0}, \
        {FAL_PART_MAGIC_WORD, "app_s2" , "norflash0", 0        , 4096*1024, 0}, \
        {FAL_PART_MAGIC_WORD, "resv_2" , "norflash0", 4096*1024, 4096*1024, 0}, \
        {FAL_PART_MAGIC_WORD, "fs"     , "norflash0", 8192*1024, 8192*1024, 0}, \
    }

#endif /* PART_TABLE_H */

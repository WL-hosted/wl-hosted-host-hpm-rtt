# HPM bootloader（wl host 起点工程）

Build it independently from the repository root:

```sh
cmake -S bootloader -B bootloader/build
cmake --build bootloader/build
```

`hpm_bootloader.bin` occupies the `bl` partition (`0x80000000`, 128 KiB).
The application is linked at `0x80020000` and packaged by
`tools/uf2_pack.py`.  The BL has no SDRAM memory region.

Or from the App CMake project (`custom.cmake` targets, ExternalProject form):

```sh
cmake --build build --target build_bootloader
cmake --build build --target download_bootloader   # OpenOCD 烧录到 bl 分区
cmake --build build --target download_bl_app       # OpenOCD 整片烧录 BL+App 合并 HEX
```

## 结构

- `bootuf2/` — 官方 CherryUSB UF2 组件（与
  `CherryDAP/projects/HSLink-Pro/bootloader/bootuf2/` 同源，Apache-2.0）：
  完整 FAT16 虚拟磁盘（DBR/FAT/ROOT/DATA）+ `INFO_UF2TXT`/`INDEX.HTM`/
  `JOIN.HTM`/串号文件 + 4 KB 缓存擦写。`bootuf2_config.h` 仅含 UF2
  磁盘配置（`CONFIG_BOOTUF2_*`）与 `FLASH_BASE` 宏（链接脚本
  `linker/flash_xip.ld` 导出的 `__flash_base__`，不硬编码）。
- `msc_bootuf2.c` — MSC 适配层：USB 描述符（string3 注入 OTP chip id）、
  `USBD_EVENT_CONFIGURED` 时初始化 UF2 磁盘、flash 擦写经 **FAL 分区 API**
  （`fal_partition_find("app")` + `erase/write`）。
- `src/main.c` — 入口：LED/看门狗/FAL/FlashDB 状态初始化 → 按
  `bl_state` 与 App 签名决定跳转或进入 UF2 模式；升级完成软复位。App
  偏移经 FAL 表运行时查询，基址用 `FLASH_BASE`（链接脚本导出）。
- 启动使用 SDK 标准 `soc/HPM6300/HPM6360/toolchains/reset.c`
  （汇编 `start.S` → `reset_handler` → `system_init()` → `main()`），
  未自写 startup；`startup/syscalls.c` 提供 newlib 最小 syscall stub，
  `_sbrk` 复用 SDK `utils/hpm_sbrk.c`（链接脚本 `_heap_size = 8K`）。

## 存储与状态

静态 FAL 表：`bl` (128 KiB)、`app` (0x360000)、`flashdb` (32 KiB)、
`resv` (0x78000)。分区定义源为 `part_table/part_table.md`，经
`tools/gen_part_table.py` 生成 `part_table/part_table.h`（`config/fal_cfg.h`
`#include`，勿手改）。FlashDB keys `bl.fail_count` 与 `bl.force_bootloader`
记录 WDG0 崩溃状态：WDG0 使用内部外设时钟，连续 5 次 WDG 复位强制进入
UF2 模式；`bl.force_bootloader` 为一次性标志——BL 读到后立即清除，
本次会话留在 UF2，下次上电/复位直接进入 App（除非 App 签名仍无效）。
跳转 App 例程保留但默认禁用（`BL_ENABLE_APP_JUMP=0`，App
迁到 `0x80020000` 布局后开启）。

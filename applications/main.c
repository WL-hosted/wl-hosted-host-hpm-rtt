#define LOG_TAG "main"
#include <ulog.h>

#include "rtt_board.h"
#include <drv_gpio.h>
#include <rtdevice.h>
#include <rtthread.h>
#include "hpm_wdg_drv.h"
#include "dev_spi_flash_sfud.h"
#include <fal.h>
#include <dfs_fs.h>

/* BL 使能 WDG0(复位窗口约 5s);App 在 idle 线程喂狗,若 App 异常导致
 * idle 停摆,WDG 复位系统,BL 侧 fail_count 累计后停留 UF2 模式。 */
#ifndef DISABLE_WDG
static void watchdog_feed_hook(void) {
    wdg_restart(HPM_WDG0);
}
#endif

int main(void) {
#ifdef DISABLE_WDG
    wdg_disable(HPM_WDG0);
#else
    rt_thread_idle_sethook(watchdog_feed_hook);
#endif

    struct rt_spi_device * sfud_dev = (struct rt_spi_device *) rt_malloc(sizeof(struct rt_spi_device));
    rt_err_t ret = rt_spi_bus_attach_device_cspin(
        sfud_dev,
        "spi10",
        "spi1",
        GET_PIN(A, 16),
        RT_NULL
    );
    if (ret != RT_EOK) {
        rt_kprintf("spi attach failed\n");
        return 1;
    }
    rt_sfud_flash_probe("norflash0", "spi10");

    fal_init();

    /* fs 分区（norflash0 @ 8MiB, 8MiB）挂 littlefs 到根目录 */
    struct rt_device *fs_dev = fal_mtd_nor_device_create("fs");
    if (fs_dev == RT_NULL) {
        log_e("create fs mtd device failed");
    } else if (dfs_mount("fs", "/", "lfs", 0, RT_NULL) == RT_EOK) {
        log_i("mount fs(lfs) on / success");
    } else {
        log_w("mount fs failed, try to format...");
        if (dfs_mkfs("lfs", "fs") == RT_EOK &&
            dfs_mount("fs", "/", "lfs", 0, RT_NULL) == RT_EOK) {
            log_i("mount fs(lfs) success after format");
        } else {
            log_e("mount fs failed again");
        }
    }

    return 0;
}
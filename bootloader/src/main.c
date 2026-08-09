#include "bl_state.h"
#include "board_init.h"
#include "bootuf2.h"
#define LOG_TAG "main"
#include "elog.h"

#include <fal.h>
#include "sfud.h"
#include <hpm_clock_drv.h>
#include <hpm_gpio_drv.h>
#include <hpm_iomux.h>
#include <hpm_l1c_drv.h>
#include <hpm_ppor_drv.h>
#include <hpm_soc.h>
#include <hpm_wdg_drv.h>
#include "bootuf2_config.h"

void msc_bootuf2_init(uint8_t busid, uint32_t reg_base);

int bl_sfud_flash_probe(void);

__attribute__((section(".nor_cfg_option"), used))
const uint32_t xpi0_option[4] = {0xfcf90002,0x00000007,0x00001000,0x00000000};

static void led_init(void) {
    HPM_IOC->PAD[IOC_PAD_PC22].FUNC_CTL = IOC_PC22_FUNC_CTL_GPIO_C_22;
    gpio_set_pin_output(HPM_GPIO0, GPIO_OE_GPIOC, 22);
    gpio_write_pin(HPM_GPIO0, GPIO_DO_GPIOC, 22, 1); /* PC22 is active-low. */
}

static void led_toggle(void) {
    static bool on;
    on = !on;
    gpio_write_pin(HPM_GPIO0, GPIO_DO_GPIOC, 22, on ? 0 : 1);
}

static void watchdog_init(void) {
#ifdef DISABLE_WDG
    /* Debug only: retain crash state until OpenOCD explicitly resets the MCU. */
    wdg_disable(HPM_WDG0);
#else
    clock_add_to_group(clock_watchdog0, 0);
    uint32_t hz = clock_get_frequency(clock_watchdog0);
    wdg_control_t config = {
        .reset_interval = reset_interval_clock_period_mult_128,
        .interrupt_interval =
            wdg_convert_interrupt_interval_from_us(hz, 5U * 1000U * 1000U),
        .reset_enable = true,
        .interrupt_enable = false,
        .clksrc = wdg_clksrc_pclk, /* internal peripheral/bus clock */
        .wdg_enable = true,
    };
    (void)wdg_init(HPM_WDG0, &config);
#endif
}

static void watchdog_feed(void) {
#ifndef DISABLE_WDG
    wdg_restart(HPM_WDG0);
#endif
}

static bool app_valid(void) {
    /* App 分区偏移经 FAL 表运行时获取；跳转基址由链接脚本导出的 FLASH_BASE 提供 */
    const struct fal_partition *app = fal_partition_find("app");
    if (app == NULL) {
        return false;
    }
    return *(const volatile uint32_t *)(uint32_t)(FLASH_BASE + app->offset) ==
           CONFIG_BOOTUF2_FAMILYID;
}

static void jump_to_app(void) {
    const struct fal_partition *app = fal_partition_find("app");
    uint32_t app_start =
        (app != NULL) ? (uint32_t)(FLASH_BASE + app->offset) : 0U;
    /* Kept implemented for board validation; disabled by default at build time.
     */
    fencei();
    l1c_dc_disable();
    __asm volatile("jr %0" : : "r"(app_start + 4U));
    __builtin_unreachable();
}

int main(void) {
    board_init();

    elog_init();
    for (uint8_t lvl = ELOG_LVL_ASSERT; lvl <= ELOG_LVL_VERBOSE; lvl++) {
        elog_set_fmt(lvl, ELOG_FMT_LVL | ELOG_FMT_TAG | ELOG_FMT_TIME);
    }
    elog_start();

    sfud_init();

    int fal_devs = fal_init();
    elog_i("main", "fal_init: %d flash device(s)", fal_devs);


    led_init();
    watchdog_init();

    uint32_t reset_flags = ppor_reset_get_flags(HPM_PPOR);
    ppor_reset_clear_flags(HPM_PPOR, reset_flags);
    uint32_t fail_count = 0;
    bool state_ok = bl_state_init() &&
                    bl_state_update_for_reset(reset_flags, &fail_count);
    bool stay_in_bl = !state_ok || bl_state_should_stay() || !app_valid();
    log_i("reset_flags=0x%08lx fail_count=%lu stay_in_bl=%d",
           (unsigned long)reset_flags, (unsigned long)fail_count,
           (int)stay_in_bl);

    if (!stay_in_bl) {
        for (int i = 0; i < 5000; i++) {
            if (i % 1000 == 0) {
                log_i("jumping to app in %d ms", (5000 - i));
            }
            watchdog_feed();
            clock_cpu_delay_us(1000);
        }
        jump_to_app();
    }

    msc_bootuf2_init(0, HPM_USB0_BASE);
    uint8_t cnt = 0;
    for (;;) {
        elog_i("main", "led: %d", cnt++);
        led_toggle();
        for (int i = 0; i < 1000; i++)
            clock_cpu_delay_us(1000);
        if (bootuf2_is_write_done()) {
            /* Software reset clears the UF2 block mask so a repeated
             * drag-and-drop upgrade works; after reset the BL validates
             * the new app signature and jumps (or stays in UF2 mode). */
            ppor_reset_mask_set_source_enable(HPM_PPOR, ppor_reset_software);
            ppor_sw_reset(HPM_PPOR, 1000);
        }
        watchdog_feed();
    }
}

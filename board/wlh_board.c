#include "board.h"

#include <rtthread.h>
#include <ulog.h>

#include "hpm_gpio_drv.h"
#include "wlh_rtt_board.h"

#define WLH_ESP_EN_PIN 14u

extern void rt_hw_cpu_reset(void);

void rt_hw_sdxc_prepare_device(void) {
    /* PA14 is ESP_EN, not card-detect. Open-drain low asserts reset; changing
     * the GPIO direction to input releases the external pull-up without ever
     * driving a push-pull high. */
    HPM_IOC->PAD[IOC_PAD_PA14].FUNC_CTL = IOC_PA14_FUNC_CTL_GPIO_A_14;
    HPM_IOC->PAD[IOC_PAD_PA14].PAD_CTL = IOC_PAD_PAD_CTL_OD_SET(1) |
                                         IOC_PAD_PAD_CTL_PE_SET(1) |
                                         IOC_PAD_PAD_CTL_PS_SET(1);
    gpio_set_pin_output_with_initial(
        HPM_GPIO0, GPIO_DO_GPIOA, WLH_ESP_EN_PIN, 0u
    );
    rt_thread_mdelay(100u);
    gpio_set_pin_input(HPM_GPIO0, GPIO_DI_GPIOA, WLH_ESP_EN_PIN);
    rt_thread_mdelay(1500u);
}

static void fatal_recovery(void *context, int reason) {
    (void)context;
    log_e("WL-hosted fatal transport failure: %d; resetting board", reason);
    rt_hw_cpu_reset();
}

static const wlh_rtt_board_ops_t board_ops = {
    .fatal_recovery = fatal_recovery,
};

const wlh_rtt_board_ops_t *wlh_rtt_board_get_ops(void **context) {
    if (context != RT_NULL)
        *context = RT_NULL;
    return &board_ops;
}

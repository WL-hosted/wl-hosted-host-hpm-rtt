/*
 * Copyright (c) 2021 HPMicro
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */

#ifndef _RTT_BOARD_H
#define _RTT_BOARD_H
#include "board.h"
#include "hpm_common.h"
#include "hpm_soc.h"
#include <drv_gpio.h>

/* mchtimer section */
#define BOARD_MCHTMR_FREQ_IN_HZ (24000000UL)

/* gptmr as os_tick */
#define BOARD_OS_TIMER HPM_GPTMR0
#define BOARD_OS_TIMER_CH 1
#define BOARD_OS_TIMER_IRQ IRQn_GPTMR0
#define BOARD_OS_TIMER_CLK_NAME (clock_gptmr0)

/* CAN section */
#define BOARD_CAN_NAME "can0"
#define BOARD_CAN_HWFILTER_INDEX (0U)

/* UART section */

/* ADC section */
#define BOARD_ADC_NAME BOARD_APP_ADC16_NAME
#define BOARD_ADC_CHANNEL BOARD_APP_ADC16_CH_1

#define BOARD_SD_NAME "sd0"

#define IRQn_PendSV IRQn_DEBUG_0

#define BOARD_ENET0_INF (0U) /* 0: RMII, 1: RGMII */
#define BOARD_ENET0_INT_REF_CLK (1U)
#define BOARD_ENET0_PHY_RST_TIME (30)

#if BOARD_ENET0_INF
#define BOARD_ENET0_TX_DLY (0U)
#define BOARD_ENET0_RX_DLY (0U)
#endif

#if defined(__USE_ENET_PTP) && __USE_ENET_PTP
#define BOARD_ENET0_PTP_CLOCK (clock_ptp0)
#endif

/***************************************************************
 *
 * RT-Thread related definitions
 *
 **************************************************************/
extern unsigned int __heap_start__;
extern unsigned int __heap_end__;

#define RT_HW_HEAP_BEGIN ((void *)&__heap_start__)
#define RT_HW_HEAP_END ((void *)&__heap_end__)

typedef struct {
    uint16_t vdd;
    uint8_t bus_width;
    uint8_t drive_strength;
} sdxc_io_cfg_t;

void app_init_usb_pins(void);

#if defined(__cplusplus)
extern "C" {
#endif /* __cplusplus */

#if defined(__cplusplus)
}
#endif /* __cplusplus */
#endif /* _RTT_BOARD_H */

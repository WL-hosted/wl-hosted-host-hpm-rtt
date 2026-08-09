/*
 * Copyright (c) 2026 HPM project
 *
 * EasyLogger output port for the bootloader: synchronous direct output over
 * the console UART (UART0, see board_cfg.h). No async mode, no DMA, no OS.
 */

#include "board_cfg.h"
#include "elog.h"

#include <stdio.h>
#include <string.h>

#include "hpm_mchtmr_drv.h"
#include "hpm_uart_drv.h"

/* mchtmr0 is clocked at 24 MHz by board_init_clock() (clk_src_osc24m, div 1) */
#define BL_MCHTMR_HZ (24UL * 1000UL * 1000UL)

ElogErrCode elog_port_init(void)
{
    return ELOG_NO_ERR;
}

ElogErrCode elog_port_deinit(void)
{
    return ELOG_NO_ERR;
}

/* Single-threaded bootloader: no output lock needed. */
void elog_port_output_lock(void)
{
}

void elog_port_output_unlock(void)
{
}

/* Direct (blocking) UART output, byte by byte; no DMA, no buffering. */
void elog_port_output(const char *log, size_t size)
{
    UART_Type *uart = (UART_Type *)BOARD_CONSOLE_UART_BASE;
    size_t i;

    for (i = 0; i < size; i++) {
        uart_send_byte(uart, (uint8_t)log[i]);
    }
}

/* No process/thread info in the bare-metal bootloader. */
const char *elog_port_get_p_info(void)
{
    return "";
}

const char *elog_port_get_t_info(void)
{
    return "";
}

/* Uptime in milliseconds since reset, derived from the 24 MHz mchtmr0.
 * The 32-bit tick wraps around every ~179 s, which is fine for bootloader
 * diagnostics. */
const char *elog_port_get_time(void)
{
    static char time_buf[12];
    uint32_t tick_ms;

    tick_ms = (uint32_t)(mchtmr_get_count(HPM_MCHTMR) & 0xFFFFFFFFUL) /
              (BL_MCHTMR_HZ / 1000UL);
    snprintf(time_buf, sizeof(time_buf), "%lu", (unsigned long)tick_ms);

    return time_buf;
}

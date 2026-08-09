#ifndef BL_BOARD_CFG_H
#define BL_BOARD_CFG_H

#include "hpm_clock_drv.h"
#include "hpm_debug_console.h"
#include "hpm_soc.h"

#define BOARD_NAME "HPM Bootloader"

#define BOARD_CPU_FREQ (648 * 1000 * 1000)

/* uart console section (UART0, same port as the App's debug console) */
#define BOARD_CONSOLE_TYPE CONSOLE_TYPE_UART
#define BOARD_CONSOLE_UART_BASE HPM_UART0
#define BOARD_CONSOLE_UART_CLK_NAME clock_uart0
#define BOARD_CONSOLE_UART_BAUDRATE (2 * 1000 * 1000)

#define BOARD_SHOW_CLOCK 1
#define BOARD_SHOW_BANNER 0 /* the bootloader does not print the App banner */

#endif /* BL_BOARD_CFG_H */

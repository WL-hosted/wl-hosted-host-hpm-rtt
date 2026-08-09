/*
 * Copyright (c) 2026 HPM Project
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Board bring-up entry point for the bootloader (implemented in
 * src/board_init.c, excerpted from the App's board/board.c).
 */
#ifndef BL_BOARD_INIT_H
#define BL_BOARD_INIT_H

#ifdef __cplusplus
extern "C" {
#endif

void board_init(void);

#ifdef __cplusplus
}
#endif

#endif /* BL_BOARD_INIT_H */

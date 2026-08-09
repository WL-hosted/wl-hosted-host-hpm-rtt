/*
 * This file is part of the Serial Flash Universal Driver Library.
 *
 * Copyright (c) 2016-2018, Armink, <armink.ztl@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * 'Software'), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED 'AS IS', WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Function: Portable interface for each platform.
 * Created on: 2016-04-23
 *
 * ===================== hpm BL 板级适配 =====================
 * 平台：HPM6360 裸机（Bootloader）
 * 硬件：SPI1 外挂 W25Q128（16MB），引脚 PA16=CSN / PA17=MISO / PA18=SCLK / PA19=MOSI
 * 实现：
 *  - 参考 HPM SDK serial_nor 组件（components/serial_nor/interface/spi/
 *    hpm_serial_nor_host_spi.c）的写法：把 SFUD 的命令流解析为命令/地址/dummy/数据
 *    各阶段，配置 SPI 控制器的 trans_mode（read_only / dummy_read / write_only /
 *    no_data）后经 spi_transfer() 传输；dummy 与数据阶段由 SPI 硬件完成，
 *    不再手工做读写配对。
 *  - 单次传输受 SPI_SOC_TRANSFER_COUNT_MAX(512) 限制，读数据按块循环（每块重发
 *    命令，CS 由硬件自动拉低/拉高）。
 *  - 日志走 elog（log_x 宏，AGENTS.md 约定）。
 * =================================================================
 */

#include <sfud.h>
#include <stdarg.h>
#include <string.h>

#define LOG_TAG "sfud"
#include "elog.h"
#include "hpm_clock_drv.h"
#include "hpm_gpio_drv.h"
#include "hpm_iomux.h"
#include "hpm_soc.h"
#include "hpm_spi_drv.h"

/* HPM SPI 单次传输最大计数（SPI_SOC_TRANSFER_COUNT_MAX） */
#define SFUD_SPI_TRANSFER_MAX (512U)

/* 当前 SFUD flash 指针，用于判断 4 字节地址模式 */
static sfud_flash *s_flash;

/**
 * SPI1 引脚复用（HPM6360，ALT_SELECT 5），PAD_CTL 参考 App board/pinmux.c SPI3 写法
 */
static void sfud_spi1_pins_init(void)
{
    uint32_t spi_pad_ctl =
        IOC_PAD_PAD_CTL_SR_MASK | IOC_PAD_PAD_CTL_SPD_SET(3);

    /* PA16 = GPIO 软件 CS，与 App 的
     * rt_spi_bus_attach_device_cspin(..., GET_PIN(A, 16), ...) 保持一致；
     * CS 低有效，空闲置高，传输期间由 spi_write_read() 控制 */
    HPM_IOC->PAD[IOC_PAD_PA16].FUNC_CTL = IOC_PA16_FUNC_CTL_GPIO_A_16;
    gpio_set_pin_output(HPM_GPIO0, GPIO_OE_GPIOA, 16);
    gpio_write_pin(HPM_GPIO0, GPIO_DO_GPIOA, 16, 1);

    HPM_IOC->PAD[IOC_PAD_PA17].FUNC_CTL = IOC_PA17_FUNC_CTL_SPI1_MISO;
    /* SCLK 必须使能 LOOP_BACK（输出回送）供 SPI 采样时钟使用，
     * HPM 平台要求（参考 App board/pinmux.c 与 SDK 文档） */
    HPM_IOC->PAD[IOC_PAD_PA18].FUNC_CTL =
        IOC_PA18_FUNC_CTL_SPI1_SCLK | IOC_PAD_FUNC_CTL_LOOP_BACK_SET(1);
    HPM_IOC->PAD[IOC_PAD_PA19].FUNC_CTL = IOC_PA19_FUNC_CTL_SPI1_MOSI;

    HPM_IOC->PAD[IOC_PAD_PA17].PAD_CTL = spi_pad_ctl;
    HPM_IOC->PAD[IOC_PAD_PA18].PAD_CTL = spi_pad_ctl;
    HPM_IOC->PAD[IOC_PAD_PA19].PAD_CTL = spi_pad_ctl;
}

/**
 * 软件 CS 控制（PA16，低有效）
 */
static void sfud_spi1_cs_low(void)
{
    gpio_write_pin(HPM_GPIO0, GPIO_DO_GPIOA, 16, 0);
}

static void sfud_spi1_cs_high(void)
{
    gpio_write_pin(HPM_GPIO0, GPIO_DO_GPIOA, 16, 1);
}

/**
 * 按目标频率配置 SPI1 时序（SCLK）
 */
static void sfud_spi1_frequency_set(uint32_t freq)
{
    spi_timing_config_t timing_config = {0};

    spi_master_get_default_timing_config(&timing_config);
    timing_config.master_config.clk_src_freq_in_hz = clock_get_frequency(clock_spi1);
    timing_config.master_config.sclk_freq_in_hz = freq;
    if (status_success != spi_master_timing_init(HPM_SPI1, &timing_config)) {
        log_e("SPI1 timing init failed (%u Hz)", (unsigned)freq);
    }
}

/**
 * 参照 serial_nor host（hpm_config_cmd_addr_format）：
 * 根据 SFUD 命令流 [命令][地址][dummy][数据] 配置 SPI control 的命令/地址阶段
 *
 * @param control_config 待配置的 SPI control
 * @param cmd 命令字节（spi_transfer 命令阶段参数）
 * @param addr 地址（spi_transfer 地址阶段参数）
 * @param write_buf SFUD 命令流
 * @param write_size 命令流长度
 */
static void config_cmd_addr_format(spi_control_config_t *control_config, uint8_t *cmd, uint32_t *addr,
                                   const uint8_t *write_buf, size_t write_size)
{
    size_t addr_len;
    size_t i;

    *cmd = write_buf[0];
    *addr = 0;
    control_config->master_config.cmd_enable = true;

    /* 地址长度：SFUD 启用 4 字节地址模式时 4 字节，否则 3 字节（W25Q128 16MB 默认 3 字节） */
    addr_len = (s_flash != NULL && s_flash->addr_in_4_byte) ? 4U : 3U;
    if (write_size >= 1U + addr_len) {
        control_config->master_config.addr_enable = true;
        control_config->master_config.addr_phase_fmt = spi_address_phase_format_single_io_mode;
        if (addr_len == 4U) {
            spi_set_address_len(HPM_SPI1, addrlen_32bit);
        } else {
            spi_set_address_len(HPM_SPI1, addrlen_24bit);
        }
        for (i = 0; i < addr_len; i++) {
            *addr = (*addr << 8) | write_buf[1 + i];
        }
    } else {
        control_config->master_config.addr_enable = false;
    }
}

/**
 * SPI write data then read data（参照 serial_nor host read/write）
 *
 * @param spi SPI 设备（未使用）
 * @param write_buf SFUD 命令流：[命令][地址][dummy][数据]
 * @param write_size 写字节数
 * @param read_buf 读数据缓冲
 * @param read_size 读字节数
 */
static sfud_err spi_write_read(const sfud_spi *spi, const uint8_t *write_buf, size_t write_size,
                               uint8_t *read_buf, size_t read_size)
{
    uint8_t cmd;
    uint32_t addr = 0;
    size_t addr_len;
    size_t dummy;
    hpm_stat_t stat;
    sfud_err err = SFUD_SUCCESS;

    (void)spi;

    addr_len = (s_flash != NULL && s_flash->addr_in_4_byte) ? 4U : 3U;

    /* 软件 CS：整个命令序列期间保持选中（低有效），出口统一释放 */
    sfud_spi1_cs_low();

    if (read_size > 0) {
        /* 读数据阶段：命令+地址[+dummy] 后读 read_size 字节。
         * 单次传输受限，按 512 字节分块，每块重发命令/地址（CS 保持低） */
        uint8_t *dst = read_buf;
        uint32_t remaining = (uint32_t)read_size;

        dummy = (write_size >= 1U + addr_len) ? (write_size - 1U - addr_len) : 0U;
        while (remaining > 0U) {
            spi_control_config_t control_config;
            uint32_t chunk = MIN(remaining, SFUD_SPI_TRANSFER_MAX);

            spi_master_get_default_control_config(&control_config);
            config_cmd_addr_format(&control_config, &cmd, &addr, write_buf, write_size);
            if (dummy > 0U) {
                control_config.common_config.dummy_cnt = (uint8_t)(dummy - 1U);
                control_config.common_config.trans_mode = spi_trans_dummy_read;
            } else {
                control_config.common_config.trans_mode = spi_trans_read_only;
            }
            control_config.common_config.data_phase_fmt = spi_single_io_mode;

            stat = spi_transfer(HPM_SPI1, &control_config, &cmd, &addr,
                                NULL, 0, dst, chunk);
            if (stat != status_success) {
                err = SFUD_ERR_READ;
                goto transfer_done;
            }
            dst += chunk;
            addr += chunk;
            remaining -= chunk;
        }
        goto transfer_done;
    }

    /* 写数据或无数据阶段（写使能/擦除/页编程等） */
    {
        spi_control_config_t control_config;

        spi_master_get_default_control_config(&control_config);
        config_cmd_addr_format(&control_config, &cmd, &addr, write_buf, write_size);
        control_config.common_config.data_phase_fmt = spi_single_io_mode;

        if (write_size > 1U + addr_len) {
            control_config.common_config.trans_mode = spi_trans_write_only;
            stat = spi_transfer(HPM_SPI1, &control_config, &cmd, &addr,
                                (uint8_t *)write_buf + 1U + addr_len,
                                (uint32_t)(write_size - 1U - addr_len), NULL, 0);
        } else {
            control_config.common_config.trans_mode = spi_trans_no_data;
            stat = spi_transfer(HPM_SPI1, &control_config, &cmd, &addr, NULL, 0, NULL, 0);
        }
        if (stat != status_success) {
            err = SFUD_ERR_WRITE;
        }
    }

transfer_done:
    sfud_spi1_cs_high();
    return err;
}

/**
 * SFUD 平台初始化入口（sfud.c hardware_init 调用）
 */
sfud_err sfud_spi_port_init(sfud_flash *flash)
{
    spi_format_config_t format_config = {0};

    /* 1. SPI1 引脚（先于时钟使能） */
    sfud_spi1_pins_init();

    /* 2. SPI1 时钟加入 group0 */
    clock_add_to_group(clock_spi1, 0);
    if (clock_get_frequency(clock_spi1) == 0U) {
        log_e("SPI1 clock not ready");
        return SFUD_ERR_TIMEOUT;
    }

    /* 3. 格式：8-bit，SPI Master，SPI Mode 0（CPOL=0/CPHA=0，W25Q128 支持） */
    spi_master_get_default_format_config(&format_config);
    format_config.common_config.data_len_in_bits = 8;
    format_config.common_config.mode = spi_master_mode;
    format_config.common_config.cpol = spi_sclk_low_idle;
    format_config.common_config.cpha = spi_sclk_sampling_odd_clk_edges;
    format_config.common_config.data_merge = false;
    spi_format_init(HPM_SPI1, &format_config);

    /* 4. 时序：SFDP 规范要求探测时钟 <= 50MHz，固定 20MHz 起步 */
    sfud_spi1_frequency_set(20000000U);

    /* 5. 挂接 SFUD 回调（单线程裸机，无需 lock/unlock） */
    s_flash = flash;
    flash->spi.wr = spi_write_read;
    flash->spi.user_data = (void *)HPM_SPI1;
    flash->retry.times = 10000;
    flash->retry.delay = NULL; /* wait_busy 轮询状态寄存器，无需延时 */

    return SFUD_SUCCESS;
}

static char log_buf[256];

/**
 * SFUD debug 日志（SFUD_DEBUG_MODE 启用）
 */
void sfud_log_debug(const char *file, const long line, const char *format, ...)
{
    va_list args;

    va_start(args, format);
    vsnprintf(log_buf, sizeof(log_buf), format, args);
    va_end(args);
    log_d("%s:%ld %s", file, line, log_buf);
}

/**
 * SFUD 常规日志
 */
void sfud_log_info(const char *format, ...)
{
    va_list args;

    va_start(args, format);
    vsnprintf(log_buf, sizeof(log_buf), format, args);
    va_end(args);
    log_i("%s", log_buf);
}

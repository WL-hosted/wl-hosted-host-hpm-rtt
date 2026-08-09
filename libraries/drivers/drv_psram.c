#include "drv_psram.h"
#include <rtconfig.h>

#ifdef BSP_USING_PSRAM

#include "board.h"
#include "hpm_gpio_drv.h"
#include "hpm_l1c_drv.h"
#include "hpm_romapi.h"
#include "pinmux.h"

#include <rtthread.h>
#include <ulog.h>

#include <string.h>

#define PSRAM_BASE_ADDRESS       (0x90000000UL)
#define PSRAM_EXPECTED_SIZE      (8UL * 1024UL * 1024UL)
#define PSRAM_EXPECTED_SIZE_KB   (PSRAM_EXPECTED_SIZE / 1024UL)
#define PSRAM_INIT_WAIT_MS       (1U)
#define PSRAM_FREQUENCY_OPTION   (4U)
#define PSRAM_WRITE_SEQUENCE     (1U)
#define PSRAM_APB_SEQUENCE       (1U)

static bool s_psram_ready;

void psram_cache_sync(void) {
    if (l1c_dc_is_enabled()) {
        l1c_dc_flush_all();
    }
}

static void psram_hold_power_up_levels(void) {
    /* The device requires CE# high before the first command. Set CE# first,
     * then keep CLK and all data pins low throughout the power-up wait. */
    HPM_IOC->PAD[IOC_PAD_PB17].FUNC_CTL = IOC_PB17_FUNC_CTL_GPIO_B_17;
    gpio_set_pin_output_with_initial(HPM_GPIO0, GPIO_DO_GPIOB, 17U, 1U);

    HPM_IOC->PAD[IOC_PAD_PB12].FUNC_CTL = IOC_PB12_FUNC_CTL_GPIO_B_12;
    HPM_IOC->PAD[IOC_PAD_PB13].FUNC_CTL = IOC_PB13_FUNC_CTL_GPIO_B_13;
    HPM_IOC->PAD[IOC_PAD_PB14].FUNC_CTL = IOC_PB14_FUNC_CTL_GPIO_B_14;
    HPM_IOC->PAD[IOC_PAD_PB15].FUNC_CTL = IOC_PB15_FUNC_CTL_GPIO_B_15;
    HPM_IOC->PAD[IOC_PAD_PB16].FUNC_CTL = IOC_PB16_FUNC_CTL_GPIO_B_16;
    gpio_set_pin_output_with_initial(HPM_GPIO0, GPIO_DO_GPIOB, 12U, 0U);
    gpio_set_pin_output_with_initial(HPM_GPIO0, GPIO_DO_GPIOB, 13U, 0U);
    gpio_set_pin_output_with_initial(HPM_GPIO0, GPIO_DO_GPIOB, 14U, 0U);
    gpio_set_pin_output_with_initial(HPM_GPIO0, GPIO_DO_GPIOB, 15U, 0U);
    gpio_set_pin_output_with_initial(HPM_GPIO0, GPIO_DO_GPIOB, 16U, 0U);
    board_delay_ms(PSRAM_INIT_WAIT_MS);
}

static bool psram_probe_cache_lines(void) {
    static const uint32_t probe_offsets[] = {
        0x00000000UL,
        PSRAM_EXPECTED_SIZE - HPM_L1C_CACHELINE_SIZE,
    };
    const uint32_t words_per_line = HPM_L1C_CACHELINE_SIZE / sizeof(uint32_t);

    psram_cache_sync();
    for (uint32_t probe = 0; probe < ARRAY_SIZE(probe_offsets); ++probe) {
        volatile uint32_t *const line =
            (volatile uint32_t *)(PSRAM_BASE_ADDRESS + probe_offsets[probe]);
        (void)line[0];
    }

    for (uint32_t probe = 0; probe < ARRAY_SIZE(probe_offsets); ++probe) {
        volatile uint32_t *const line =
            (volatile uint32_t *)(PSRAM_BASE_ADDRESS + probe_offsets[probe]);
        const uint32_t seed = 0x13579BDFU ^ probe_offsets[probe];
        for (uint32_t word = 0; word < words_per_line; ++word) {
            line[word] = seed ^ (word * 0x01010101U);
        }
    }

    psram_cache_sync();

    for (uint32_t probe = 0; probe < ARRAY_SIZE(probe_offsets); ++probe) {
        volatile uint32_t *const line =
            (volatile uint32_t *)(PSRAM_BASE_ADDRESS + probe_offsets[probe]);
        const uint32_t seed = 0x13579BDFU ^ probe_offsets[probe];
        for (uint32_t word = 0; word < words_per_line; ++word) {
            const uint32_t expected = seed ^ (word * 0x01010101U);
            const uint32_t actual = line[word];
            if (actual != expected) {
                log_e("PSRAM probe 0x%08lx word %lu: got=0x%08lx expected=0x%08lx",
                      (unsigned long)(PSRAM_BASE_ADDRESS + probe_offsets[probe]),
                      (unsigned long)word, (unsigned long)actual,
                      (unsigned long)expected);
                return false;
            }
        }
    }
    return true;
}

static hpm_stat_t psram_send_command(uint8_t command, uint8_t pads) {
    xpi_instr_seq_t sequence = {
        .entry = {
            XPI_INSTR_SEQ(XPI_PHASE_CMD_SDR, pads, command,
                          XPI_PHASE_STOP, XPI_1PAD, 0U),
            0U,
            0U,
            0U,
        },
    };
    xpi_xfer_ctx_t transfer;

    hpm_stat_t status = ROM_API_TABLE_ROOT->xpi_driver_if->update_instr_table(
        HPM_XPI1, sequence.entry, PSRAM_APB_SEQUENCE, 1U);
    if (status != status_success) {
        return status;
    }

    memset(&transfer, 0, sizeof(transfer));
    transfer.channel = xpi_xfer_channel_a1;
    transfer.cmd_type = xpi_apb_xfer_type_cmd;
    transfer.seq_idx = PSRAM_APB_SEQUENCE;
    transfer.seq_num = 1U;
    return ROM_API_TABLE_ROOT->xpi_driver_if->transfer_blocking(HPM_XPI1, &transfer);
}

static bool psram_read_spi_id(void) {
    xpi_instr_seq_t sequence = {
        .entry = {
            XPI_INSTR_SEQ(XPI_PHASE_CMD_SDR, XPI_1PAD, 0x9FU,
                          XPI_PHASE_RADDR_SDR, XPI_1PAD, 24U),
            XPI_INSTR_SEQ(XPI_PHASE_READ_SDR, XPI_1PAD, 8U,
                          XPI_PHASE_STOP, XPI_1PAD, 0U),
            0U,
            0U,
        },
    };
    uint32_t id_words[2] = {0U, 0U};
    xpi_xfer_ctx_t transfer;

    hpm_stat_t status = ROM_API_TABLE_ROOT->xpi_driver_if->update_instr_table(
        HPM_XPI1, sequence.entry, PSRAM_APB_SEQUENCE, 1U);
    if (status != status_success) {
        log_e("PSRAM SPI ID sequence install failed: status=%ld", (long)status);
        return false;
    }

    memset(&transfer, 0, sizeof(transfer));
    transfer.channel = xpi_xfer_channel_a1;
    transfer.cmd_type = xpi_apb_xfer_type_read;
    transfer.seq_idx = PSRAM_APB_SEQUENCE;
    transfer.seq_num = 1U;
    transfer.buf = id_words;
    transfer.xfer_size = sizeof(id_words);
    status = ROM_API_TABLE_ROOT->xpi_driver_if->transfer_blocking(HPM_XPI1, &transfer);
    if (status != status_success) {
        log_e("PSRAM SPI ID read failed: status=%ld", (long)status);
        return false;
    }

    const uint8_t *const id = (const uint8_t *)id_words;
    if (id[0] != 0x0DU || id[1] != 0x5DU) {
        log_e("PSRAM SPI ID mismatch: MF=0x%02x KGD=0x%02x expected=0x0d/0x5d",
              id[0], id[1]);
        return false;
    }
    return true;
}

static bool psram_enter_known_qpi_mode(void) {
    /* ESP-PSRAM64H powers up in SPI mode. Send F5 as a best-effort exit if a
     * previous initialization left it in QPI mode. Reset must be issued as
     * the adjacent 66/99 pair, after which 35 switches from SPI to QPI. */
    (void)psram_send_command(0xF5U, XPI_4PADS);

    hpm_stat_t status = psram_send_command(0x66U, XPI_1PAD);
    if (status == status_success) {
        status = psram_send_command(0x99U, XPI_1PAD);
    }
    if (status != status_success) {
        log_e("PSRAM SPI reset command failed: status=%ld", (long)status);
        return false;
    }

    board_delay_ms(PSRAM_INIT_WAIT_MS);
    if (!psram_read_spi_id()) {
        return false;
    }
    status = psram_send_command(0x35U, XPI_1PAD);
    if (status != status_success) {
        log_e("PSRAM enter-QPI command failed: status=%ld", (long)status);
        return false;
    }

    return true;
}

int psram_init(void) {
    xpi_ram_config_option_t option;
    xpi_ram_config_t config;

    s_psram_ready = false;
    memset(&option, 0, sizeof(option));
    memset(&config, 0, sizeof(config));

    psram_hold_power_up_levels();
    init_psram_pins();

    option.header.tag = XPI_RAM_CFG_OPTION_TAG;
    option.header.words = 2U;
    option.header.instance = 1U;
    option.option0.freq_opt = PSRAM_FREQUENCY_OPTION;
    option.option0.ram_size = PSRAM_EXPECTED_SIZE / (1024UL * 1024UL);
    option.option0.pin_group = xpi_io_1st_group;
    option.option0.read_dummy_cycles = 0U;
    option.option0.write_dummy_cycles = 0U;
    option.option0.probe_type = xpi_ram_type_apmemory_x4;
    option.option1.max_cs_low_time = xpi_ram_default_max_cs_low_time_us;
    option.option1.pin_group_sel = 0U;
    option.option1.channel = 0U;

    hpm_stat_t status = rom_xpi_ram_get_config(HPM_XPI1, &config, &option);
    if (status != status_success) {
        log_e("PSRAM ROM get_config failed: %ld", (long)status);
        return -RT_ERROR;
    }

    if (config.device_info.size_in_kbytes != PSRAM_EXPECTED_SIZE_KB ||
        config.device_info.data_pads != xpi_ram_qpi) {
        log_e("PSRAM ROM config mismatch: size=%lu KiB pads=%u",
              (unsigned long)config.device_info.size_in_kbytes,
              config.device_info.data_pads);
        return -RT_ERROR;
    }

    /* The HPM6360 ROM APMemory X4 template defaults to DQS loopback even
     * though this board's quad PSRAM has no DQS connection. Keep the ROM's
     * protocol/timing configuration, but explicitly select the XPI internal
     * sampling loopback before initialization. */
    if (config.rxclk_src != xpi_rxclksrc_internal_loopback ||
        config.rxclk_src_for_init != xpi_rxclksrc_internal_loopback) {
        log_w("PSRAM override ROM RX clock: run=%u init=%u -> internal loopback",
              config.rxclk_src, config.rxclk_src_for_init);
        config.rxclk_src = xpi_rxclksrc_internal_loopback;
        config.rxclk_src_for_init = xpi_rxclksrc_internal_loopback;
    }

    status = rom_xpi_ram_init(HPM_XPI1, &config);
    if (status != status_success) {
        log_e("PSRAM ROM init failed: %ld", (long)status);
        return -RT_ERROR;
    }

    if (!psram_enter_known_qpi_mode()) {
        return -RT_ERROR;
    }
    status = ROM_API_TABLE_ROOT->xpi_driver_if->update_instr_table(
        HPM_XPI1, config.instr_set[PSRAM_WRITE_SEQUENCE].entry,
        PSRAM_APB_SEQUENCE, 1U);
    if (status != status_success) {
        log_e("PSRAM AHB write-sequence restore failed: %ld", (long)status);
        return -RT_ERROR;
    }

    if (!psram_probe_cache_lines()) {
        return -RT_ERROR;
    }

    s_psram_ready = true;
    log_i("PSRAM ready: XPI1 QPI 80 MHz, base=0x%08lx, size=%lu KiB, internal RX clock",
          (unsigned long)PSRAM_BASE_ADDRESS, (unsigned long)PSRAM_EXPECTED_SIZE_KB);
    return RT_EOK;
}

bool psram_is_ready(void) {
    return s_psram_ready;
}

uintptr_t psram_get_base(void) {
    return PSRAM_BASE_ADDRESS;
}

size_t psram_get_size(void) {
    return PSRAM_EXPECTED_SIZE;
}

INIT_DEVICE_EXPORT(psram_init);

#endif /* BSP_USING_PSRAM */

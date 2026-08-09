cmake_minimum_required(VERSION 3.20)

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR riscv)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(HPM_RISCV_TOOLCHAIN_BIN "/Users/kai/DevTools/xpack-riscv-none-elf-gcc-15.2.0-1/bin" CACHE PATH "RISC-V GCC bin directory")
set(CMAKE_C_COMPILER "${HPM_RISCV_TOOLCHAIN_BIN}/riscv-none-elf-gcc")
set(CMAKE_ASM_COMPILER "${HPM_RISCV_TOOLCHAIN_BIN}/riscv-none-elf-gcc")
set(CMAKE_OBJCOPY "${HPM_RISCV_TOOLCHAIN_BIN}/riscv-none-elf-objcopy")
set(CMAKE_SIZE "${HPM_RISCV_TOOLCHAIN_BIN}/riscv-none-elf-size")

project(hpm_bootloader C ASM)

set(PKGS_DIR ${CMAKE_CURRENT_LIST_DIR}/../packages)
set(SDK ${PKGS_DIR}/hpm_sdk-v1.10.0)
set(SOC ${SDK}/soc/HPM6300/HPM6360)
set(PART_TABLE_DIR ${CMAKE_CURRENT_LIST_DIR}/../part_table)

# 独立构建 BL 时也生成 FAL 分区表头与分区图（App 侧 custom.cmake 的 gen_part_table 目标同样生成，幂等）
execute_process(COMMAND python3 ${CMAKE_CURRENT_LIST_DIR}/../tools/gen_part_table.py
    ${PART_TABLE_DIR}/part_table.md
    ${PART_TABLE_DIR}/part_table.h
    ${PART_TABLE_DIR}/part_table_map.png
    WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/..)

add_compile_definitions(
    HPM6360
    FLASH_XIP=1
    USE_NONVECTOR_MODE=0
    # DISABLE_WDG=1
)
add_compile_options(
    -mcmodel=medlow -march=rv32imac_zicsr_zifencei -mabi=ilp32 
    -ffunction-sections -fdata-sections -fno-common 
    -Os -g
)
add_link_options(
    -mcmodel=medlow -march=rv32imac_zicsr_zifencei -mabi=ilp32 
    --specs=nano.specs 
    -nostartfiles -Wl,--gc-sections 
    -T${CMAKE_CURRENT_LIST_DIR}/linker/flash_xip.ld -Wl,--print-memory-usage
)

add_executable(hpm_bootloader.elf
    src/main.c 
    src/board_init.c 
    msc_bootuf2.c 
    bootuf2/bootuf2.c 
    port/fal/fal_flash_port.c 
    port/elog/elog_port.c 
    src/bl_state.c 
    startup/syscalls.c
    ${CMAKE_CURRENT_LIST_DIR}/port/sfud/sfud_port.c
    ${PKGS_DIR}/SFUD/sfud/src/sfud.c
    ${PKGS_DIR}/SFUD/sfud/src/sfud_sfdp.c
    ${SOC}/boot/hpm_bootheader.c 
    ${SOC}/toolchains/reset.c 
    ${SOC}/toolchains/trap.c 
    ${SOC}/toolchains/gcc/start.S
    ${SDK}/arch/riscv/l1c/hpm_l1c_drv.c
    ${SDK}/utils/hpm_sbrk.c
    ${SDK}/drivers/src/hpm_gpio_drv.c 
    ${SDK}/drivers/src/hpm_wdg_drv.c 
    ${SDK}/drivers/src/hpm_usb_drv.c 
    ${SDK}/drivers/src/hpm_spi_drv.c
    ${SDK}/drivers/src/hpm_pllctlv2_drv.c
    ${SDK}/drivers/src/hpm_pcfg_drv.c
    ${SDK}/drivers/src/hpm_pmp_drv.c
    ${SDK}/drivers/src/hpm_uart_drv.c
    ${SDK}/components/debug_console/hpm_debug_console.c
    ${SDK}/components/usb/device/hpm_usb_device.c
    ${SOC}/system.c 
    ${SOC}/hpm_clock_drv.c 
    ${SOC}/hpm_sysctl_drv.c 
    ${SOC}/hpm_otp_drv.c
    ${PKGS_DIR}/fal/src/fal.c
    ${PKGS_DIR}/fal/src/fal_flash.c
    ${PKGS_DIR}/fal/src/fal_partition.c
    ${PKGS_DIR}/flashdb/src/fdb.c
    ${PKGS_DIR}/flashdb/src/fdb_kvdb.c
    ${PKGS_DIR}/flashdb/src/fdb_utils.c
    ${PKGS_DIR}/easylogger/easylogger/src/elog.c
    ${PKGS_DIR}/easylogger/easylogger/src/elog_utils.c
    ${PKGS_DIR}/cherryusb/core/usbd_core.c
    ${PKGS_DIR}/cherryusb/class/msc/usbd_msc.c
    ${PKGS_DIR}/cherryusb/port/hpm/usb_dc_hpm.c
)

target_include_directories(hpm_bootloader.elf PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/include 
    ${CMAKE_CURRENT_LIST_DIR}/config 
    ${CMAKE_CURRENT_LIST_DIR}/bootuf2
    ${CMAKE_CURRENT_LIST_DIR}/port/elog
    ${CMAKE_CURRENT_LIST_DIR}/port/sfud
    ${PKGS_DIR}/SFUD/sfud/inc
    ${PART_TABLE_DIR}
    ${SDK}/drivers/inc 
    ${SOC} 
    ${SOC}/boot 
    ${SDK}/soc/HPM6300/ip 
    ${SDK}/arch 
    ${SDK}/arch/riscv/l1c
    ${SDK}/components/usb 
    ${SDK}/components/usb/device
    ${SDK}/components/debug_console
    ${PKGS_DIR}/fal/inc 
    ${PKGS_DIR}/flashdb/inc
    ${PKGS_DIR}/easylogger/easylogger/inc
    ${PKGS_DIR}/cherryusb/core 
    ${PKGS_DIR}/cherryusb/common
    ${PKGS_DIR}/cherryusb/class/msc 
    ${SOC}/toolchains
)

target_link_libraries(hpm_bootloader.elf PRIVATE c m gcc)
add_custom_command(TARGET hpm_bootloader.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary hpm_bootloader.elf hpm_bootloader.bin
    COMMAND ${CMAKE_OBJCOPY} -O ihex hpm_bootloader.elf hpm_bootloader.hex
    COMMAND ${CMAKE_SIZE} hpm_bootloader.elf
)

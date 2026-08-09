# ===== 构建定制（由生成的 CMakeLists.txt include） =====

# RT-Thread 5.2.0 的 DHCPD 源文件漏掉 string.h。GCC 15 默认拒绝隐式函数
# 声明；这里只对该上游源文件强制包含标准头，不修改 RT-Thread submodule。
# TODO: RT-Thread 更新到包含该修复的版本后移除这个兼容选项。
set_source_files_properties(
    ${CMAKE_CURRENT_SOURCE_DIR}/rt-thread/components/net/lwip-dhcpd/dhcp_server_raw.c
    PROPERTIES COMPILE_OPTIONS "-include;string.h"
)

include(ExternalProject)

# gen_part_table：由 part_table/part_table.md 生成 BL 的 FAL 分区表头
# （bootloader/config/fal_cfg.h #include 的 part_table/part_table.h）
add_custom_target(gen_part_table
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_part_table.py
        ${CMAKE_CURRENT_SOURCE_DIR}/part_table/part_table.md
        ${CMAKE_CURRENT_SOURCE_DIR}/part_table/part_table.h
        ${CMAKE_CURRENT_SOURCE_DIR}/part_table/part_table_map.png
    COMMENT "Generating part_table/part_table.h + partition map PNG from part_table.md"
    VERBATIM
)

# build_bootloader：以 ExternalProject 引入独立 BL 工程（配置 + 构建）
ExternalProject_Add(build_bootloader
    PREFIX          ${CMAKE_CURRENT_BINARY_DIR}/bootloader_external   # 临时/stamp 文件留在 App 构建树
    SOURCE_DIR      ${CMAKE_CURRENT_SOURCE_DIR}/bootloader
    BINARY_DIR      ${CMAKE_CURRENT_SOURCE_DIR}/bootloader/build      # 保持 README 文档化产物目录
    DOWNLOAD_COMMAND ""      # 本地源码目录，禁用下载步骤
    UPDATE_COMMAND   ""      # 不执行拉取/更新
    INSTALL_COMMAND  ""      # BL 无 install 步骤
    BUILD_ALWAYS     1       # 每次构建都重新配置 + 编译，保证 ELF 最新
    EXCLUDE_FROM_ALL 1       # 默认 make 不构建 BL，按需触发
    DEPENDS gen_part_table  # 构建 BL 前先保证分区表头最新
)

# download_bootloader：OpenOCD 烧录 BL（依赖 build_bootloader）
# 以下 OpenOCD 相关变量为全局工具配置，不加 bootloader/bl 前缀
set(OPENOCD /Users/kai/DevTools/hpm-openocd-0.12.0-7/bin/openocd )
# 总配置文件在仓库根目录，内部 source 探针/SoC/板级三份 cfg
set(OPENOCD_CFG ${CMAKE_CURRENT_SOURCE_DIR}/openocd.cfg )

# mergehex-rs：BL + App Intel HEX 合并工具（Nordic mergehex CLI 兼容）
set(MERGEHEX_RS /Users/kai/DevTools/mergehex/mergehex-rs CACHE FILEPATH "mergehex-rs binary")

add_custom_target(download_bootloader
    DEPENDS build_bootloader
    # 用 Intel HEX 烧录：记录自带 32 位地址（最低 LMA .nor_cfg_option
    # @ 0x80000400），无需手动指定基址，也不会像 bin 那样在烧到
    # 0x80000000 时把整个镜像前移 0x400 导致 boot ROM 读不到 FCB
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/tools/openocd_program.py
        --openocd ${OPENOCD}
        --config ${OPENOCD_CFG}
        --image ${CMAKE_CURRENT_SOURCE_DIR}/bootloader/build/hpm_bootloader.hex
    COMMENT "Downloading bootloader via OpenOCD"
    VERBATIM
)

# package_app_uf2：打包 App bin（构建目录 ${CMAKE_PROJECT_NAME}.bin）为 UF2 升级镜像，
# 拖入 BL 的 UF2 盘即可升级 App
add_custom_target(package_app_uf2
    DEPENDS ${CMAKE_PROJECT_NAME}.elf
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/tools/uf2_pack.py
        ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_PROJECT_NAME}.bin
        ${CMAKE_CURRENT_BINARY_DIR}/hpm_app.uf2
    COMMENT "Packaging application as UF2 (hpm_app.uf2)"
    VERBATIM
)

# package_bl_app_hex：BL + App 合并 Intel HEX（整片烧录用，
# 依赖 build_bootloader 保证 BL HEX 最新）
add_custom_target(package_bl_app_hex
    DEPENDS build_bootloader ${CMAKE_PROJECT_NAME}.elf
    COMMAND ${CMAKE_OBJCOPY} -O ihex ${CMAKE_PROJECT_NAME}.elf
        ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_PROJECT_NAME}.hex
    COMMAND ${MERGEHEX_RS} -i
        ${CMAKE_CURRENT_SOURCE_DIR}/bootloader/build/hpm_bootloader.hex
        -i ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_PROJECT_NAME}.hex
        -o ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_PROJECT_NAME}_bl_app.hex
    COMMENT "Merging bootloader + application HEX (${CMAKE_PROJECT_NAME}_bl_app.hex)"
    VERBATIM
)

# download_bl_app：OpenOCD 整片烧录 BL+App 合并 HEX（依赖 package_bl_app_hex，
# 后者已保证 BL/App 均为最新构建）
add_custom_target(download_bl_app
    DEPENDS package_bl_app_hex
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/tools/openocd_program.py
        --openocd ${OPENOCD}
        --config ${OPENOCD_CFG}
        --image ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_PROJECT_NAME}_bl_app.hex
    COMMENT "Downloading bootloader + application (${CMAKE_PROJECT_NAME}_bl_app.hex) via OpenOCD"
    VERBATIM
)

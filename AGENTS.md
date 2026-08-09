# HPM 6360 wl host（RT-Thread）协作说明

## 项目定位与当前状态

- 本仓库目标平台为 HPM6360（RISC-V），RT-Thread 为基础运行环境；当前工程由
  HPM6360 BSP（Bootloader + App 结构）演进而来，作为 **wl host
  （无线 host）** 新工程起点：App 业务已全部清理，仅保留基础运行、存储（XPI0
  onchip flash + 外部 SPI flash/littlefs）与引导/升级框架。
- 板级硬件、外设和业务功能尚未适配 wl host 目标板。迁移时应先抽取与硬件无关的
  业务逻辑和协议，再为实际硬件重新实现板级适配层。

## 目标目录与工程边界

最终仓库包含两个彼此独立、可单独构建的工程（HPM 侧 Bootloader 与 App）：

```text
.
├── bootloader/             # 独立 BL 工程（唯一入口：bootloader/CMakeLists.txt）
├── applications/           # App 业务入口
│   ├── components/         # 可复用业务功能组件
├── key/                    # 密钥、签名及镜像认证相关接口和材料
├── tools/                  # BL/App 构建、打包和开发辅助工具
├── test/                   # BL/App 单元、集成与板级验证测试
├── board/                  # App 的板级适配、引脚、外设、Flash/FAL 配置
├── libraries/              # 本项目的驱动封装和硬件抽象
├── part_table/             # 分区定义源（part_table.md）与生成的分区表头（part_table.h）
├── startup/                # HPM6360 启动代码
├── packages/               # 固定版本的第三方依赖与 HPM SDK（无 submodule）
├── rt-thread/              # RT-Thread 源码
├── hpm_sdk_samples/        # HPM SDK 示例代码软链接（参考用，不直接构建）
├── custom.cmake            # App CMake 后处理脚本（由生成的 CMakeLists.txt include）
└── CMakeLists.txt          # App 工程入口（由 scons 生成，勿手改）
```

- 根目录 `CMakeLists.txt` 始终是 **App 工程**入口，由
  `scons --target=cmake --project-name="rtthread"` 生成，属生成物，禁止手改
  （重新生成会覆盖手改内容）。对 App CMake 的任何定制——新增源文件、头文件路径、
  工具链路径、链接脚本、编译选项等——应通过 scons 相关文件（`rtconfig.py`、各级
  `SConscript`、menuconfig 生成的 `rtconfig.h`）实现后再重新生成；工具链路径等环境
  相关配置在 `rtconfig.py` 中做成可配置项，避免提交个人绝对路径。`custom.cmake`
  由生成的 CMakeLists.txt 自动 include，仅用于承载后处理脚本（如构建后生成 BIN、
  镜像打包、拷贝产物等），不得用于修改编译选项或源文件集合。不要将 BL 源码、目标
  文件或构建规则混入根 App 工程。
- `bootloader/` 必须自带 `CMakeLists.txt`、启动代码/链接脚本、配置头文件和必要源
  文件；其构建目录应位于 `bootloader/build/`（或其他位于 `bootloader/` 下的目录）。
- Bootloader 可按需引用仓库 `packages/` 中的库（特别是 HPM SDK）作为源码或头文件
  依赖，但依赖必须在 `bootloader/CMakeLists.txt` 中显式列出，并与 App 分别构建；
  不要复制 SDK 源码到 BL，也不要将 App 私有源码（`board/`、`applications/`、
  `libraries/` 等）、App 构建产物或 App 专属业务组件作为 BL 依赖。`board/` 仅服务
  于 App，Bootloader 一律不得引用。
- App 与 BL 不直接互相包含私有源文件。若需要共享镜像头、版本、分区或升级协议，
  放在一个明确、最小化且与硬件无关的公共头文件目录；任何 ABI/镜像格式变更必须
  同时更新两端。
- 不要在未确定实际 Flash 容量、擦除粒度、启动 ROM 行为和量产烧录流程前，猜测
  分区地址或覆盖现有链接脚本。

### 项目目录职责

- `applications/components/`：按功能域放置业务组件。组件应暴露稳定接口，不依赖
  其他组件的私有实现；硬件访问经 `board/` 或 `libraries/` 的接口完成。每个组件
  一个目录并自带 `SConscript`（`DefineGroup('<Name>', …)`），由
  `applications/components/SConscript` 自动发现纳入构建；对外接口经组件头文件暴露，
  供 C 代码调用的入口使用 `extern "C"`。
- 新增的板上 MSH 测试/诊断命令统一放在 `applications/cmd_test.cpp/.hpp`；
  `applications/SConscript` 同时纳入顶层 C 与 C++ 源文件。
- `board/`：App 专用板级适配（引脚、时钟、外设、链接脚本、Flash/FAL 访问等），
  仅供 App 引用；Bootloader 不得引用 `board/` 下任何文件，BL 所需同类能力在
  `bootloader/` 内自建或引用 `packages/`。
- `libraries/`：存放可复用的驱动封装与硬件抽象，驱动源码在 `libraries/drivers/`
  （按 `BSP_USING_*` 配置经 SConscript 纳入构建）。修改 `libraries/drivers/` 下
  任何驱动——新增、修复或板级适配——必须同步在 `libraries/patch说明.md` 中更新
  对应驱动条目，并与代码同一提交；新增驱动时先补文档再合入。
- `key/`：存放 BL/App 共用的密钥管理、签名/验签和镜像认证接口、密钥格式说明及
  项目所需的密钥材料。该目录中的密钥允许提交到仓库；新增、替换或撤销密钥时，
  必须同步说明用途、适用镜像/环境、密钥标识和对 Bootloader/App 验签兼容性的影响。
- `tools/`：存放服务于 BL 和 App 的可重复执行工具，例如镜像打包、签名、版本信息
  生成及格式校验。工具须明确输入、输出和目标分区，默认不执行烧录或整片擦除。
- `test/`：仅存放 BL/App 的主机侧单元测试、测试工具、固件集成/板级验证记录和
  说明。不在该目录添加会被根 SCons 自动发现的 `SConscript`；其中源码禁止链接进
  App 或 Bootloader。板上 MSH 测试命令属于 App 源码，统一维护在
  `applications/cmd_test.cpp/.hpp`。
- `hpm_sdk_samples/`：指向 HPM SDK 示例代码目录的符号链接（当前目标：
  `/Volumes/aigo_1t/DevPkgs/hpm/hpm_sdk_v1.10/samples`），仅作为外设用法和 SDK API
  的本地参考。禁止直接修改该链接目标下的源码，也不得将其源文件直接加入 App 或
  Bootloader 的构建。若需复用示例逻辑，应先抽取到 `libraries/drivers/` 或
  `board/` 中并按本板硬件重新适配，同时记录参考的 SDK 示例路径和版本。

## 启动、镜像与升级约束

- 现有 `board/linker_scripts/gcc/flash_rtt.ld` 为 BSP 默认 XPI0 启动布局：配置区
  从 `0x80000000 + 0x400` 开始、Boot Header 从 `+0x1000` 开始、App 负载从
  `+0x3000` 开始。这仅是当前 App 的基线，尚不是最终 BL/App 分区方案。
- 引入 Bootloader 后，必须由一份可审查的分区定义统一约束：BL、主 App、备用 App
  （如启用 OTA）、镜像元数据/状态区、参数或日志区。链接脚本、烧录脚本、升级打包
  脚本和文档必须使用同一组地址与大小。分区定义源统一维护在 `part_table/part_table.md`。
- FAL 分区表仅在 Bootloader 中保存（即上述分区定义在 Flash 中的落盘形式）。
  `bootloader/config/fal_cfg.h` 经 `#include "part_table.h"` 引入
  `part_table/part_table.h`（由 `tools/gen_part_table.py` 从 `part_table/part_table.md`
  生成，生成物勿手改；同脚本按 flash 分组校验分区不重叠，并导出分区图
  `part_table/part_table_map.png`，PNG 依赖 matplotlib，未安装时仅告警跳过）。
  BL 链接脚本 `bootloader/linker/flash_xip.ld` 将 `FalPartTable` 段固定在 XPI0
  `0x80003000`（`.fal_tabel` 区，紧跟 boot header 之后、BL 代码区 `0x80003200`
  之前），表结构即 `fal_partition` 数组。App 不内置静态 FAL 分区表，启动后按该
  地址扫描读取 BL 保存的分区表，再据此挂载 FAL 分区。修改分区定义时改
  `part_table/part_table.md` 一处即可（BL 重建时自动重新生成）。flash 基址
  `FLASH_BASE` 由 BL 链接脚本 `bootloader/linker/flash_xip.ld` 的 `ORIGIN(XPI0)`
  导出（`__flash_base__`，仅运行期右值），flash 实际容量由 ROM API 探测，分区偏移
  经 FAL 表运行时获取；`bootloader/bootuf2/bootuf2_config.h` 仅保留 UF2 磁盘配置
  （`CONFIG_BOOTUF2_*`）。
- 在修改启动地址或镜像头前，先核实 HPM6360 ROM 启动要求以及 SDK 中
  `hpm_bootheader.c`、启动汇编和链接脚本的配合；改变 App 的链接地址后，要验证
  向量表、数据加载地址和 XIP/RAM 拷贝均正确。
- 升级流程至少应定义并验证：镜像完整性校验、版本/兼容性检查、断电恢复、回滚
  策略、写入时禁止执行被擦写 Flash 中的代码，以及失败后的可诊断日志。
- 不要将未经验证的 App 直接写入 Bootloader 或保留分区；烧录、擦除和量产脚本
  必须显式限定地址范围。

## 代码与板级适配原则

- 先建立实际板子的引脚表、供电/复位关系、时钟源、Flash 型号与连接、UART 调试口，
  再修改 `board/pinmux.c`、`board/board.c`、驱动和 Kconfig。每项配置应保留硬件
  依据（原理图页码、丝印或测试记录）。
- `board/` 放 App 专用板级代码；`libraries/drivers/` 放可复用的驱动封装。业务模块
  不得直接散落寄存器访问或硬编码 GPIO 编号。
- 当前 `board/fal_flash_port.c` 操作 XPI0 NOR Flash（onchip），`applications/main.c`
  经 SFUD 探测外部 SPI flash（`norflash0`）并挂 littlefs 到 `/`。App 侧 FAL 不内置
  静态分区表：启动时先扫描读取 Bootloader 保存的分区表，再挂载 FAL 分区。flash
  基址 `FLASH_BASE` 由 App 链接脚本（`board/linker_scripts/gcc/flash_rtt_uf2.ld` /
  `flash_rtt.ld`）导出（`__flash_base__`，仅运行期右值，不可用于静态初始化），
  flash 实际容量由 FAL init 时 ROM API 探测；App 加载偏移与分区定义由 BL 侧
  `part_table/part_table.md` 约束。变更存储布局、OTA 或参数保存前，先确认分区表
  来源、缓存维护、扇区/块擦除大小和 XIP 执行限制。
- PSRAM 驱动（`libraries/drivers/drv_psram.c`）默认**不启用**（`BSP_USING_PSRAM`
  关闭）；如需启用，按实际硬件确认 XPI1 引脚（`board/pinmux.c` `init_psram_pins`）
  与容量后再打开。
- 默认编译选项采用 `rv32imac_zicsr_zifencei`、`ilp32`、HPM6360 SDK 和
  `FLASH_XIP=1`。新增代码和库必须与此 ABI、异常/RTTI 策略及链接内存区兼容。
- 保持 C/C++ 接口边界清楚：跨语言头文件使用 `extern "C"`；中断、启动和底层驱动
  优先保持小而确定，避免动态分配和不可控阻塞。
- App 业务代码可使用 C++，标准为 **gnu++20**（`rtconfig.py` 的 `CXXFLAGS` 设定），
  并受 `-fno-exceptions -fno-rtti` 约束（禁止异常与 RTTI）。已启用
  `RT_USING_CPLUSPLUS11`（连带 pthreads/POSIX FS/STDIO），`std::mutex`/
  `std::lock_guard`/`std::thread` 等使用 RT-Thread cpp11 封装
  （`rt-thread/components/libc/cplusplus/cpp11/gcc/`，经 -I 路径覆盖 libstdc++
  同名头，底层为 pthread_mutex）；线程类优先使用 `rtthread::Thread`
  （`cxx_thread.h`）；不要自己向 `namespace std` 添加实现。
- 日志输出偏好使用 EasyLogger/RT-Thread ulog 的 `log_x` 简写宏（`log_a`/`log_e`/
  `log_w`/`log_i`/`log_d`/`log_v`），不使用 `elog_x`/`ulog_x`/`printf`/`rt_kprintf`；
  `elog_init`、`elog_set_fmt`、`elog_start` 等库初始化接口除外。Bootloader 与 App
  （RT-Thread 环境）统一遵守此约定。

## 构建、产物与验证

- App 的 CMake 工程由 `scons --target=cmake --project-name="rtthread"` 生成，
  产物为 `rtthread.elf` 和构建后命令生成的 `rtthread.bin`。构建参数（源文件、
  头文件、工具链、编译选项、链接脚本）经 scons 侧（`rtconfig.py`、`SConscript`
  等）修改后重新生成，不在生成物或 custom.cmake 中改动；`custom.cmake` 只放后
  处理脚本（如 BIN 生成、镜像打包），不承载编译选项或源文件修改。修改 `.config`
  （menuconfig 或手改）后必须执行 `scons --pyconfig-silent` 重新生成
  `rtconfig.h`——普通 `scons` 构建不会自动重新生成，配置不生效会导致编译/链接
  结果与 `.config` 不一致。
- 所有 `.o` 必须落在 `build/` 下，源码树不得出现构建中间产物。SCons 的 variant
  映射（`PrepareBuilding` 以 `variant_dir='build'` 调根 `SConscript`）只沿**相对
  路径**的 SConscript 调用链传播：各级 SConscript 互相调用时禁止使用
  `os.path.join(cwd, …)` 之类的绝对路径（绝对路径那一跳会脱离 variant 上下文，
  使其下游对象文件落到源文件旁边）。参照 `packages/SConscript` 的写法：
  `SConscript(os.path.join(item, 'SConscript'))`。
- Bootloader 必须使用独立的目标名、ELF、BIN 和 map 文件，不能覆盖 App 的
  `rtthread.bin`、`rtthread.map` 或构建目录。BL 产物为 `hpm_bootloader.bin/.hex`。
- 修改启动代码、链接脚本、Flash、时钟、中断、DMA 或分区后，至少执行一次干净
  构建，并检查 map 文件中的镜像地址、各内存区占用和生成 BIN 的大小。
- 涉及真实硬件时，按“串口启动日志 → 基础 GPIO/UART → 外部 Flash 读写 →
  Bootloader 跳转 App → 升级/回滚”的顺序验证；每一步记录板卡版本、烧录方式和
  结果。
- `build*/`、ELF、BIN、map、IDE 缓存和临时打包文件均为生成物，不应提交，除非
  任务明确要求交付已发布的固件。

## 变更纪律

- 不修改 `packages/hpm_sdk-v1.10.0/` 或 `rt-thread/` 的第三方源码来承载本板定制；
  优先在 `board/`、`libraries/` 或项目自有模块中实现。确需修改上游代码时，单独
  说明原因、版本影响和回迁方案。
- 避免大范围格式化、无关重构或覆盖用户已有变更。变更 Kconfig、链接脚本、启动
  文件和 Flash 操作时，说明对 BL/App 的影响。
- 第三方库的配置头文件倾向于拷贝库自带的模板到项目配置目录后修改，不直接改动
  `packages/` 下库内的原始模板，不直接生成配置头文件；禁用模板中的某项功能时用
  `//` 注释掉对应宏定义而不是删除，并保留模板原有注释，便于与上游模板对比差异
  和后续回迁。
- 新增业务能力时，以参考工程的模块职责和通信协议为准，但根据 HPM 外设能力重新
  设计适配层，并补充最小可复现的构建或硬件验证记录。

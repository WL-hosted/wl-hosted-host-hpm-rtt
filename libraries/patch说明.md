# libraries/drivers 补丁说明

> 依据 `AGENTS.md`：修改 `libraries/drivers/` 下任何驱动（新增、修复或板级适配）时，必须在此追加对应补丁条目（接口、硬件依据、变更记录），并与代码同一提交；新增驱动先补文档再合入。

本文件仅记录相对 HPM6300EVK BSP 主线的补丁；未改动过的驱动不罗列，也不能视为本板已验证能力（见 `AGENTS.md`「项目定位与当前状态」）。

## 补丁列表

### [pending] 修复 drv_sdio 并适配 ESP32-C5 SDIO WLAN（2026-08-09）

- 涉及文件：`libraries/drivers/drv_sdio.c`、`board/wlh_board.c`、`board/pinmux.c`。
- 改动内容：修复 signal-voltage 参数误用、CMD52 空数据解引用、CMD53 byte/block count 被错误改写、传输超时映射、IRQ semaphore 判空、ADMA bounce-buffer 长度边界以及 cache maintenance 使用 DMA system address 的错误；去除逐命令 INFO 日志。保留 16 KiB noncacheable bounce buffer，RT-Thread SDIO worker 串行调用 CMD52/CMD53。增加弱 `rt_hw_sdxc_prepare_device()` hook，在 SDXC 枚举前由 HPM BSP 以 PA14 开漏低 100 ms、释放 1500 ms 对 ESP32-C5 冷复位。
- 硬件依据：SDXC0 使用 PA11=CLK、PA10=CMD、PA12=D0、PA13=D1、PA08=D2、PA09=D3；PA14 是低有效 ESP_EN，外部上拉，禁止作为 card-detect 或推挽输出高。总线为 3.3 V，初始化约 375 kHz，协商 4-bit high-speed 后最高 50 MHz。

### [17240cd] 修复 drv_uart_v2 RX DMA 与 Serial V2 ping 缓冲协议错配（2026-08-07）

- 涉及文件：`libraries/drivers/drv_uart_v2.c`
- 改动内容：HPM6360 的 UART 无 SDK 可用的 RX-idle 特性，RX DMA 改为通过 `RT_HW_SERIAL_CTRL_GET_DMA_PING_BUF` 取得 Serial V2 的 DMA ping 缓冲；首次配置、DMA 完成通知和后续重装统一使用实际 DMA 块长。HPM6360 的 cache line 为 64 字节，因此 ping 块设为 64 字节，并在驱动的对齐重建路径中同时保留、初始化主 RX ring 和 DMA ping ring。由于无 idle 中断，DMA 模式下将 UART FIFO 门限设为 4 字节并启用标准字符超时中断：超时时暂停 DMA，先按 `TRANSIZE` 实际剩余量提交 ping 中的前段，再读出 FIFO 尾字节，最后从 ping 当前写偏移继续 DMA，保证短包及流尾及时可见且不乱序。保留其他 HPM 型号现有的 RX-idle 条件分支。
- 原因/硬件依据：UART6（115200 bps）开启 RX DMA 后，原驱动把 DMA 目标设为 256 字节主 RX ring，却按 Serial V2 DMA 事件上报 256 字节；框架实际 ping 缓冲仅 32 字节，因此报 `The serial buffer (len 32) is overflow` 并将未由 DMA 填充的 ping 内容搬入主 ring，造成接收帧校验失败。HPM6360 的 SDK v1.10 `hpm_soc_ip_feature.h`、SVD 和生成的 UART 寄存器头均未提供 RX-idle 能力，本补丁不依赖 idle 中断。

### [40decf9] 新增 drv_psram：ESP-PSRAM64H @ XPI1 驱动（2026-08-06）

- 涉及文件：`libraries/drivers/drv_psram.c`、`drv_psram.h`、`SConscript`（`BSP_USING_PSRAM`）
- 改动内容：新增 PSRAM 初始化驱动。初始化 3.3 V ESP-PSRAM64H（8 MiB），经 HPM6360 XPI1 以 QPI/SDR 80 MHz 访问（频率选项 4，低于 84 MHz 跨 1 KiB 页限制）。上电时序 CS# 先高、CLK/数据脚保持低电平 1 ms；SPI 复位（F5 尽力退出 QPI → `66/99` 复位对）后经 SPI `9F` 读 ID（校验厂商 `0D`/KGD `5D`，不匹配立即失败），再 SPI `35` 进入 QPI；临时命令复用 APB 指令槽 1，结束后恢复 ROM 的 AHB 写序列（`instr_set[1]`）。校验 ROM 容量（8192 KiB）与位宽（QPI 4 线），对首、尾 cache line 做写-同步-读探测。
- 对外接口：`psram_init()`（`INIT_DEVICE_EXPORT` 自动执行，失败仅标记不可用、不阻塞 App 启动）、`psram_is_ready()`、`psram_get_base()`/`psram_get_size()`（`0x90000000`，8 MiB）、`psram_cache_sync()`（L1D 写回+失效全部缓存行，读改写前必须调用）。
- 硬件依据：映射 `0x90000000-0x907fffff`（HPM6300 内存图 XPI1）；引脚 PB12=D0、PB15=D1、PB13=D2、PB16=D3、PB14=SCLK、PB17=CS0；无 DQS 连接，驱动将 ROM 默认 DQS 环回覆盖为 XPI 内部采样环回（`rxclk_src`/`rxclk_src_for_init`）。
- 使用注意：PSRAM 不在链接布局、`rt_malloc` 或系统堆中；MSH 命令 `psram_test` 为破坏性测试（临时覆盖整片 PSRAM），仅可在无其他代码访问时运行；首批测试 PCB D0/D1 接反（PB12/PB15）曾以飞线修正，量产前须改原理图/PCB。

### [87e9c5c] 修复 drv_uart_v2 TX 中断标志判断（2026-08-05）

- 涉及文件：`libraries/drivers/drv_uart_v2.c`
- 改动内容：发送中断判定由 `irq_id & uart_intr_tx_slot_avail` 改为 `irq_id == uart_intr_id_tx_slot_avail`，避免将其他中断号误判为 TX 可用导致发送中断行为异常。
- 原因/硬件依据：接入 UART6 串口外设时暴露；引脚复用见 `board/pinmux.c`。

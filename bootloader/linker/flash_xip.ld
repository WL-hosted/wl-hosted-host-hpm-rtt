/*
 * Bootloader linker script for HPM6360 (XPI0 NOR, XIP boot).
 *
 * Layout reference: CherryDAP projects/HSLink-Pro/bootloader/flash_xip.ld.
 *
 *   XPI0 0x80000000            BL 分区（128 KiB，App 从 0x80020000 起）
 *     +0x0000  保留（boot ROM 使用）
 *     +0x0400  .nor_cfg_option  —— XPI NOR 配置区（FCB）
 *     +0x1000  .boot_header     —— boot header + fw_info
 *     +0x3000  .fal_tabel       —— FAL 分区表（FalPartTable 段，XIP 直读）
 *     +0x3200  .start/.text/... —— BL 代码与只读数据
 *
 *   ILM 0x00000000             向量表(.vectors)、RAM 函数(.fast)，启动时从 XPI0 拷贝
 *   DLM 0x00080000             .data/.bss/堆/栈
 *   NONCACHEABLE_RAM 0x10E0000 USB DMA 缓冲区（PMA 标记非缓存，见 board_init_pmp）
 */

ENTRY(_start)

_stack_size = 4K;
_heap_size = 8K;

MEMORY
{
    XPI0 (rx) : ORIGIN = 0x80000000, LENGTH = 128K
    ILM  (wx) : ORIGIN = 0x00000000, LENGTH = 128K
    DLM  (w)  : ORIGIN = 0x00080000, LENGTH = 128K
    /* Same layout as the App's NONCACHEABLE_RAM (flash_rtt.ld): 128K at
     * AXI SRAM tail, NAPOT-aligned for the PMA entry in board_init_pmp(). */
    NONCACHEABLE_RAM (wx) : ORIGIN = 0x10E0000, LENGTH = 128K
}

__nor_cfg_option_load_addr__ = ORIGIN(XPI0) + 0x400;
__boot_header_load_addr__ = ORIGIN(XPI0) + 0x1000;
__fal_tabel_addr__ = ORIGIN(XPI0) + 0x3000;
__app_load_addr__ = ORIGIN(XPI0) + 0x3200;
__boot_header_length__ = __boot_header_end__ - __boot_header_start__;
__app_offset__ = __app_load_addr__ - __boot_header_load_addr__;
__flash_base__ = ORIGIN(XPI0);

SECTIONS
{
    /* XPI NOR 配置区：boot ROM 启动时读取，固定位于分区起始 +0x400 */
    .nor_cfg_option __nor_cfg_option_load_addr__ : {
        KEEP(*(.nor_cfg_option))
    } > XPI0

    /* Boot header：tag/version/fw_info，供 boot ROM 解析镜像入口 */
    .boot_header __boot_header_load_addr__ : {
        __boot_header_start__ = .;
        KEEP(*(.boot_header))
        KEEP(*(.fw_info_table))
        KEEP(*(.dc_info))
        __boot_header_end__ = .;
    } > XPI0

    /* FAL 分区表：固定在 Flash 固定偏移，App 侧启动后按此地址读取 */
    .fal_tabel __fal_tabel_addr__ : {
        KEEP(*(FalPartTable))
    } > XPI0

    /* 启动代码入口（_start），紧随分区表 */
    .start __app_load_addr__ : {
        . = ALIGN(8);
        KEEP(*(.start))
    } > XPI0

    /* 向量表：拷贝到 ILM 运行 */
    __vector_load_addr__ = ADDR(.start) + SIZEOF(.start);
    .vectors ORIGIN(ILM) : AT(__vector_load_addr__) {
        . = ALIGN(8);
        __vector_ram_start__ = .;
        KEEP(*(.vector_table))
        KEEP(*(.isr_vector))
        KEEP(*(.isr_vector.*))
        . = ALIGN(8);
        __vector_ram_end__ = .;
    } > ILM

    /* 只读代码与数据：XIP 直接执行 */
    .text (__vector_load_addr__ + SIZEOF(.vectors)) : {
        . = ALIGN(8);
        *(.text)
        *(.text*)
        *(.rodata)
        *(.rodata*)
        *(.srodata*)
        KEEP(*(.init))
        KEEP(*(.fini))
        . = ALIGN(8);
    } > XPI0
    __data_load_addr__ = .;

    /* 可写数据：拷贝到 DLM */
    .data : AT(__data_load_addr__) {
        . = ALIGN(8);
        __data_start__ = .;
        __global_pointer$ = . + 0x800;
        __thread_pointer$ = .;
        *(.data)
        *(.data*)
        *(.sdata)
        *(.sdata*)
        *(.got*)
        . = ALIGN(8);
        PROVIDE(__preinit_array_start = .);
        KEEP(*(.preinit_array))
        PROVIDE(__preinit_array_end = .);
        PROVIDE(__init_array_start = .);
        KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*)))
        KEEP(*(.init_array))
        PROVIDE(__init_array_end = .);
        PROVIDE(__fini_array_start = .);
        KEEP(*(SORT_BY_INIT_PRIORITY(.fini_array.*)))
        KEEP(*(.fini_array))
        PROVIDE(__fini_array_end = .);
        . = ALIGN(8);
        __data_end__ = .;
    } > DLM

    /* RAM 函数：拷贝到 ILM 执行，避免擦写 XIP Flash 时取指 */
    __fast_load_addr__ = __data_load_addr__ + SIZEOF(.data);
    .fast : AT(__fast_load_addr__) {
        . = ALIGN(8);
        __ramfunc_start__ = .;
        *(.fast)
        *(.fast.*)
        . = ALIGN(8);
        __ramfunc_end__ = .;
    } > ILM

    /* TLS 数据段 */
    __tdata_load_addr__ = __data_load_addr__ + SIZEOF(.data) + SIZEOF(.fast);
    .tdata : AT(__tdata_load_addr__) {
        . = ALIGN(8);
        __tdata_start__ = .;
        *(.tdata)
        *(.tdata.*)
        *(.gnu.linkonce.td.*)
        . = ALIGN(8);
        __tdata_end__ = .;
    } > DLM

    .tbss (NOLOAD) : {
        . = ALIGN(8);
        __tbss_start__ = .;
        *(.tbss)
        *(.tbss.*)
        *(.gnu.linkonce.tb.*)
        *(.tcommon)
        . = ALIGN(8);
        __tbss_end__ = .;
    } > DLM

    /* 非缓存 RAM 初始化数据（USB DMA 描述符等） */
    __noncacheable_init_load_addr__ = __tdata_load_addr__ + SIZEOF(.tdata);
    .noncacheable.init : AT(__noncacheable_init_load_addr__) {
        . = ALIGN(8);
        __noncacheable_init_start__ = .;
        KEEP(*(.noncacheable.init))
        KEEP(*(.noncacheable.init.*))
        . = ALIGN(8);
        __noncacheable_init_end__ = .;
    } > NONCACHEABLE_RAM

    /* fast_ram 初始化数据 */
    __fast_ram_init_load_addr__ = __noncacheable_init_load_addr__ + SIZEOF(.noncacheable.init);
    .fast_ram.init : AT(__fast_ram_init_load_addr__) {
        . = ALIGN(8);
        __fast_ram_init_start__ = .;
        KEEP(*(.fast_ram.init))
        KEEP(*(.fast_ram.init.*))
        . = ALIGN(8);
        __fast_ram_init_end__ = .;
    } > DLM

    /* BSS：零初始化 */
    .bss (NOLOAD) : {
        . = ALIGN(8);
        __bss_start__ = .;
        *(.bss)
        *(.bss*)
        *(.sbss*)
        *(COMMON)
        . = ALIGN(8);
        __bss_end__ = .;
        _end = .;
        end = .;
    } > DLM

    /* 非缓存 BSS：零初始化（PMA 非缓存，见 board_init_pmp） */
    .noncacheable (NOLOAD) : {
        . = ALIGN(8);
        KEEP(*(.noncacheable))
        KEEP(*(.noncacheable.non_init))
        KEEP(*(.noncacheable.non_init.*))
        __noncacheable_bss_start__ = .;
        KEEP(*(.noncacheable.bss))
        KEEP(*(.noncacheable.bss.*))
        __noncacheable_bss_end__ = .;
        . = ALIGN(8);
    } > NONCACHEABLE_RAM

    __noncacheable_start__ = ORIGIN(NONCACHEABLE_RAM);
    __noncacheable_end__ = ORIGIN(NONCACHEABLE_RAM) + LENGTH(NONCACHEABLE_RAM);

    /* fast_ram BSS：零初始化 */
    .fast_ram (NOLOAD) : {
        . = ALIGN(8);
        KEEP(*(.fast_ram))
        KEEP(*(.fast_ram.non_init))
        KEEP(*(.fast_ram.non_init.*))
        __fast_ram_bss_start__ = .;
        KEEP(*(.fast_ram.bss))
        KEEP(*(.fast_ram.bss.*))
        __fast_ram_bss_end__ = .;
        . = ALIGN(8);
    } > DLM

    /* 堆与栈 */
    .heap (NOLOAD) : {
        __heap_start__ = .;
        . += _heap_size;
        __heap_end__ = .;
    } > DLM

    .stack (NOLOAD) : {
        . = ALIGN(16);
        __stack_base__ = .;
        . += _stack_size;
        . = ALIGN(16);
        _stack = .;
        _stack_safe = .;
    } > DLM

    /* 固件大小与容量校验 */
    __last_addr__ = __fast_ram_init_load_addr__ + SIZEOF(.fast_ram.init);
    __fw_size__ = __last_addr__ - __app_load_addr__;
    ASSERT((__last_addr__ <= (ORIGIN(XPI0) + LENGTH(XPI0))), "bootloader exceeds 128 KiB XPI0 partition")
}

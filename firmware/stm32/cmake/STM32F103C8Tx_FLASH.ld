/* ============================================================================
 *  STM32F103C8T6 (Blue Pill) 链接脚本 —— 64KB Flash / 20KB RAM
 *  由 STM32CubeMX 官方模板改写，保持 section 布局一致
 *  用法：-TSTM32F103C8Tx_FLASH.ld
 * ============================================================================ */
ENTRY(Reset_Handler)

_estack = ORIGIN(RAM) + LENGTH(RAM);          /* 栈顶 = RAM 末尾（栈向下生长） */

_Min_Heap_Size  = 0x200;                      /* 堆：512 B（用 malloc 才需要） */
_Min_Stack_Size = 0x400;                      /* 栈：1 KB */

MEMORY
{
  RAM   (xrw) : ORIGIN = 0x20000000, LENGTH = 20K    /* C8T6：20KB SRAM */
  FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 64K    /* C8T6：64KB Flash */
}

/* 各段起始/结束符号，供启动文件清零 BSS、复制 DATA 用 */
_sidata = LOADADDR(.data);

SECTIONS
{
  .isr_vector :
  {
    . = ALIGN(4);
    KEEP(*(.isr_vector))       /* 中断向量表必须在最前面 */
    . = ALIGN(4);
  } >FLASH

  .text :
  {
    . = ALIGN(4);
    *(.text)
    *(.text*)
    *(.glue_7)
    *(.glue_7t)
    *(.eh_frame)
    KEEP (*(.init))
    KEEP (*(.fini))
    . = ALIGN(4);
    _etext = .;
  } >FLASH

  .rodata :
  {
    . = ALIGN(4);
    *(.rodata)
    *(.rodata*)
    . = ALIGN(4);
  } >FLASH

  .ARM.extab : { *(.ARM.extab* .gnu.linkonce.armextab.*) } >FLASH
  .ARM :
  {
    __exidx_start = .;
    *(.ARM.exidx*)
    __exidx_end = .;
  } >FLASH

  .preinit_array :
  {
    PROVIDE_HIDDEN (__preinit_array_start = .);
    KEEP (*(.preinit_array*))
    PROVIDE_HIDDEN (__preinit_array_end = .);
  } >FLASH
  .init_array :
  {
    PROVIDE_HIDDEN (__init_array_start = .);
    KEEP (*(SORT(.init_array.*)))
    KEEP (*(.init_array*))
    PROVIDE_HIDDEN (__init_array_end = .);
  } >FLASH
  .fini_array :
  {
    PROVIDE_HIDDEN (__fini_array_start = .);
    KEEP (*(SORT(.fini_array.*)))
    KEEP (*(.fini_array*))
    PROVIDE_HIDDEN (__fini_array_end = .);
  } >FLASH

  /* 已初始化变量：运行期在 RAM，初值存放在 Flash 里 */
  .data :
  {
    . = ALIGN(4);
    _sdata = .;
    *(.data)
    *(.data*)
    . = ALIGN(4);
    _edata = .;
  } >RAM AT> FLASH

  /* 未初始化变量：启动时整体清零 */
  .bss :
  {
    . = ALIGN(4);
    _sbss = .;
    __bss_start__ = _sbss;
    *(.bss)
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;
    __bss_end__ = _ebss;
  } >RAM

  /* 堆栈边界检查：链接时若 RAM 不够会直接报错 */
  ._user_heap_stack :
  {
    . = ALIGN(8);
    PROVIDE ( end = . );
    PROVIDE ( _end = . );
    . = . + _Min_Heap_Size;
    . = . + _Min_Stack_Size;
    . = ALIGN(8);
  } >RAM

  /DISCARD/ :
  {
    libc.a ( * )
    libm.a ( * )
    libgcc.a ( * )
  }

  .ARM.attributes 0 : { *(.ARM.attributes) }
}

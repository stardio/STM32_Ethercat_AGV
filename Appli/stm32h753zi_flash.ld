/*
 * Linker script for STM32H753ZI (NUCLEO-H753ZI)
 * Flash: 2MB at 0x08000000
 * RAM:   512KB AXI SRAM at 0x24000000 (D1)
 *        128KB DTCM     at 0x20000000
 *         64KB ITCM     at 0x00000000
 * Flash storage: Bank2 Sector7 at 0x081E0000 (128KB) - reserved for settings
 */

ENTRY(Reset_Handler)

_estack = ORIGIN(DTCM) + LENGTH(DTCM);

_Min_Heap_Size  = 0x800;
_Min_Stack_Size = 0x800;

MEMORY
{
  ITCM     (xrw) : ORIGIN = 0x00000000, LENGTH = 64K
  FLASH    (rx)  : ORIGIN = 0x08000000, LENGTH = 1920K   /* Bank1 + Bank2 up to settings sector */
  SETTINGS (r)   : ORIGIN = 0x081E0000, LENGTH = 128K    /* Bank2 Sector7: flash settings */
  DTCM     (xrw) : ORIGIN = 0x20000000, LENGTH = 128K
  RAM      (xrw) : ORIGIN = 0x24000000, LENGTH = 448K    /* AXI SRAM (D1), 512K - 64K for ETH */
  ETH_NC   (xrw) : ORIGIN = 0x24070000, LENGTH = 64K     /* AXI SRAM non-cacheable: ETH DMA */
  RAM_D2   (xrw) : ORIGIN = 0x30000000, LENGTH = 288K    /* SRAM1+SRAM2 (D2) */
  RAM_D3   (xrw) : ORIGIN = 0x38000000, LENGTH = 64K     /* SRAM4 (D3) */
  BKPSRAM  (xrw) : ORIGIN = 0x38800000, LENGTH = 4K
}

SECTIONS
{
  .isr_vector :
  {
    . = ALIGN(4);
    KEEP(*(.isr_vector))
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
    KEEP(*(.init))
    KEEP(*(.fini))
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

  .ARM.extab :
  {
    *(.ARM.extab* .gnu.linkonce.armextab.*)
  } >FLASH

  .ARM :
  {
    __exidx_start = .;
    *(.ARM.exidx*)
    __exidx_end = .;
  } >FLASH

  .preinit_array :
  {
    PROVIDE_HIDDEN(__preinit_array_start = .);
    KEEP(*(.preinit_array*))
    PROVIDE_HIDDEN(__preinit_array_end = .);
  } >FLASH

  .init_array :
  {
    PROVIDE_HIDDEN(__init_array_start = .);
    KEEP(*(SORT(.init_array.*)))
    KEEP(*(.init_array*))
    PROVIDE_HIDDEN(__init_array_end = .);
  } >FLASH

  .fini_array :
  {
    PROVIDE_HIDDEN(__fini_array_start = .);
    KEEP(*(SORT(.fini_array.*)))
    KEEP(*(.fini_array*))
    PROVIDE_HIDDEN(__fini_array_end = .);
  } >FLASH

  _sidata = LOADADDR(.data);

  .data :
  {
    . = ALIGN(4);
    _sdata = .;
    *(.data)
    *(.data*)
    . = ALIGN(4);
    _edata = .;
  } >RAM AT>FLASH

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

  ._user_heap_stack :
  {
    . = ALIGN(8);
    PROVIDE(end = .);
    PROVIDE(_end = .);
    . = . + _Min_Heap_Size;
    . = . + _Min_Stack_Size;
    . = ALIGN(8);
  } >RAM

  /* ETH DMA descriptors: AXI SRAM non-cacheable region (MPU Region 0) */
  .eth_dma (NOLOAD) :
  {
    . = ALIGN(32);
    *(.eth_dma)
    *(.eth_dma*)
    . = ALIGN(32);
  } >ETH_NC

  /* DTCM: FreeRTOS heap */
  .dtcm_bss (NOLOAD) :
  {
    . = ALIGN(4);
    *(.dtcm_bss)
    *(.dtcm_bss*)
    . = ALIGN(4);
  } >DTCM

  /DISCARD/ :
  {
    libc.a(*)
    libm.a(*)
    libgcc.a(*)
  }

  .ARM.attributes 0 : { *(.ARM.attributes) }
}

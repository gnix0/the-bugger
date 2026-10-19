/* STM32F446RE: board-specific fixture addresses, not generic viewer defaults. */
ENTRY(Reset_Handler)
MEMORY
{
    FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 512K
    RAM (rw) : ORIGIN = 0x20000000, LENGTH = 128K
}
_stack_top = ORIGIN(RAM) + LENGTH(RAM);
SECTIONS
{
    .vectors : { KEEP(*(.vectors)) } > FLASH
    .text : { *(.text*) *(.rodata*) } > FLASH
    .ARM.extab : { *(.ARM.extab*) } > FLASH
    .ARM.exidx : { *(.ARM.exidx*) } > FLASH
    .data : {
        . = ALIGN(4); _data_start = .;
        *(.data*)
        . = ALIGN(4); _data_end = .;
    } > RAM AT > FLASH
    _data_load = LOADADDR(.data);
    .bss (NOLOAD) : {
        . = ALIGN(4); _bss_start = .;
        *(.bss*) *(COMMON)
        . = ALIGN(4); _bss_end = .;
    } > RAM
    ASSERT(_bss_end + 4096 <= _stack_top, "fixture requires at least 4KiB stack")
    /DISCARD/ : { *(.comment) *(.note.GNU-stack) }
}

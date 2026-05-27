ENTRY(Reset_Handler)

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x00000000, LENGTH = @ATS_BOARD_ROM_BYTES@
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = @ATS_BOARD_RAM_BYTES@
}

SECTIONS
{
    .isr_vector :
    {
        . = ALIGN(4);
        KEEP(*(.isr_vector))
        . = ALIGN(4);
    } > FLASH

    .text :
    {
        . = ALIGN(4);
        *(.text*)
        *(.init)
        *(.fini)
        *(.rodata*)
        *(.preinit_array*)
        *(.init_array*)
        *(.fini_array*)
        *(.glue_7)
        *(.glue_7t)
        *(.eh_frame*)
        . = ALIGN(4);
    } > FLASH

    .ARM.extab :
    {
        . = ALIGN(4);
        *(.ARM.extab*)
        . = ALIGN(4);
    } > FLASH

    .ARM.exidx :
    {
        . = ALIGN(4);
        *(.ARM.exidx*)
        . = ALIGN(4);
        _etext = .;
    } > FLASH

    _sidata = LOADADDR(.data);

    .data : AT (_etext)
    {
        . = ALIGN(4);
        _sdata = .;
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } > RAM

    .bss :
    {
        . = ALIGN(4);
        _sbss = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > RAM

    . = ALIGN(8);
    end = .;
    _end = .;

    .stack ORIGIN(RAM) + LENGTH(RAM) - @ATS_MSP_STACK_BYTES@ (NOLOAD) :
    {
        . = ALIGN(8);
        __StackLimit = .;
        . = . + @ATS_MSP_STACK_BYTES@;
        . = ALIGN(8);
        __StackTop = .;
    } > RAM

    _estack = __StackTop;
    PROVIDE(__stack = __StackTop);

    /DISCARD/ :
    {
        *(.note*)
        *(.comment*)
    }
}

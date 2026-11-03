OUTPUT_ARCH(riscv)
ENTRY(_start)
MEMORY {
    ROM (rx) : ORIGIN = 0x80000000, LENGTH = 16M
    RAM (rw) : ORIGIN = 0x20000000, LENGTH = 128K
}
SECTIONS {
    .text : { KEEP(*(.text.start)) *(.text .text.*) } > ROM
    .rodata : { *(.rodata .rodata.*) . = ALIGN(4); } > ROM
    .data : {
        __data_start = .;
        *(.data .data.* .sdata .sdata.*)
        . = ALIGN(4);
        __data_end = .;
    } > RAM AT> ROM
    __data_load = LOADADDR(.data);
    .bss (NOLOAD) : {
        __bss_start = .;
        *(.bss .bss.* .sbss .sbss.*) *(COMMON)
        . = ALIGN(4);
        __bss_end = .;
    } > RAM
    __stack_top = ORIGIN(RAM) + LENGTH(RAM);
    ASSERT(__bss_end <= __stack_top - 4096, "Contra must leave 4 KiB for stack")
    /DISCARD/ : { *(.comment .eh_frame .eh_frame.* .riscv.attributes) }
}

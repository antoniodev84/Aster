ENTRY(_start)
PHDRS { text PT_LOAD FLAGS(5); rodata PT_LOAD FLAGS(4); data PT_LOAD FLAGS(6); }
SECTIONS {
    . = 0x400000;
    .text : { *(.text .text.*) } :text
    . = ALIGN(4096);
    .rodata : { *(.rodata .rodata.*) } :rodata
    . = ALIGN(4096);
    .data : { *(.data .data.*) . = ALIGN(8); __init_array_start = .; KEEP(*(.init_array .init_array.*)) __init_array_end = .; . = ALIGN(8); __fini_array_start = .; KEEP(*(.fini_array .fini_array.*)) __fini_array_end = .; } :data
    .bss : { *(.bss .bss.*) *(COMMON) } :data
    /DISCARD/ : { *(.eh_frame*) *(.note*) *(.comment*) }
}

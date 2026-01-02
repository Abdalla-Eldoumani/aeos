/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: linker.ld
 * Description: Linker script for AArch64 kernel
 *              Defines memory layout and section placement
 * ============================================================================ */

/* Entry point symbol from boot.asm */
ENTRY(_start)

/* Memory layout for QEMU virt board */
MEMORY
{
    /* RAM starts at 0x40000000 (1GB offset) */
    RAM (rwx) : ORIGIN = 0x40000000, LENGTH = 128M
}

/* Section definitions */
SECTIONS
{
    /* Kernel base address */
    . = 0x40000000;
    _kernel_start = .;

    /* Boot code section - must be first */
    .text.boot : {
        *(.text.boot)
    } > RAM

    /* Code section */
    .text : ALIGN(4096) {
        *(.text)
        *(.text.*)
    } > RAM

    /* Read-only data section */
    .rodata : ALIGN(4096) {
        *(.rodata)
        *(.rodata.*)
    } > RAM

    /* Initialized data section */
    .data : ALIGN(4096) {
        *(.data)
        *(.data.*)
    } > RAM

    /* BSS section (zero-initialized data) */
    .bss : ALIGN(4096) {
        __bss_start = .;
        *(.bss)
        *(.bss.*)
        *(COMMON)
        __bss_end = .;
    } > RAM

    /* Kernel end marker */
    . = ALIGN(4096);
    _kernel_end = .;

    /* Heap section FIRST (grows upward) */
    .heap (NOLOAD) : ALIGN(4096) {
        __heap_start = .;
        . = . + 0x400000;       /* 4MB heap (for framebuffer + allocations) */
        __heap_end = .;
    } > RAM

    /* Stack section AFTER heap (grows downward from top) */
    .stack (NOLOAD) : ALIGN(4096) {
        . = . + 0x20000;        /* 128KB stack (doubled to prevent overflow) */
        __stack_top = .;
    } > RAM

    /* Discard unnecessary sections */
    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }
}

/* Symbol exports for use in C/assembly code */
PROVIDE(__text_start = ADDR(.text));
PROVIDE(__text_end = ADDR(.text) + SIZEOF(.text));
PROVIDE(__rodata_start = ADDR(.rodata));
PROVIDE(__rodata_end = ADDR(.rodata) + SIZEOF(.rodata));
PROVIDE(__data_start = ADDR(.data));
PROVIDE(__data_end = ADDR(.data) + SIZEOF(.data));

/* ============================================================================
 * End of linker.ld
 * ============================================================================ */

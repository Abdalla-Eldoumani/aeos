/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: tests/user/hello.S
 * Description: Minimal static AArch64 EL0 test payload for the ELF loader.
 *
 * Built and embedded by the Makefile, parsed and run by the Phase 6 loader.
 * Uses the AEOS EL0 syscall ABI: x8 = syscall number, x0-x5 = args, svc #0
 * traps into el0_aarch64_sync -> syscall_handler. SYS_WRITE=1 writes fd 1
 * (STDOUT) to the UART, so write(1, msg, len) puts msg on serial - the
 * criterion-3 observable. SYS_EXIT=0 ends the one-shot.
 *
 * Position-independent within its single PT_LOAD segment (adr is PC-relative);
 * linked at VA 0x80000000 by user.ld with exactly one in-window LOAD.
 * ============================================================================ */

    .section .text
    .globl _start
_start:
    mov   x0, #1            // fd = STDOUT_FILENO
    adr   x1, msg          // PC-relative; position-independent within the segment
    mov   x2, #14          // length: matches the .ascii byte count below
    mov   x8, #1           // SYS_WRITE
    svc   #0
    mov   x0, #0
    mov   x8, #0           // SYS_EXIT
    svc   #0
1:  b 1b                  // guard against fallthrough

    .section .rodata
msg:
    .ascii "hello, EL0!\n\0\0"

/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: tests/user/user.ld
 * Description: Link script for the EL0 test payload (tests/user/hello.S).
 *
 * Places .text + .rodata at VA 0x80000000 in a SINGLE PT_LOAD segment
 * (FLAGS 5 = PF_R|PF_X). Discarding .note (and friends) plus building with
 * --build-id=none is what collapses the image to one in-window LOAD: without
 * it gcc emits a .note.gnu.build-id LOAD at the default text base 0x400000,
 * a spurious segment OUTSIDE the user window the loader maps.
 * ============================================================================ */

ENTRY(_start)
PHDRS { load PT_LOAD FLAGS(5); }   /* 5 = PF_R(4) | PF_X(1) */
SECTIONS {
    . = 0x80000000;
    .text : { *(.text*) *(.rodata*) } :load
    /DISCARD/ : { *(.note*) *(.comment) *(.eh_frame*) }
}

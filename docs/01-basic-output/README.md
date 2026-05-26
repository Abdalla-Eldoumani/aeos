# Section 01: Boot and Basic Output

## Overview

This section covers the bootstrap code and basic output functionality for AEOS. It implements the initial boot sequence, exception level transitions, and serial console output via the PL011 UART controller.

## Components

### Boot Code (boot.asm)
- **Location**: `src/boot/boot.asm`
- **Purpose**: First code executed by the kernel, sets up execution environment
- **Key Features**:
  - CPU core detection (parks secondary CPUs)
  - Exception level transitions (EL2 => EL1)
  - Stack pointer initialization
  - BSS section clearing
  - Jump to C kernel entry point

### Macro Definitions (macros.m4)
- **Location**: `src/boot/macros.m4`
- **Purpose**: M4 preprocessor macros for readable assembly code
- **Key Features**:
  - Register aliases
  - Stack frame management macros
  - Memory operation macros
  - UART register definitions
  - System register access helpers

### UART Driver (uart.c)
- **Location**: `src/drivers/uart.c`
- **Purpose**: PL011 UART driver for serial console I/O
- **Key Features**:
  - Polling mode I/O (no interrupts)
  - Character and string output
  - Character and buffer input
  - 115200 baud, 8N1 configuration

### Formatted Output (kprintf.c)
- **Location**: `src/kernel/kprintf.c`
- **Purpose**: Kernel printf implementation
- **Key Features**:
  - Format specifiers: %d, %u, %x, %X, %llu, %lld, %p, %s, %c, %%
  - Width modifiers (e.g., %-10s, %10s) and a `0` zero-pad flag
  - `%.N` precision on `%s` (prints at most N characters), the BUG-1 fix
  - Logging levels (DEBUG, INFO, WARN, ERROR, FATAL)

`kprintf`, `klog`, and `snprintf` share the same `%.N` precision support on `%s`. They differ on the `l`/`ll` length modifiers: `kprintf` and `klog` parse `l`/`ll` and support `%llu`/`%lld`, while `snprintf` does not. If you extend the modifier handling, extend it in `snprintf` (`src/lib/string.c`) to match `kprintf` (`src/kernel/kprintf.c`); values are 64-bit on this target either way.

### Crash-Dump Ring

`kprintf` keeps a fixed-size crash-dump ring (`kprintf_ring` in `kprintf.c`). Every byte that `putchar` emits in normal context is also stored in the ring; the ring is read only on the panic path, by `kprintf_ring_walk` from `handle_exception`. After storing a byte, `putchar` issues a `dmb ish` so the byte write and the ring's position/wrap update are ordered as a unit for a panic-time reader (SEC-04).

A normal ring write is kept from racing the panic-time dump by two layers. The `handle_exception` DAIF mask is the same-core exclusion: it stops a timer interrupt on this core from preempting a ring write while the dump reads. Since Phase 7 there is also a cross-core lock, `kprintf_ring_lock` (a spinlock in `kprintf.c`): `putchar` takes it across the ring store and the position/wrap update so two cores cannot splice a torn pair. The lock is additive, it does not replace the DAIF mask. The panic reader does not block on it: `kprintf_ring_walk` uses a trylock-or-bypass so a wedged core holding the lock can never deadlock the crash dump. Section 3 covers this in full.

## Boot Sequence

1. **Entry Point (_start)**: QEMU loads kernel and jumps to `_start` in EL2
2. **CPU Core Check**: Only CPU 0 continues, others park in WFI loop
3. **Exception Level Drop**:
   - From EL2 => EL1 via ERET instruction
   - Configures EL1 for AArch64 execution
   - Masks all interrupts (DAIF)
4. **Stack Setup**:
   - Sets both SP_EL0 and SP_EL1 to the same `__stack_top` value (currently around `0x40649000`; the linker places the stack 128 KB above `__heap_end`)
   - 128 KB stack allocated by linker
5. **MMU/Cache Disable**: Ensures clean state
6. **BSS Clearing**: Zeros uninitialized data section
7. **Jump to C**: Calls `kernel_main()` with device tree pointer

## Memory Map

```
0x09000000: UART0 (PL011)
0x40000000: RAM start (kernel loaded here)
0x40229000: __heap_start (4 MB heap follows; address shifts with kernel size)
0x40629000: __heap_end
0x40649000: __stack_top (128 KB stack ends here; SP grows downward into the stack region)
```

## Important Notes

### Exception Level Transition
The boot code handles starting in either EL2 (standard QEMU) or EL3. The transition preserves the device tree pointer in x19 across the ERET instruction.

### Stack Pointer Gotcha
Both SP_EL0 and SP_EL1 are set to the same address. This prevents crashes if SPSel unexpectedly changes during exception handling.

### BSS Clearing
The `ZERO_MEMORY` macro clears the BSS section by writing 64-bit zeros in a loop. This initializes global/static variables to zero.

### M4 Preprocessing
Assembly files are preprocessed with m4 before assembly. The quote characters are changed to `«»` to avoid conflicts with assembly comments.

## Usage

### Kernel Output
```c
kprintf("Hello, %s! Value: %d\n", "World", 42);
```

### Logging with Levels
```c
klog_info("System initialized");
klog_error("Failed to allocate memory");
```

### Direct UART Access
```c
uart_puts("Direct output\n");
char c = uart_getc();  // Blocking read
```

## Testing

Build and run:
```bash
make clean && make run
```

Expected output:
```
AEOS kernel starting...
[INFO]  Initializing UART...
UART initialized
```

## Known Issues

- **No Interrupt Support**: UART operates in polling mode only
- **No Buffering**: Output is unbuffered, slow for large amounts of text
- **Newline Conversion**: uart_putc() automatically adds '\r' after '\n' for proper terminal display

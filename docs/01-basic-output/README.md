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
  - Width modifiers (e.g., %-10s, %10s)
  - Logging levels (DEBUG, INFO, WARN, ERROR, FATAL)

## Boot Sequence

1. **Entry Point (_start)**: QEMU loads kernel and jumps to `_start` in EL2
2. **CPU Core Check**: Only CPU 0 continues, others park in WFI loop
3. **Exception Level Drop**:
   - From EL2 => EL1 via ERET instruction
   - Configures EL1 for AArch64 execution
   - Masks all interrupts (DAIF)
4. **Stack Setup**:
   - Sets both SP_EL0 and SP_EL1 to same value (0x4001a000)
   - 128KB stack allocated by linker
5. **MMU/Cache Disable**: Ensures clean state
6. **BSS Clearing**: Zeros uninitialized data section
7. **Jump to C**: Calls `kernel_main()` with device tree pointer

## Memory Map

```
0x09000000: UART0 (PL011)
0x40000000: RAM start (kernel loaded here)
0x4001a000: Stack top (grows downward)
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

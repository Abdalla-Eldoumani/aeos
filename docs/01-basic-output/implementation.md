# Boot and Basic Output - Implementation Details

## boot.asm - Bootstrap Code

### File Structure
The boot code uses m4 preprocessing for readable macros. It includes `macros.m4` which defines helper macros like `ZERO_MEMORY`, `ISB`, and `WFI`.

### Entry Point (_start)

```assembly
_start:
    /* Check which CPU core we are */
    mrs x1, mpidr_el1
    and x1, x1, 0xFF            /* Extract CPU ID */
    cbz x1, primary_cpu         /* If CPU 0, continue boot */
```

**Purpose**: Only CPU core 0 boots the system. Secondary cores are parked in WFI (Wait For Interrupt) to conserve power.

**MPIDR_EL1**: Multiprocessor Affinity Register identifies which CPU core is executing.

### Exception Level Detection

```assembly
    /* Check current exception level */
    mrs x0, CurrentEL
    and x0, x0, 0xC             /* Extract EL bits [3:2] */
    lsr x0, x0, 2               /* Shift to get EL number */
```

**CurrentEL** register bits [3:2] contain the current exception level:
- 0b00 = EL0 (user)
- 0b01 = EL1 (kernel)
- 0b10 = EL2 (hypervisor)
- 0b11 = EL3 (secure monitor)

QEMU virt board starts in EL2.

### EL2 => EL1 Transition

```assembly
drop_to_el1:
    /* Set EL1 execution state to AArch64 */
    mov x0, (1 << 31)           /* EL1 uses AArch64 */
    orr x0, x0, (1 << 1)        /* SWIO hardwired on PE */
    msr hcr_el2, x0

    /* Set SPSR for EL1h (use SP_EL1, mask all interrupts) */
    mov x0, 0x3c5               /* EL1h, DAIF masked */
    msr spsr_el2, x0

    /* Set return address to EL1 entry */
    adr x0, el1_entry
    msr elr_el2, x0

    /* Exception return to EL1 */
    eret
```

**Key Registers**:
- **HCR_EL2**: Hypervisor Configuration Register
  - Bit 31: RW bit (1 = EL1 uses AArch64)
- **SPSR_EL2**: Saved Program Status Register
  - 0x3c5 = EL1h mode with DAIF (Debug, Async abort, IRQ, FIQ) all masked
- **ELR_EL2**: Exception Link Register
  - Contains the address to return to after ERET

**ERET Instruction**: Exception Return - restores PC from ELR_EL2 and PSTATE from SPSR_EL2.

### Stack Initialization (Critical Fix)

```assembly
el1_entry:
    /* Set BOTH SP_EL0 and SP_EL1 to the SAME value */
    ldr x0, =__stack_top        /* x0 = 0x4001a000 */

    /* Set SP_EL0 */
    msr spsel, #0
    ISB
    mov sp, x0                  /* SP_EL0 = 0x4001a000 */

    /* Set SP_EL1 */
    msr spsel, #1
    ISB
    mov sp, x0                  /* SP_EL1 = 0x4001a000 */
```

**Why Both Stack Pointers?**
- ARM64 has separate stack pointers: SP_EL0 and SP_EL1
- SPSel register selects which one is currently in use
- Setting both to the same value prevents crashes if SPSel changes unexpectedly
- ISB (Instruction Synchronization Barrier) ensures the MSR completes before MOV

### BSS Clearing

```assembly
    /* Clear BSS section (uninitialized data) */
    ldr x0, =__bss_start
    ldr x1, =__bss_end
    ZERO_MEMORY(x0, x1, x2)
```

The `ZERO_MEMORY` macro (from macros.m4) expands to:
```assembly
    mov x2, xzr             /* x2 = 0 */
1:  cmp x0, x1              /* Compare start to end */
    b.ge 2f                 /* If start >= end, done */
    str x2, [x0], 8         /* Store 0, increment by 8 */
    b 1b                    /* Loop */
2:
```

This zeros the BSS section in 8-byte chunks.

## macros.m4 - M4 Preprocessor Macros

### Quote Character Change
```m4
changequote(`«', `»')
```
Changes m4 quote characters from ` and ' to « and » to avoid conflicts with assembly comments.

### Memory Operation Macros

**ZERO_MEMORY(start_reg, end_reg, temp_reg)**
```m4
define(«ZERO_MEMORY», «
    mov $3, xzr
1:  cmp $1, $2
    b.ge 2f
    str $3, [$1], 8
    b 1b
2:
»)
```
Generates a loop to zero memory from start to end addresses.

### Utility Macros
```m4
define(«ISB», «isb»)
define(«DSB», «dsb sy»)
define(«WFI», «wfi»)
```
Simple wrappers for barrier and wait instructions.

## uart.c - PL011 UART Driver

### Register Definitions

The PL011 UART has registers at fixed offsets from base address 0x09000000:

```c
#define UART_DR     0x00    /* Data Register */
#define UART_FR     0x18    /* Flag Register */
#define UART_IBRD   0x24    /* Integer Baud Rate Divisor */
#define UART_FBRD   0x28    /* Fractional Baud Rate Divisor */
#define UART_LCRH   0x2C    /* Line Control Register */
#define UART_CR     0x30    /* Control Register */
#define UART_IMSC   0x38    /* Interrupt Mask Set/Clear */
#define UART_ICR    0x44    /* Interrupt Clear Register */
```

### Initialization

```c
void uart_init(void)
{
    /* Disable UART while we configure it */
    MMIO_WRITE(UART_REG(UART_CR), 0);

    /* Set baud rate to 115200 */
    MMIO_WRITE(UART_REG(UART_IBRD), 13);
    MMIO_WRITE(UART_REG(UART_FBRD), 1);
```

**Baud Rate Calculation**:
- UART clock = 24 MHz (QEMU virt board)
- Divisor = UART_CLK / (16 * baud_rate)
- Divisor = 24000000 / (16 * 115200) = 13.020833
- IBRD = 13 (integer part)
- FBRD = round(0.020833 * 64) = 1 (fractional part)

```c
    /* Set line control: 8N1, FIFOs enabled */
    MMIO_WRITE(UART_REG(UART_LCRH),
               UART_LCRH_WLEN_8BIT | UART_LCRH_FEN);

    /* Enable UART, transmit, and receive */
    MMIO_WRITE(UART_REG(UART_CR),
               UART_CR_UARTEN | UART_CR_TXE | UART_CR_RXE);
}
```

### Character Output (Polling Mode)

```c
void uart_putc(char c)
{
    /* Wait until transmit FIFO is not full */
    while (MMIO_READ(UART_REG(UART_FR)) & UART_FR_TXFF) {
        /* Busy wait */
    }

    /* Write character to data register */
    MMIO_WRITE(UART_REG(UART_DR), (uint32_t)c);

    /* If newline, also send carriage return */
    if (c == '\n') {
        uart_putc('\r');
    }
}
```

**UART_FR_TXFF**: Transmit FIFO Full flag (bit 5). When set, the FIFO is full and writing would be lost.

**Newline Handling**: Terminals expect CRLF (carriage return + line feed). This code automatically adds '\r' after each '\n'.

### Character Input (Blocking)

```c
char uart_getc(void)
{
    /* Wait until receive FIFO is not empty */
    while (MMIO_READ(UART_REG(UART_FR)) & UART_FR_RXFE) {
        /* Busy wait */
    }

    /* Read character from data register */
    return (char)MMIO_READ(UART_REG(UART_DR));
}
```

**UART_FR_RXFE**: Receive FIFO Empty flag (bit 4). When set, no data is available.

## kprintf.c - Formatted Output

### Variable Arguments

```c
typedef __builtin_va_list va_list;
#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_arg(ap, type)   __builtin_va_arg(ap, type)
#define va_end(ap)         __builtin_va_end(ap)
```

Uses GCC built-ins for variable argument handling (no stdarg.h dependency).

### Integer to String Conversion

```c
static int print_uint(uint64_t value, int base, int uppercase)
{
    char buf[32];
    int i = 0;
    const char *digits = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";

    if (value == 0) {
        putchar('0');
        return 1;
    }

    /* Convert to string (reversed) */
    while (value > 0) {
        buf[i++] = digits[value % base];
        value /= base;
    }

    /* Print in correct order */
    while (i > 0) {
        putchar(buf[--i]);
    }
}
```

**Algorithm**: Repeatedly divide by base and collect remainders, then print in reverse order.

### Pointer Formatting

```c
static int print_pointer(void *ptr)
{
    uint64_t addr = (uint64_t)ptr;
    char buf[16];
    int i;

    putchar('0');
    putchar('x');

    /* Print address in hex (16 digits for 64-bit) */
    for (i = 15; i >= 0; i--) {
        buf[i] = "0123456789abcdef"[addr & 0xF];
        addr >>= 4;
    }

    for (i = 0; i < 16; i++) {
        putchar(buf[i]);
    }
}
```

Always prints 16 hex digits (64-bit) with "0x" prefix.

### Format String Parsing

```c
while (*fmt) {
    if (*fmt == '%') {
        fmt++;

        /* Check for '-' (left align) */
        if (*fmt == '-') {
            left_align = 1;
            fmt++;
        }

        /* Parse width */
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt - '0');
            fmt++;
        }

        /* Check for 'll' modifier */
        if (*fmt == 'l') {
            fmt++;
            if (*fmt == 'l') {
                long_long = 1;
                fmt++;
            }
        }
```

Supports:
- `-` flag for left alignment
- Width specification (numeric)
- `ll` length modifier for 64-bit integers

### String Alignment

```c
if (width > 0 && width > str_len) {
    padding = width - str_len;
    if (left_align) {
        count += putstring(str);
        for (i = 0; i < padding; i++) {
            putchar(' ');
        }
    } else {
        for (i = 0; i < padding; i++) {
            putchar(' ');
        }
        count += putstring(str);
    }
}
```

**Right-align** (default): Print spaces first, then string
**Left-align** (`-` flag): Print string first, then spaces

### Log Levels

```c
void klog(log_level_t level, const char *fmt, ...)
{
    const char *prefix;

    switch (level) {
        case LOG_DEBUG: prefix = "[DEBUG] "; break;
        case LOG_INFO:  prefix = "[INFO]  "; break;
        case LOG_WARN:  prefix = "[WARN]  "; break;
        case LOG_ERROR: prefix = "[ERROR] "; break;
        case LOG_FATAL: prefix = "[FATAL] "; break;
    }

    putstring(prefix);
    /* ... print formatted message ... */
    putchar('\n');
}
```

Automatically adds level prefix and trailing newline.

## Testing and Debugging

### Verifying Boot Sequence

Add debug output at each stage:
```c
kprintf("Stage 1: UART initialized\n");
kprintf("Stage 2: BSS cleared\n");
kprintf("Stage 3: Entering kernel_main\n");
```

### Checking Stack Pointer

```c
uint64_t sp;
__asm__ volatile("mov %0, sp" : "=r"(sp));
kprintf("Current SP: %p\n", (void *)sp);
```

### Verifying Exception Level

```c
uint64_t el;
__asm__ volatile("mrs %0, CurrentEL" : "=r"(el));
kprintf("Current EL: %u\n", (uint32_t)((el >> 2) & 3));
```

Should print "Current EL: 1" if transition succeeded.

## Common Issues

### Garbled Output
**Symptom**: Random characters on console
**Cause**: Incorrect baud rate calculation
**Fix**: Verify UART clock frequency matches QEMU configuration (24 MHz)

### Missing Newlines
**Symptom**: Lines don't wrap properly
**Cause**: Terminal expects CRLF, only receiving LF
**Fix**: uart_putc() already handles this - check it's being called

### Stack Corruption
**Symptom**: Crashes during function calls
**Cause**: Stack pointer not initialized or wrong alignment
**Fix**: Verify SP_EL1 is set and 16-byte aligned

### Stuck in Boot Loop
**Symptom**: Kernel restarts repeatedly
**Cause**: Exception during boot (check QEMU logs)
**Fix**: Add debug output before the crash point to narrow down location

/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: src/drivers/uart.c
 * Description: PL011 UART driver implementation (polling mode)
 * ============================================================================ */

#include <aeos/uart.h>
#include <aeos/types.h>

/* Helper macros for MMIO register access */
#define MMIO_READ(addr)       (*(volatile uint32_t *)(addr))
#define MMIO_WRITE(addr, val) (*(volatile uint32_t *)(addr) = (val))

/* UART register access macros */
#define UART_REG(offset) (UART0_BASE + (offset))

/**
 * Initialize the UART hardware
 *
 * For QEMU virt board, the UART is already initialized by the firmware,
 * but we configure it explicitly for educational purposes.
 *
 * Baud rate: 115200
 * Data bits: 8
 * Parity: None
 * Stop bits: 1
 */
void uart_init(void)
{
    /* Disable UART while we configure it */
    MMIO_WRITE(UART_REG(UART_CR), 0);

    /* Clear all interrupt masks */
    MMIO_WRITE(UART_REG(UART_IMSC), 0);

    /* Clear any pending interrupts */
    MMIO_WRITE(UART_REG(UART_ICR), 0x7FF);

    /*
     * Set baud rate to 115200
     *
     * For QEMU virt board, UART clock is 24 MHz
     * Baud rate divisor = UART_CLK / (16 * baud_rate)
     *                   = 24000000 / (16 * 115200)
     *                   = 13.020833...
     * Integer part (IBRD) = 13
     * Fractional part (FBRD) = int(0.020833 * 64 + 0.5) = 1
     */
    MMIO_WRITE(UART_REG(UART_IBRD), 13);
    MMIO_WRITE(UART_REG(UART_FBRD), 1);

    /*
     * Set line control register:
     * - 8 data bits
     * - No parity
     * - 1 stop bit
     * - FIFOs enabled
     */
    MMIO_WRITE(UART_REG(UART_LCRH), UART_LCRH_WLEN_8BIT | UART_LCRH_FEN);

    /*
     * Enable UART, transmit, and receive
     */
    MMIO_WRITE(UART_REG(UART_CR),
               UART_CR_UARTEN | UART_CR_TXE | UART_CR_RXE);
}

/**
 * Send a single character to the UART
 * Waits until the transmit FIFO has space
 */
void uart_putc(char c)
{
    /* Wait until transmit FIFO is not full */
    while (MMIO_READ(UART_REG(UART_FR)) & UART_FR_TXFF) {
        /* Busy wait */
    }

    /* Write character to data register */
    MMIO_WRITE(UART_REG(UART_DR), (uint32_t)c);

    /* If newline, also send carriage return for proper terminal display */
    if (c == '\n') {
        uart_putc('\r');
    }
}

/**
 * Send a null-terminated string to the UART
 */
void uart_puts(const char *s)
{
    if (s == NULL) {
        return;
    }

    while (*s) {
        uart_putc(*s++);
    }
}

/**
 * Receive a single character from the UART
 * Blocks until a character is available
 */
char uart_getc(void)
{
    /* Wait until receive FIFO is not empty */
    while (MMIO_READ(UART_REG(UART_FR)) & UART_FR_RXFE) {
        /* Busy wait */
    }

    /* Read character from data register */
    return (char)MMIO_READ(UART_REG(UART_DR));
}

/**
 * Write a buffer of data to the UART
 * Returns the number of bytes written (always equals len for polling mode)
 */
size_t uart_write(const char *buf, size_t len)
{
    size_t i;

    if (buf == NULL) {
        return 0;
    }

    for (i = 0; i < len; i++) {
        uart_putc(buf[i]);
    }

    return len;
}

/**
 * Read data from the UART into a buffer
 * Blocks until requested number of bytes are read
 */
size_t uart_read(char *buf, size_t len)
{
    size_t i;

    if (buf == NULL) {
        return 0;
    }

    for (i = 0; i < len; i++) {
        buf[i] = uart_getc();
    }

    return len;
}

/* ============================================================================
 * End of uart.c
 * ============================================================================ */

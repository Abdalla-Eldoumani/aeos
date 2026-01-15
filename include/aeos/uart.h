/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: include/aeos/uart.h
 * Description: PL011 UART driver interface
 * ============================================================================ */

#ifndef AEOS_UART_H
#define AEOS_UART_H

#include <aeos/types.h>

/* PL011 UART base address on QEMU virt board */
#define UART0_BASE 0x09000000

/* UART register offsets */
#define UART_DR     0x00  /* Data Register */
#define UART_RSR    0x04  /* Receive Status Register */
#define UART_FR     0x18  /* Flag Register */
#define UART_ILPR   0x20  /* IrDA Low-Power Counter Register */
#define UART_IBRD   0x24  /* Integer Baud Rate Register */
#define UART_FBRD   0x28  /* Fractional Baud Rate Register */
#define UART_LCRH   0x2C  /* Line Control Register */
#define UART_CR     0x30  /* Control Register */
#define UART_IFLS   0x34  /* Interrupt FIFO Level Select Register */
#define UART_IMSC   0x38  /* Interrupt Mask Set/Clear Register */
#define UART_RIS    0x3C  /* Raw Interrupt Status Register */
#define UART_MIS    0x40  /* Masked Interrupt Status Register */
#define UART_ICR    0x44  /* Interrupt Clear Register */

/* UART Flag Register (UART_FR) bits */
#define UART_FR_TXFE (1 << 7)  /* Transmit FIFO empty */
#define UART_FR_RXFF (1 << 6)  /* Receive FIFO full */
#define UART_FR_TXFF (1 << 5)  /* Transmit FIFO full */
#define UART_FR_RXFE (1 << 4)  /* Receive FIFO empty */
#define UART_FR_BUSY (1 << 3)  /* UART busy */

/* UART Line Control Register (UART_LCRH) bits */
#define UART_LCRH_FEN  (1 << 4)  /* Enable FIFOs */
#define UART_LCRH_WLEN_8BIT (3 << 5)  /* 8 bits */

/* UART Control Register (UART_CR) bits */
#define UART_CR_UARTEN (1 << 0)  /* UART enable */
#define UART_CR_TXE    (1 << 8)  /* Transmit enable */
#define UART_CR_RXE    (1 << 9)  /* Receive enable */

/**
 * Initialize the UART hardware
 * Sets up baud rate, data format, and enables transmit/receive
 */
void uart_init(void);

/**
 * Send a single character to the UART
 * @param c Character to send
 */
void uart_putc(char c);

/**
 * Send a null-terminated string to the UART
 * @param s String to send
 */
void uart_puts(const char *s);

/**
 * Receive a single character from the UART (blocking)
 * @return Character received
 */
char uart_getc(void);

/**
 * Check if data is available to read from the UART (non-blocking)
 * @return true if data is available, false otherwise
 */
bool uart_data_available(void);

/**
 * Write a buffer of data to the UART
 * @param buf Buffer containing data to write
 * @param len Number of bytes to write
 * @return Number of bytes written
 */
size_t uart_write(const char *buf, size_t len);

/**
 * Read data from the UART into a buffer
 * @param buf Buffer to store received data
 * @param len Maximum number of bytes to read
 * @return Number of bytes read
 */
size_t uart_read(char *buf, size_t len);

#endif /* AEOS_UART_H */

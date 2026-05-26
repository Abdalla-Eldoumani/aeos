/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: include/aeos/pl031.h
 * Description: PrimeCell PL031 RTC driver interface
 * ============================================================================ */

#ifndef AEOS_PL031_H
#define AEOS_PL031_H

#include <aeos/types.h>

/* PrimeCell PL031 RTC on QEMU virt (DTB: compatible = "arm,pl031"). The base
 * sits inside the kernel's identity-mapped Device-nGnRnE MMIO block (vmm.c maps
 * 0x00000000-0x3FFFFFFF as one Device block), so RTC_DR is read with a plain
 * volatile load - no separate vmm mapping. */
#define PL031_BASE 0x09010000UL

/* RTC_DR (data register), offset 0x00: the current time as UTC seconds since
 * the Unix epoch. The only register this driver reads; QEMU keeps the RTC
 * running by default so no enable write is needed. */
#define PL031_DR   0x00

/**
 * Probe the PL031 and log the current time on the boot serial.
 * A register read has no failure path that hangs; log-and-continue.
 */
void pl031_init(void);

/**
 * Read RTC_DR and return the raw UTC seconds-since-epoch.
 */
uint32_t pl031_now_seconds(void);

/**
 * Format UTC seconds as a zero-padded "HH:MM:SS" string (no date).
 * Integer-only (safe under -mgeneral-regs-only). Writes at most buf_size bytes
 * including the terminator.
 * @param secs    UTC seconds since epoch
 * @param buf     destination buffer (>= 9 bytes for "HH:MM:SS\0")
 * @param buf_size size of buf
 */
void pl031_format_hms(uint32_t secs, char *buf, uint32_t buf_size);

#endif /* AEOS_PL031_H */

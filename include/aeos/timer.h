/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: include/aeos/timer.h
 * Description: ARM Generic Timer driver interface
 * ============================================================================ */

#ifndef AEOS_TIMER_H
#define AEOS_TIMER_H

#include <aeos/types.h>

/* Timer tick frequency (Hz) */
#define TIMER_FREQ_HZ   100     /* 100 ticks per second = 10ms per tick */

/**
 * Initialize the system timer
 * - Configures ARM Generic Timer
 * - Registers timer interrupt handler
 * - Does NOT start timer (call timer_start() after enabling interrupts)
 */
void timer_init(void);

/**
 * Start the timer
 * Should be called after interrupts are enabled
 */
void timer_start(void);

/**
 * Get current system tick count
 *
 * @return Number of timer ticks since boot
 */
uint64_t timer_get_ticks(void);

/**
 * Get system uptime in milliseconds
 *
 * @return Milliseconds since boot
 */
uint64_t timer_get_uptime_ms(void);

/**
 * Get system uptime in seconds
 *
 * @return Seconds since boot
 */
uint64_t timer_get_uptime_sec(void);

/**
 * Delay for specified number of milliseconds
 * Uses busy-wait loop (blocking)
 *
 * @param ms Milliseconds to delay
 */
void timer_delay_ms(uint32_t ms);

/**
 * Get timer frequency in Hz
 *
 * @return Timer frequency
 */
uint32_t timer_get_frequency(void);

/**
 * Handle timer interrupt from FIQ
 * Called directly from FIQ handler when timer interrupt is pending
 *
 * @return true if timer interrupt was handled, false otherwise
 */
bool timer_handle_fiq(void);

#endif /* AEOS_TIMER_H */

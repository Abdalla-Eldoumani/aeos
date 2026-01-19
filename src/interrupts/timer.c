/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: src/interrupts/timer.c
 * Description: ARM Generic Timer driver
 * ============================================================================ */

#include <aeos/timer.h>
#include <aeos/interrupts.h>
#include <aeos/gic.h>
#include <aeos/kprintf.h>
#include <aeos/types.h>
#include <aeos/scheduler.h>

/* Timer state */
static struct {
    uint64_t ticks;             /* Total ticks since boot */
    uint32_t frequency;         /* Timer frequency in Hz */
    uint32_t tick_interval;     /* Ticks between interrupts */
    bool initialized;
} timer;

/* ============================================================================
 * ARM Generic Timer System Register Access
 * ============================================================================ */

/* Read counter frequency */
static inline uint32_t read_cntfrq(void)
{
    uint32_t val;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(val));
    return val;
}

/* Read current virtual counter value */
static inline uint64_t read_cntvct(void)
{
    uint64_t val;
    __asm__ volatile("mrs %0, cntvct_el0" : "=r"(val));
    return val;
}

/* Write virtual timer interval value */
static inline void write_cntv_tval(int32_t val)
{
    __asm__ volatile("msr cntv_tval_el0, %0" : : "r"(val));
    __asm__ volatile("isb");
}

/* Read virtual timer control register */
static inline uint32_t read_cntv_ctl(void)
{
    uint32_t val;
    __asm__ volatile("mrs %0, cntv_ctl_el0" : "=r"(val));
    return val;
}

/* Write virtual timer control register */
static inline void write_cntv_ctl(uint32_t val)
{
    __asm__ volatile("msr cntv_ctl_el0, %0" : : "r"(val));
    __asm__ volatile("isb");
}

/* ============================================================================
 * Timer Interrupt Handler
 * ============================================================================ */

/**
 * Timer interrupt handler
 * Called at TIMER_FREQ_HZ rate (100 Hz = every 10ms)
 */
static void timer_irq_handler(void)
{
    /* Increment tick count */
    timer.ticks++;

    /* Set next timer interrupt using virtual timer */
    write_cntv_tval(timer.tick_interval);

    /* Call scheduler tick for preemptive scheduling */
    scheduler_tick();

    /* Optional: print tick count every 10 seconds for debugging (avoid spam) */
    if ((timer.ticks % (TIMER_FREQ_HZ * 10)) == 0) {
        klog_debug("Uptime: %u seconds", (uint32_t)(timer.ticks / TIMER_FREQ_HZ));
    }
}

/* ============================================================================
 * Timer Functions
 * ============================================================================ */

/**
 * Initialize the system timer
 * Uses virtual timer (CNTV) which is the correct timer for non-secure EL1
 */
void timer_init(void)
{
    klog_info("Initializing ARM Generic Timer (virtual)...");

    /* Get timer frequency */
    timer.frequency = read_cntfrq();
    kprintf("  Timer frequency: %u Hz\n", timer.frequency);

    /* Calculate tick interval for desired frequency */
    timer.tick_interval = timer.frequency / TIMER_FREQ_HZ;
    kprintf("  Tick interval: %u (every %u ms)\n",
            timer.tick_interval,
            1000 / TIMER_FREQ_HZ);

    /* Initialize tick count */
    timer.ticks = 0;

    /* Disable virtual timer while configuring */
    write_cntv_ctl(0);

    /* Set initial timer value */
    write_cntv_tval(timer.tick_interval);

    /* Register timer interrupt handler */
    /* IRQ 27 = virtual timer PPI on QEMU virt platform */
    irq_register_handler(27, timer_irq_handler);

    /* Enable timer interrupt in GIC */
    gic_set_priority(27, GIC_PRIORITY_HIGH);
    gic_enable_irq(27);

    /* DO NOT start timer yet - will be started after interrupts_enable() */
    /* This prevents spurious interrupts during initialization */

    timer.initialized = true;

    klog_info("Timer initialized (not started yet)");
}

/**
 * Start the timer
 * Should be called after interrupts are enabled
 */
void timer_start(void)
{
    uint32_t ctl;

    if (!timer.initialized) {
        klog_error("Timer not initialized");
        return;
    }

    /* Enable virtual timer (enable bit, unmask interrupt) */
    ctl = (1 << 0);  /* ENABLE bit */
    write_cntv_ctl(ctl);

    klog_info("Timer started");
}

/**
 * Handle timer interrupt from FIQ
 * Called directly from FIQ handler when timer interrupt is pending
 * Returns true if timer interrupt was handled
 */
bool timer_handle_fiq(void)
{
    uint32_t ctl;

    if (!timer.initialized) {
        return false;
    }

    /* Check if timer interrupt is pending (ISTATUS bit) */
    ctl = read_cntv_ctl();
    if (!(ctl & (1 << 2))) {
        /* No timer interrupt pending */
        return false;
    }

    /* Increment tick count */
    timer.ticks++;

    /* Re-arm timer (this clears the interrupt) */
    write_cntv_tval(timer.tick_interval);

    /* Call scheduler tick for preemptive scheduling */
    scheduler_tick();

    return true;
}

/**
 * Get current system tick count
 */
uint64_t timer_get_ticks(void)
{
    return timer.ticks;
}

/**
 * Get system uptime in milliseconds
 */
uint64_t timer_get_uptime_ms(void)
{
    return (timer.ticks * 1000) / TIMER_FREQ_HZ;
}

/**
 * Get system uptime in seconds
 */
uint64_t timer_get_uptime_sec(void)
{
    return timer.ticks / TIMER_FREQ_HZ;
}

/**
 * Get timer frequency
 */
uint32_t timer_get_frequency(void)
{
    return timer.frequency;
}

/**
 * Delay for specified milliseconds
 * Uses busy-wait on hardware counter
 */
void timer_delay_ms(uint32_t ms)
{
    uint64_t start, target, now;
    uint64_t ticks_per_ms;

    if (!timer.initialized) {
        /* Fallback: use counter frequency */
        timer.frequency = read_cntfrq();
    }

    ticks_per_ms = timer.frequency / 1000;
    start = read_cntvct();
    target = start + (ticks_per_ms * ms);

    do {
        now = read_cntvct();
    } while (now < target);
}

/* ============================================================================
 * End of timer.c
 * ============================================================================ */

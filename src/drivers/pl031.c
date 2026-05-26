/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: src/drivers/pl031.c
 * Description: PrimeCell PL031 RTC driver. Reads RTC_DR (UTC seconds since the
 *              Unix epoch) and formats wall-clock H:M:S for the taskbar.
 * ============================================================================ */

#include <aeos/pl031.h>
#include <aeos/types.h>
#include <aeos/string.h>
#include <aeos/kprintf.h>

/* Same MMIO read pattern as uart.c. PL031_BASE is inside the identity-mapped
 * Device-nGnRnE block, so no separate mapping is needed. */
#define MMIO_READ(addr) (*(volatile uint32_t *)(addr))

/* Seconds in one UTC day, the wrap point for the H:M:S breakdown. */
#define SECS_PER_DAY  86400u

uint32_t pl031_now_seconds(void)
{
    /* One stateless load of the RTC data register. PL031 is a single global
     * device (not banked per-core like the GICC), so under -smp any core reads
     * the same value; no lock is needed. */
    return MMIO_READ(PL031_BASE + PL031_DR);
}

void pl031_format_hms(uint32_t secs, char *buf, uint32_t buf_size)
{
    /* "HH:MM:SS" needs 9 bytes with the NUL. A shorter buffer would let snprintf
     * silently truncate to a plausible-but-wrong time, so refuse instead of
     * misleading a future caller; emit an empty string when there is room. */
    if (buf == NULL || buf_size < 9) {
        if (buf != NULL && buf_size > 0)
            buf[0] = '\0';
        return;
    }

    /* Integer-only so it survives -mgeneral-regs-only (no FP/SIMD). The seconds
     * are taken modulo a day so the value wraps at midnight UTC. */
    uint32_t rem = secs % SECS_PER_DAY;
    uint32_t h   = rem / 3600;
    uint32_t m   = (rem % 3600) / 60;
    uint32_t s   = rem % 60;

    snprintf(buf, buf_size, "%02u:%02u:%02u", h, m, s);
}

void pl031_init(void)
{
    char buf[16];
    uint32_t secs = pl031_now_seconds();

    /* The serial proof: the raw seconds let a reader sanity-check the value
     * (a plausible epoch is after 2023), and the formatted time is what the
     * taskbar shows. A register read cannot hang, so there is no failure path. */
    pl031_format_hms(secs, buf, sizeof(buf));
    klog_info("PL031: %u seconds, %s UTC", secs, buf);
}

/* ============================================================================
 * End of pl031.c
 * ============================================================================ */

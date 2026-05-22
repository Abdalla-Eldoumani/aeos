/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: src/interrupts/gic.c
 * Description: ARM Generic Interrupt Controller (GICv2) driver for QEMU virt
 * ============================================================================ */

#include <aeos/gic.h>
#include <aeos/kprintf.h>
#include <aeos/types.h>

/* ============================================================================
 * GIC Distributor (GICD) Registers
 * ============================================================================ */

#define GICD_CTLR       (GICD_BASE + 0x000)  /* Control Register */
#define GICD_TYPER      (GICD_BASE + 0x004)  /* Type Register */
#define GICD_IGROUPR    (GICD_BASE + 0x080)  /* Interrupt Group (0=Group0/FIQ, 1=Group1/IRQ) */
#define GICD_ISENABLER  (GICD_BASE + 0x100)  /* Interrupt Set-Enable */
#define GICD_ICENABLER  (GICD_BASE + 0x180)  /* Interrupt Clear-Enable */
#define GICD_IPRIORITYR (GICD_BASE + 0x400)  /* Interrupt Priority */
#define GICD_ITARGETSR  (GICD_BASE + 0x800)  /* Interrupt Targets */
#define GICD_ICFGR      (GICD_BASE + 0xC00)  /* Interrupt Config */

/* GICD_CTLR bits */
#define GICD_CTLR_ENABLE_GRP0   (1 << 0)    /* Enable Group 0 (FIQ) */
#define GICD_CTLR_ENABLE_GRP1   (1 << 1)    /* Enable Group 1 (IRQ) */

/* ============================================================================
 * GIC CPU Interface (GICC) Registers - GICv2
 * ============================================================================ */

#define GICC_CTLR   (GICC_BASE + 0x000)  /* CPU Interface Control */
#define GICC_PMR    (GICC_BASE + 0x004)  /* Priority Mask */
#define GICC_BPR    (GICC_BASE + 0x008)  /* Binary Point */
#define GICC_IAR    (GICC_BASE + 0x00C)  /* Interrupt Acknowledge */
#define GICC_EOIR   (GICC_BASE + 0x010)  /* End of Interrupt */

/* GICC_CTLR bits */
#define GICC_CTLR_ENABLE_GRP0   (1 << 0)    /* Enable Group 0 (FIQ) */
#define GICC_CTLR_ENABLE_GRP1   (1 << 1)    /* Enable Group 1 (IRQ) */

/* MMIO access helpers */
#define MMIO_READ(addr)         (*(volatile uint32_t *)(uintptr_t)(addr))
#define MMIO_WRITE(addr, val)   (*(volatile uint32_t *)(uintptr_t)(addr) = (val))

/* ============================================================================
 * GIC Functions
 * ============================================================================ */

/**
 * Initialize the GIC
 */
void gic_init(void)
{
    uint32_t num_irqs;
    uint32_t i;

    klog_info("Initializing GIC...");

    /* Get number of interrupt lines supported */
    num_irqs = ((MMIO_READ(GICD_TYPER) & 0x1F) + 1) * 32;
    kprintf("  IRQ lines: %u\n", num_irqs);

    /* Disable distributor */
    MMIO_WRITE(GICD_CTLR, 0);

    /* Disable all interrupts */
    for (i = 0; i < num_irqs; i += 32) {
        MMIO_WRITE(GICD_ICENABLER + (i / 32) * 4, 0xFFFFFFFF);
    }

    /* Default every interrupt to Group 1. */
    for (i = 0; i < num_irqs; i += 32) {
        MMIO_WRITE(GICD_IGROUPR + (i / 32) * 4, 0xFFFFFFFF);
    }

    /* Move the virtual timer PPI (INTID 27) to Group 0. This is the acknowledge
     * path that actually works on QEMU virt's GICv2 (no security extensions):
     * a Group 0 interrupt is acknowledged by a plain GICC_IAR read, which then
     * returns the real INTID 27 to handle_irq. A Group 1 interrupt instead makes
     * GICC_IAR return the 1022 group-0-spurious indicator and must be acked via
     * GICC_AIAR -- but the aliased registers depend on the Non-secure banking
     * that this configuration does not provide, so an AIAR read returns 0 and
     * the timer is never serviced. With FIQEn left at 0 (the reset default) a
     * Group 0 interrupt is still signaled as IRQ, so the timer keeps arriving on
     * the el1_spx_irq -> handle_irq path; only its acknowledge group changes. */
    {
        uint32_t grp = MMIO_READ(GICD_IGROUPR + (27 / 32) * 4);
        grp &= ~(1u << (27 % 32));
        MMIO_WRITE(GICD_IGROUPR + (27 / 32) * 4, grp);
    }

    /* Set all interrupts to lowest priority */
    for (i = 0; i < num_irqs; i += 4) {
        MMIO_WRITE(GICD_IPRIORITYR + i, 0xA0A0A0A0);
    }

    /* Set all SPIs to target CPU 0 */
    for (i = 32; i < num_irqs; i += 4) {
        MMIO_WRITE(GICD_ITARGETSR + i, 0x01010101);
    }

    /* Set all interrupts to level-sensitive */
    for (i = 0; i < num_irqs; i += 16) {
        MMIO_WRITE(GICD_ICFGR + (i / 16) * 4, 0);
    }

    /* Enable the distributor for both groups. Group 0 carries the timer PPI
     * (see the IGROUPR setup above); Group 1 carries everything else. */
    MMIO_WRITE(GICD_CTLR, GICD_CTLR_ENABLE_GRP0 | GICD_CTLR_ENABLE_GRP1);

    /* Configure CPU interface */
    /* Set priority mask to allow all priorities */
    MMIO_WRITE(GICC_PMR, 0xFF);

    /* Set binary point to no grouping */
    MMIO_WRITE(GICC_BPR, 0);

    /* Enable the CPU interface for both groups. FIQEn is left at 0, so Group 0
     * interrupts (the timer) are signaled as IRQ, not FIQ -- they still arrive
     * on the el1_spx_irq -> handle_irq path and are acked via GICC_IAR. */
    MMIO_WRITE(GICC_CTLR, GICC_CTLR_ENABLE_GRP0 | GICC_CTLR_ENABLE_GRP1);

    kprintf("  GIC base: GICD=%p, GICC=%p\n",
            (void *)GICD_BASE, (void *)GICC_BASE);
    klog_info("GIC initialized");
}

/**
 * Initialize the per-core GIC CPU interface (GICC) on a SECONDARY core. The
 * GICC registers (CTLR/PMR/BPR/IAR/EOIR) are banked per-core, so each secondary
 * enables its own interface with the same three writes gic_init does for the
 * primary: unmask all priorities (PMR=0xFF), no sub-priority grouping (BPR=0),
 * enable both groups (CTLR=GRP0|GRP1, the timer PPI rides Group 0 here).
 *
 * It deliberately touches NO GICD (distributor) register. The distributor -
 * group assignments, the INTID-27-to-Group-0 move, priorities, SPI targeting,
 * and the distributor enable - is GLOBAL and configured exactly once by the
 * primary's gic_init. A secondary re-running any distributor write would
 * re-disable/reconfigure it mid-flight and break the primary's timer and device
 * IRQs (RESEARCH Pitfall 5). Call from secondary_main on each secondary.
 */
void gic_init_secondary(void)
{
    /* Allow all priorities through this core's interface. */
    MMIO_WRITE(GICC_PMR, 0xFF);

    /* No sub-priority grouping. */
    MMIO_WRITE(GICC_BPR, 0);

    /* Enable this core's CPU interface for both groups. */
    MMIO_WRITE(GICC_CTLR, GICC_CTLR_ENABLE_GRP0 | GICC_CTLR_ENABLE_GRP1);
}

/**
 * Enable a specific IRQ
 */
void gic_enable_irq(uint32_t irq)
{
    uint32_t reg, bit;

    if (irq >= GIC_MAX_IRQ) {
        klog_error("Invalid IRQ: %u", irq);
        return;
    }

    reg = irq / 32;
    bit = irq % 32;

    MMIO_WRITE(GICD_ISENABLER + reg * 4, (1 << bit));

    klog_debug("Enabled IRQ %u", irq);
}

/**
 * Disable a specific IRQ
 */
void gic_disable_irq(uint32_t irq)
{
    uint32_t reg, bit;

    if (irq >= GIC_MAX_IRQ) {
        klog_error("Invalid IRQ: %u", irq);
        return;
    }

    reg = irq / 32;
    bit = irq % 32;

    MMIO_WRITE(GICD_ICENABLER + reg * 4, (1 << bit));

    klog_debug("Disabled IRQ %u", irq);
}

/**
 * Set IRQ priority
 */
void gic_set_priority(uint32_t irq, uint8_t priority)
{
    uint32_t reg, shift;
    uint32_t val;

    if (irq >= GIC_MAX_IRQ) {
        klog_error("Invalid IRQ: %u", irq);
        return;
    }

    reg = irq / 4;
    shift = (irq % 4) * 8;

    /* Read-modify-write */
    val = MMIO_READ(GICD_IPRIORITYR + reg * 4);
    val &= ~(0xFF << shift);
    val |= (priority << shift);
    MMIO_WRITE(GICD_IPRIORITYR + reg * 4, val);
}

/**
 * Acknowledge an interrupt
 * Returns the IRQ number
 */
uint32_t gic_acknowledge_irq(void)
{
    uint32_t iar;

    iar = MMIO_READ(GICC_IAR);

    /* Return IRQ number (lower 10 bits) */
    return iar & 0x3FF;
}

/**
 * Signal end of interrupt
 */
void gic_end_of_irq(uint32_t irq)
{
    MMIO_WRITE(GICC_EOIR, irq);
}

/**
 * Send a software-generated interrupt (SGI)
 */
void gic_send_sgi(uint32_t sgi_num, uint32_t target_cpu)
{
    uint32_t val;

    if (sgi_num >= 16) {
        klog_error("Invalid SGI: %u", sgi_num);
        return;
    }

    if (target_cpu >= 8) {
        klog_error("Invalid CPU: %u", target_cpu);
        return;
    }

    /* GICD_SGIR register */
    val = (target_cpu << 16) | sgi_num;
    MMIO_WRITE(GICD_BASE + 0xF00, val);
}

/* ============================================================================
 * End of gic.c
 * ============================================================================ */

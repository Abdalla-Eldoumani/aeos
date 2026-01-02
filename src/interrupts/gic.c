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

    /* Set all interrupts to Group 1 (IRQ, not FIQ) */
    for (i = 0; i < num_irqs; i += 32) {
        MMIO_WRITE(GICD_IGROUPR + (i / 32) * 4, 0xFFFFFFFF);
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

    /* Enable distributor for Group 1 interrupts */
    MMIO_WRITE(GICD_CTLR, GICD_CTLR_ENABLE_GRP1);

    /* Configure CPU interface */
    /* Set priority mask to allow all priorities */
    MMIO_WRITE(GICC_PMR, 0xFF);

    /* Set binary point to no grouping */
    MMIO_WRITE(GICC_BPR, 0);

    /* Enable CPU interface for Group 1 interrupts (IRQ) */
    MMIO_WRITE(GICC_CTLR, GICC_CTLR_ENABLE_GRP1);

    kprintf("  GIC base: GICD=%p, GICC=%p\n",
            (void *)GICD_BASE, (void *)GICC_BASE);
    klog_info("GIC initialized");
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

/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: include/aeos/gic.h
 * Description: ARM Generic Interrupt Controller (GIC) driver interface
 * ============================================================================ */

#ifndef AEOS_GIC_H
#define AEOS_GIC_H

#include <aeos/types.h>

/* GIC base addresses for QEMU virt board */
#define GICD_BASE   0x08000000  /* Distributor */
#define GICC_BASE   0x08010000  /* CPU Interface (GICv2) */
#define GICR_BASE   0x080A0000  /* Redistributor (GICv3) */

/* Maximum number of interrupts */
#define GIC_MAX_IRQ 1024

/* Interrupt types */
#define GIC_TYPE_SPI    0       /* Shared Peripheral Interrupt */
#define GIC_TYPE_PPI    1       /* Private Peripheral Interrupt */
#define GIC_TYPE_SGI    2       /* Software Generated Interrupt */

/* Interrupt priorities (0 = highest, 255 = lowest) */
#define GIC_PRIORITY_HIGH       0x00
#define GIC_PRIORITY_NORMAL     0x80
#define GIC_PRIORITY_LOW        0xF0
#define GIC_PRIORITY_LOWEST     0xFF

/**
 * Initialize the GIC
 * - Configures distributor
 * - Configures CPU interface
 * - Enables interrupt groups
 */
void gic_init(void);

/**
 * Initialize the per-core GIC CPU interface (GICC) on a secondary core.
 * Writes only this core's banked GICC_PMR/BPR/CTLR - it touches NO GICD
 * distributor register. The distributor is global and configured once by the
 * primary's gic_init; a secondary must never re-touch it (that would break the
 * primary's timer/device IRQs). Called by secondary_main on each secondary.
 */
void gic_init_secondary(void);

/**
 * Enable a specific IRQ
 *
 * @param irq IRQ number to enable
 */
void gic_enable_irq(uint32_t irq);

/**
 * Disable a specific IRQ
 *
 * @param irq IRQ number to disable
 */
void gic_disable_irq(uint32_t irq);

/**
 * Set IRQ priority
 *
 * @param irq IRQ number
 * @param priority Priority (0-255, lower = higher priority)
 */
void gic_set_priority(uint32_t irq, uint8_t priority);

/**
 * Acknowledge an interrupt
 * Must be called at start of IRQ handler
 *
 * @return IRQ number that fired
 */
uint32_t gic_acknowledge_irq(void);

/**
 * Signal end of interrupt
 * Must be called at end of IRQ handler
 *
 * @param irq IRQ number to acknowledge
 */
void gic_end_of_irq(uint32_t irq);

/**
 * Send a software-generated interrupt (SGI)
 *
 * @param sgi_num SGI number (0-15)
 * @param target_cpu Target CPU ID
 */
void gic_send_sgi(uint32_t sgi_num, uint32_t target_cpu);

#endif /* AEOS_GIC_H */

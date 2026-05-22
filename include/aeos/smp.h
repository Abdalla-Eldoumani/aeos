/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: include/aeos/smp.h
 * Description: SMP bringup contract - PSCI CPU_ON constants, smp_init, and the
 *              secondary-core entry / online-flag seam (PSCI multi-core, Phase 7)
 * ============================================================================ */

#ifndef AEOS_SMP_H
#define AEOS_SMP_H

#include <aeos/types.h>

/* Core count: the QEMU virt board is booted with -smp 4 and its DTB lists four
 * cortex-a57 cores (flat affinities 0..3). The primary is core 0; smp_init
 * brings up cores 1..SMP_MAX_CPUS-1. */
#define SMP_MAX_CPUS 4

/* Per-core secondary stack size. 16KB is generous for the bounded-scope
 * report-and-park path (the validated probe used this size); the kernel-thread
 * PROCESS_STACK_SIZE precedent is 4KB, but a secondary's stack is cheap and a
 * fault on a too-small stack is far harder to diagnose than the BSS cost. */
#define SMP_SECONDARY_STACK_SIZE 16384

/* The bounded handshake limit. This is a SPIN-COUNT bound, NOT a time: its only
 * job is to guarantee the primary's wait on each secondary's online flag is
 * FINITE so the primary always returns from smp_init and reaches the WM loop
 * even if a secondary never reports (the dominant non-negotiable - a stuck
 * secondary must never hang the kernel). The validated probe brought every core
 * online well within this many spins. */
#define SMP_BRINGUP_TIMEOUT 200000000ULL

/* Bring up secondaries 1..SMP_MAX_CPUS-1 via PSCI CPU_ON with a BOUNDED
 * handshake. A CPU_ON that returns nonzero, or a secondary that never sets its
 * online flag within SMP_BRINGUP_TIMEOUT spins, is logged and SKIPPED - never
 * fatal. The primary always returns. Intended to be called from kernel_main
 * AFTER gic_init/timer_init (wired in a later plan); nothing calls it yet. */
void smp_init(void);

/* The per-core entry point a secondary lands at when PSCI CPU_ON powers it on.
 * DEFINED in src/boot/secondary.asm (a later plan): it drops EL2->EL1, sets the
 * per-core SP from the context_id PSCI passes in x0, and calls secondary_main.
 * smp_init only takes its address as the CPU_ON entry argument. The secondary
 * starts with the MMU OFF, so that address must be a PHYSICAL address - it is,
 * because this kernel is identity-mapped and links at its physical address. */
extern void secondary_entry(void);

/* The per-core C entry, called by secondary_entry after the EL2->EL1 drop and
 * the per-core SP set. In production it enables the per-core MMU against the
 * primary's shared tables, programs the per-core GIC CPU interface, sets
 * VBAR_EL1, then signals online (smp_mark_online) and parks in wfe - the
 * bounded-scope idle/report loop. The full MMU/GIC body lands in a later plan. */
void secondary_main(void);

/* The online-flag seam. A secondary calls smp_mark_online(smp_cpu_id()) AFTER
 * finishing its setup; the store is preceded by a dmb ish so the primary's
 * bounded wait observes a coherent value on the weak ARM memory model.
 * smp_init and any caller (e.g. a future ps CPU column) reads it via
 * smp_is_online. Out-of-range cpu ids are ignored / read 0. */
void smp_mark_online(uint32_t cpu);
uint32_t smp_is_online(uint32_t cpu);

/* Register a per-core idle marker ("idle/cpuN", a registry-only killable=false
 * PCB) on the enumeration registry so ps lists a process for each online core
 * (criterion-3 visibility; the CPU column is a later plan). CALLED FROM THE
 * PRIMARY ONLY - it kmallocs, and the heap is not thread-safe, so secondaries
 * (which only park in wfe) never call it. Registry-only: NOT enqueued, so
 * ready_head stays NULL and the dormant scheduler is not woken. A kmalloc
 * failure is logged and ignored - a missing marker is never fatal. */
void smp_register_core_idle(uint32_t cpu);

#endif /* AEOS_SMP_H */

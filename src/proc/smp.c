/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: src/proc/smp.c
 * Description: PSCI CPU_ON secondary bringup - the HVC wrapper, the per-core
 *              stacks + online flags, and the bounded-handshake smp_init loop
 *              (built and linked, NOT yet called from any boot path; Phase 7)
 * ============================================================================ */

#include <aeos/smp.h>
#include <aeos/spinlock.h>   /* smp_cpu_id() */
#include <aeos/kprintf.h>
#include <aeos/types.h>
#include <aeos/string.h>     /* snprintf() - the per-core idle name */
#include <aeos/process.h>    /* process_register_system() - the registry marker */
#include <aeos/scheduler.h>  /* scheduler_add_process/scheduler_remove_process - the stress mutators */
#include <aeos/vmm.h>        /* vmm_enable_secondary() - per-core MMU */
#include <aeos/gic.h>        /* gic_init_secondary() - per-core GICC */
#include <aeos/interrupts.h> /* exception_vector_table - the shared vector base */

/* ===========================================================================
 * TEST_BUILD cross-core runqueue stress (criterion 2 proof). Compiled in ONLY
 * under TEST=1; production smp.c carries none of this. It brings up REAL
 * secondaries that concurrently hammer the self-locking runqueue mutators
 * (scheduler_add_process/scheduler_remove_process from scheduler.c) on their own
 * per-core dummy PCBs AND bump a shared lock-protected counter. The primary
 * (test_smp_runqueue_lock) then asserts the counter is EXACT (zero lost updates)
 * and the runqueue is well-formed. Without the lock the genuine concurrency
 * corrupts the list and loses counter updates - a REAL RED gate, not a
 * single-core tautology.
 *
 * The stress secondary path SKIPS gic_init_secondary: the TEST kernel_main does
 * NOT configure the GICD distributor (no gic_init/timer_init there), so a
 * secondary running the production GICC init would attach to an unconfigured
 * distributor - a multi-core-init hazard. A pure-memory stress needs no
 * interrupts. It DOES enable the MMU (vmm_enable_secondary) before taking any
 * lock, because the ldaxr/stlxr exclusive monitor + the Inner-Shareable barriers
 * are only correct cross-core with the MMU on against the shared tables.
 * =========================================================================== */
#ifdef TEST_BUILD

/* Stress mode flag. Set TRUE by smp_run_runqueue_stress BEFORE the stress
 * CPU_ON, so the shared secondary_main takes the stress branch instead of the
 * production VBAR/GICC/online path. Production secondary_main is byte-identical
 * when TEST_BUILD is not set (this whole block compiles out). */
static volatile uint32_t smp_stress_mode = 0;

/* The go/done handshake. The primary sets smp_stress_go after all stress CPU_ONs
 * succeed; each stress secondary spins on it (isb in the spin) then runs the
 * loop and sets its own smp_stress_done[id]. The primary waits BOUNDEDLY on the
 * done flags (the same log-and-continue discipline as smp_init), then parks the
 * secondaries by leaving them in wfe. */
static volatile uint32_t smp_stress_go = 0;
static volatile uint32_t smp_stress_done[SMP_MAX_CPUS];

/* The shared lost-update proof: every secondary bumps this under
 * stress_counter_lock ITERS times. If the lock genuinely serializes the bumps
 * the final value is participating_cores * ITERS exactly; a missing/no-op lock
 * loses updates under real contention. Guarded by its OWN lock (NOT
 * scheduler_lock) so the counter proof is independent of the runqueue mutators -
 * the runqueue well-formedness (balanced per-core add/remove) is the separate
 * list proof. */
static volatile uint64_t smp_stress_counter = 0;
static spinlock_t stress_counter_lock = SPINLOCK_INIT;

/* How many iterations each stress secondary runs (set by the primary before go
 * so every core reads the same count). */
static volatile uint32_t smp_stress_iters = 0;

/* Per-core dummy PCBs. Each stress secondary mutates its OWN PCB so the add and
 * the remove are balanced PER CORE and the runqueue returns to baseline (empty)
 * - sharing one PCB would make "is the list empty at the end" ambiguous. STATIC
 * so the secondary never kmallocs (the heap is not thread-safe). Minimal: a
 * distinct pid + a name + state, enough for the mutators to link/unlink them. */
static process_t sec_stress_pcb[SMP_MAX_CPUS];

/* The stress loop, run by each stress secondary AFTER vmm_enable_secondary and
 * AFTER smp_stress_go is observed. Hammers the self-locking runqueue mutators on
 * THIS core's dummy PCB and bumps the shared counter. The mutators take
 * scheduler_lock internally (Task 1) - do NOT take it here, or this self-
 * deadlocks. The counter bump takes its own stress_counter_lock. */
static void smp_stress_loop(uint32_t id)
{
    uint32_t iters = smp_stress_iters;
    for (uint32_t i = 0; i < iters; i++) {
        /* Balanced per-core add/remove under scheduler_lock (the list proof). */
        scheduler_add_process(&sec_stress_pcb[id]);
        scheduler_remove_process(&sec_stress_pcb[id]);

        /* Shared counter bump under its own lock (the lost-update proof). */
        spin_lock(&stress_counter_lock);
        smp_stress_counter++;
        spin_unlock(&stress_counter_lock);
    }

    /* Publish completion: barrier so the bumps + the list state are observable
     * before the primary sees done, then set the per-core done flag. */
    __asm__ volatile("dmb ish" ::: "memory");
    if (id < SMP_MAX_CPUS) {
        smp_stress_done[id] = 1;
    }
}

#endif /* TEST_BUILD */

/* PSCI CPU_ON via the HVC conduit (QEMU virt has no EL3; PSCI is provided at
 * EL2 and reached by HVC from EL1). x0 is the SMC64 CPU_ON function id
 * 0xC4000003 - NOT the SMC32 0x84000003: CPU_ON carries a 64-bit entry address,
 * so it must be the SMC64 id (the high byte 0xC4 = SMC64). x1 is the target
 * affinity, flat 0..3 on this single-cluster board; x2 is the entry PHYSICAL
 * address (the secondary starts MMU-off, so a VA would fault - &secondary_entry
 * is already a PA because the kernel is identity-mapped); x3 is the context_id
 * PSCI delivers to the secondary in x0 on entry, used to hand each core its
 * per-core stack top. Returns the PSCI status: 0=SUCCESS, -2=INVALID_PARAMETERS,
 * -4=ALREADY_ON, -5=ON_PENDING, -6=INTERNAL_FAILURE. This is the ONLY secondary
 * power-on path - QEMU virt holds secondaries OFF-until-CPU_ON, with no
 * spin-table (DTB enable-method = "psci"). */
static long psci_cpu_on(uint64_t target_mpidr, uint64_t entry_pa, uint64_t ctx_id)
{
    register uint64_t x0 __asm__("x0") = 0xC4000003ULL;  /* CPU_ON SMC64 */
    register uint64_t x1 __asm__("x1") = target_mpidr;   /* flat 0..3 on virt */
    register uint64_t x2 __asm__("x2") = entry_pa;       /* PA of entry symbol */
    register uint64_t x3 __asm__("x3") = ctx_id;         /* arrives in x0 on the secondary */
    __asm__ volatile("hvc #0" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x3) : "memory");
    return (long)x0;
}

/* Per-core stacks. Each secondary gets a private 16KB stack; using the shared
 * boot stack (__stack_top) would corrupt the primary. These live in BSS and are
 * zeroed by the primary's boot.asm BSS clear - they are deliberately NOT
 * memset here: a vectorized memset traps under -mgeneral-regs-only (the
 * documented BSS-table rule), and the secondary entry must NOT re-clear BSS.
 * The stack TOP (high address, the AArch64 SP starts there) for core i is
 * &sec_stacks[i][SMP_SECONDARY_STACK_SIZE]; it is 16-byte aligned because the
 * array is 16-byte aligned and the size is a multiple of 16. smp_init hands
 * each core its stack top via the CPU_ON context_id (x3 -> x0 on entry), so the
 * secondary sets SP from x0 without computing it before it has a stack. */
static uint8_t sec_stacks[SMP_MAX_CPUS][SMP_SECONDARY_STACK_SIZE]
    __attribute__((aligned(16)));

/* Per-core online flags, indexed by smp_cpu_id(). In BSS, zeroed by the primary
 * (NOT memset here, same reason as the stacks). A secondary sets its flag (with
 * a dmb ish first) AFTER finishing setup; the primary reads it in a BOUNDED
 * wait. A shared flag + barrier + bounded wait is the validated handshake (no
 * IPI/SGI dance is needed for the bounded scope). */
static volatile uint32_t smp_online_flags[SMP_MAX_CPUS];

/* Mark this core online. The dmb ish (inner-shareable store barrier) precedes
 * the store so the primary's bounded wait observes a coherent value on the weak
 * ARM memory model. Out-of-range ids are ignored. */
void smp_mark_online(uint32_t cpu)
{
    if (cpu >= SMP_MAX_CPUS) {
        return;
    }
    __asm__ volatile("dmb ish" ::: "memory");
    smp_online_flags[cpu] = 1;
}

/* Read a core's online flag. Out-of-range ids read 0. */
uint32_t smp_is_online(uint32_t cpu)
{
    return (cpu < SMP_MAX_CPUS) ? smp_online_flags[cpu] : 0;
}

/* Register a per-core idle marker on the enumeration registry so ps lists a
 * process for each online core (criterion-3 visibility; the CPU column itself is
 * a later plan). The marker is a registry-only PCB ("idle/cpuN", killable=false)
 * minted by process_register_system: NOT enqueued (no scheduler_add_process), so
 * ready_head stays NULL and the dormant scheduler is never woken.
 *
 * CALLED FROM THE PRIMARY ONLY. process_register_system calls kmalloc, and the
 * heap is not thread-safe (src/mm/CLAUDE.md); a secondary calling it would race
 * the free list. So the primary registers each online core's idle marker on its
 * behalf - the secondary itself only parks in wfe and never allocates. This is
 * the bounded-scope simplification: the per-core idle is a registry marker the
 * primary creates, not a scheduled process.
 *
 * A kmalloc failure is logged and ignored (returns without registering) - a
 * missing per-core idle marker must NOT be fatal, the same do-not-block
 * discipline as the bringup handshake. */
void smp_register_core_idle(uint32_t cpu)
{
    char name[16];
    process_t *marker;

    snprintf(name, sizeof(name), "idle/cpu%u", cpu);
    marker = process_register_system(name);
    if (marker == NULL) {
        klog_warn("smp: per-core idle marker for cpu %u not registered", cpu);
        return;
    }

    /* last_cpu is the REPRESENTED core, NOT the registrar's (this runs on the
     * primary, so process_register_system set last_cpu = 0). Overriding it to
     * the cpu being represented is what makes ps's CPU column meaningful: the
     * idle/cpuN marker shows core N, so ps lists cores 0..3 in the bounded
     * scope. (The secondary itself parks in wfe and never touches its PCB; the
     * marker stands in for it.) */
    marker->last_cpu = cpu;
}

/* Bring up secondaries 1..SMP_MAX_CPUS-1 with the BOUNDED handshake. For each
 * target: CPU_ON it at secondary_entry, handing its stack top via context_id.
 * A CPU_ON that returns nonzero is logged and SKIPPED (never trusted to mean
 * "core is up", never fatal). Then spin BOUNDEDLY on the core's online flag -
 * at most SMP_BRINGUP_TIMEOUT iterations, isb each pass - and on timeout log
 * and CONTINUE. The primary ALWAYS returns and reaches the WM loop; a stuck or
 * failed secondary must never hang it (the dominant non-negotiable). NOT called
 * from any boot path in this plan - a later plan wires it into kernel_main.
 *
 * Note: rc and the cpu id are printed with %d/%u (cast to int/uint32_t). The
 * kernel's snprintf/klog have NO l/ll length modifier (src/lib/CLAUDE.md), so
 * %ld would print a literal "%ld" and consume no argument; the PSCI codes are
 * small negatives that fit an int, so %d renders them correctly with sign. */
void smp_init(void)
{
    int online = 0;
    for (uint64_t target = 1; target < SMP_MAX_CPUS; target++) {
        uint64_t stk = (uint64_t)&sec_stacks[target][SMP_SECONDARY_STACK_SIZE];
        long rc = psci_cpu_on(target, (uint64_t)&secondary_entry, stk);
        if (rc != 0) {
            klog_warn("smp: CPU_ON core %u rc=%d - skipping", (uint32_t)target, (int)rc);
            continue;
        }
        uint64_t spins = 0;
        while (!smp_is_online((uint32_t)target) && spins < SMP_BRINGUP_TIMEOUT) {
            spins++;
            __asm__ volatile("isb");
        }
        if (smp_is_online((uint32_t)target)) {
            online++;
            /* Register this core's idle marker on the registry so ps lists it.
             * Done HERE on the primary - the secondary is parked in wfe and must
             * not kmalloc (the heap is not thread-safe). Registry-only, so the
             * dormant scheduler stays asleep. A kmalloc failure is non-fatal. */
            smp_register_core_idle((uint32_t)target);
        } else {
            klog_warn("smp: core %u TIMEOUT - continuing", (uint32_t)target);
        }
    }
    /* online counts the secondaries; the primary (core 0) is always online, so
     * the total core count is online + 1. */
    klog_info("smp: %d cores online", online + 1);
}

/* The real secondary_entry now lives in src/boot/secondary.asm (the per-core
 * EL2->EL1 drop + SP set + bl secondary_main); the 07-03 placeholder asm block
 * that defined it here was removed. smp_init takes &secondary_entry as the
 * CPU_ON entry PA; the symbol is resolved by secondary.asm at link time. */

/* The per-core C entry, reached from secondary_entry (src/boot/secondary.asm)
 * after the EL2->EL1 drop and the per-core SP set. It brings THIS core to a
 * running, reported-in EL1 state and then parks. Every step uses a per-core or
 * banked register / a per-core path - it NEVER touches the global interrupt
 * distributor, NEVER rebuilds the page tables, NEVER re-clears BSS.
 *
 * Order matters: VBAR_EL1 is installed FIRST, before the per-core GICC init or
 * anything else that could fault, so an unexpected exception lands on the shared
 * handler table rather than a stale/garbage vector. The SPSR set in the asm drop
 * keeps DAIF masked across the drop, so no IRQ arrives before VBAR is set.
 *
 * NOT reached on the production boot path in this plan: smp_init is not called
 * from kernel_main yet (a later plan wires it). So the per-core machinery here
 * links but does not run, and the -smp 4 boot is unchanged (no "smp:" markers). */
void secondary_main(void)
{
    uint32_t id = smp_cpu_id();

#ifdef TEST_BUILD
    /* TEST_BUILD cross-core stress branch. When the stress bringup armed
     * smp_stress_mode, this core runs the runqueue stress instead of the
     * production VBAR/GICC/online path: enable the MMU (REQUIRED for the
     * exclusive monitor + Inner-Shareable barriers to be correct cross-core),
     * wait for the go signal, run the locked loop, then park. It does NOT
     * install VBAR (DAIF stays masked from the asm drop; no exceptions are taken
     * in the tight memory loop), does NOT call gic_init_secondary (the TEST GICD
     * is unconfigured; a memory stress needs no interrupts), and does NOT emit
     * the online marker. This branch compiles out entirely in production, so the
     * production secondary_main below is byte-identical when TEST_BUILD is not
     * set. */
    if (smp_stress_mode) {
        vmm_enable_secondary();

        /* Spin on the go-flag (isb each pass so the value is re-read) until the
         * primary releases the stress. */
        while (!smp_stress_go) {
            __asm__ volatile("isb");
        }

        /* Acquire barrier AFTER the go-flag is observed. isb is a
         * context-synchronization barrier; it does NOT order two data-memory
         * loads on the weak ARM model, so without this the loads of
         * smp_stress_iters and the sec_stress_pcb[] fields (written by the
         * primary BEFORE its dmb ish; smp_stress_go=1 release) could float
         * ahead of the go observation and read stale/torn values. dmb ish
         * pairs with the primary's release so the dependent reads are ordered
         * after the flag. */
        __asm__ volatile("dmb ish" ::: "memory");

        smp_stress_loop(id);

        /* Park - the teardown. PSCI has no clean CPU_OFF wired here; the stress
         * secondaries hold no lock and touch no shared state after this, so the
         * remaining scenarios run on the primary undisturbed. */
        for (;;) {
            __asm__ volatile("wfe");
        }
    }
#endif /* TEST_BUILD */

    /* Install the SHARED exception vector table in THIS core's VBAR_EL1 before
     * anything that could fault. Same table the primary installed (exceptions.c
     * via interrupts_init); only the per-core VBAR register is written here - no
     * table is built or changed. The table is position-independent branch stubs,
     * so the one shared address is correct on every core. */
    __asm__ volatile("msr vbar_el1, %0" :: "r"((uint64_t)&exception_vector_table));
    __asm__ volatile("isb");

    /* Enable the MMU + caches against the SHARED Phase-4 tables (the primary
     * built them once during its own MMU init; this does NOT rebuild them). */
    vmm_enable_secondary();

    /* Enable only THIS core's banked GICC. The per-core init touches no
     * distributor register - the distributor stays primary-owned (re-touching it
     * would break the primary's timer/device IRQs). */
    gic_init_secondary();

    /* One-shot online marker, one line per secondary (NOT looped). Under SMP this
     * kprintf races the primary's; the kprintf ring lock that orders concurrent
     * prints is a later plan, and the secondaries are not actually brought up
     * until smp_init is wired into kernel_main (also a later plan) - so the race
     * is not live in THIS plan. Do NOT add a lock here. */
    klog_info("smp: core %u online", id);

    /* Signal online AFTER setup completes. smp_mark_online already issues the
     * dmb ish (inner-shareable store barrier) before the store, so the primary's
     * bounded wait observes a coherent flag on the weak ARM memory model. The
     * barrier lives THERE - exactly once - so this does NOT add a second one. */
    smp_mark_online(id);

    /* Bounded-scope idle/report park. The secondary runs NO scheduled work; the
     * dormant scheduler stays asleep. wfe sleeps until an event wakes it, then
     * loops back to sleep. */
    for (;;) {
        __asm__ volatile("wfe");
    }
}

#ifdef TEST_BUILD
/* The cross-core runqueue stress bringup (TEST_BUILD only). Brings up the
 * secondaries in stress mode, releases them, waits BOUNDEDLY for them to finish,
 * and reports how many participated + the shared counter. Mirrors smp_init's
 * loop but uses the go/done handshake and arms smp_stress_mode so the shared
 * secondary_main takes the stress branch (MMU on, NO gic_init_secondary).
 *
 * The same bounded, log-and-continue discipline as smp_init: a CPU_ON that fails
 * is logged and skipped; a secondary that never reports done within
 * SMP_BRINGUP_TIMEOUT spins is logged and skipped. NEVER infinite - a stuck
 * secondary must not hang the suite (the dominant non-negotiable). The scenario
 * asserts only against the cores that actually reported, and fails non-vacuously
 * (online == 0) if no secondary came up. */
void smp_run_runqueue_stress(uint32_t iters, uint32_t *out_online, uint64_t *out_counter)
{
    int started = 0;

    /* Reset the handshake state, the counter, and each core's dummy PCB. Done
     * BEFORE arming stress mode / CPU_ON so a secondary that starts fast sees a
     * coherent initial state. */
    smp_stress_go = 0;
    smp_stress_counter = 0;
    smp_stress_iters = iters;
    for (uint32_t i = 0; i < SMP_MAX_CPUS; i++) {
        smp_stress_done[i] = 0;
        sec_stress_pcb[i].pid = 1000 + i;     /* distinct, never 0 */
        sec_stress_pcb[i].state = PROCESS_READY;
        sec_stress_pcb[i].next = NULL;
        snprintf(sec_stress_pcb[i].name, sizeof(sec_stress_pcb[i].name),
                 "sec_stress%u", i);
    }

    /* Arm stress mode and barrier it out before any CPU_ON, so the secondary
     * observes smp_stress_mode == 1 when it runs secondary_main. */
    smp_stress_mode = 1;
    __asm__ volatile("dmb ish" ::: "memory");

    /* Bring up each secondary at secondary_entry (the SAME asm drop as the
     * production path; the stress branch is taken inside secondary_main). */
    for (uint64_t target = 1; target < SMP_MAX_CPUS; target++) {
        uint64_t stk = (uint64_t)&sec_stacks[target][SMP_SECONDARY_STACK_SIZE];
        long rc = psci_cpu_on(target, (uint64_t)&secondary_entry, stk);
        if (rc != 0) {
            klog_warn("smp: stress CPU_ON core %u rc=%d - skipping",
                      (uint32_t)target, (int)rc);
            continue;
        }
        started++;
    }

    /* Release the secondaries: barrier, then set go. */
    __asm__ volatile("dmb ish" ::: "memory");
    smp_stress_go = 1;

    /* Wait BOUNDEDLY for each core to report done, count the participants. */
    uint32_t participating = 0;
    for (uint32_t target = 1; target < SMP_MAX_CPUS; target++) {
        uint64_t spins = 0;
        while (!smp_stress_done[target] && spins < SMP_BRINGUP_TIMEOUT) {
            spins++;
            __asm__ volatile("isb");
        }
        /* Acquire barrier AFTER the done-flag is observed. The isb in the spin
         * only re-reads the flag; it does NOT order the later load of
         * smp_stress_counter (and the scheduler stats read in the test) after
         * the flag on the weak ARM model. dmb ish pairs with the secondary's
         * dmb ish; smp_stress_done[id]=1 release (smp_stress_loop) so the
         * counter/stats reads cannot be satisfied from before done was seen. */
        __asm__ volatile("dmb ish" ::: "memory");
        if (smp_stress_done[target]) {
            participating++;
        } else {
            klog_warn("smp: stress core %u TIMEOUT - skipping", target);
        }
    }

    (void)started;
    klog_info("smp: runqueue stress - %u cores participated, counter=%u",
              participating, (uint32_t)smp_stress_counter);

    if (out_online != NULL) {
        *out_online = participating;
    }
    if (out_counter != NULL) {
        *out_counter = smp_stress_counter;
    }
}
#endif /* TEST_BUILD */

/* ============================================================================
 * End of smp.c
 * ============================================================================ */

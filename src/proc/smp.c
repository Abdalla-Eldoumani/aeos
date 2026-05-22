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
        } else {
            klog_warn("smp: core %u TIMEOUT - continuing", (uint32_t)target);
        }
    }
    klog_info("smp: %d secondary cores online", online);
}

/* ----------------------------------------------------------------------------
 * PLACEHOLDER bridge so this plan's smp.c links standalone.
 *
 * smp_init takes the address of secondary_entry, so the linker must resolve the
 * symbol. The real per-core entry (the EL2->EL1 drop + per-core SP + the MMU/GIC
 * body in secondary_main) lands in src/boot/secondary.asm in a later plan. Until
 * then this file ships a MINIMAL, harmless placeholder. A LATER PLAN MUST REMOVE
 * this block (both secondary_entry and the placeholder secondary_main body) and
 * supply the real src/boot/secondary.asm + the production secondary_main.
 *
 * secondary_entry is a top-level naked asm block, .global, NOT a C function with
 * __attribute__((naked)) - AArch64 GCC rejects naked on a function with a body
 * (the same lesson usermode.c's payloads learned). On entry x0 = context_id =
 * this core's stack top (per psci_cpu_on's x3), so set SP from x0 and call into
 * C; if ever entered it just parks. It is never reached in this plan because
 * smp_init is not called. */
__asm__(
    ".section .text\n"
    ".global secondary_entry\n"
    ".type secondary_entry, %function\n"
    "secondary_entry:\n"
    "   mov sp, x0\n"           /* x0 = context_id = per-core stack top */
    "   bl secondary_main\n"
    "1: wfi\n"
    "   b 1b\n"
);

/* Placeholder secondary_main: signal online and park. The production version
 * (a later plan) enables the per-core MMU (vmm_enable_secondary), the per-core
 * GICC (gic_init_secondary), sets VBAR_EL1, then signals online and parks. */
void secondary_main(void)
{
    smp_mark_online(smp_cpu_id());
    for (;;) {
        __asm__ volatile("wfe");
    }
}

/* ============================================================================
 * End of smp.c
 * ============================================================================ */

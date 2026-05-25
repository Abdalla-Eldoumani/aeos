/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: include/aeos/spinlock.h
 * Description: ldaxr/stlxr spin lock (acquire/release) and the MPIDR cpu-id
 *              helper - the leaf mutual-exclusion primitive for SMP
 * ============================================================================ */

#ifndef AEOS_SPINLOCK_H
#define AEOS_SPINLOCK_H

#include <aeos/types.h>

/* Leaf-inline exception to include/CLAUDE.md's "no function bodies in headers"
 * rule: this header has no matching .c. It is a primitive used inline across
 * translation units (kprintf.c's ring lock, scheduler.c's runqueue lock,
 * test_runner.c's RED gate), so the static inline bodies must live here - a
 * single shared .c would force a call and defeat the point of a leaf lock.
 * It includes only <aeos/types.h> (uint32_t/uint64_t) so it stays cycle-free
 * and includable from any of those callers.
 *
 * Why ldaxr/stlxr/stlr and not a software lock (Peterson/Dekker) or a plain
 * load-store: ARM is a weakly-ordered memory model. A plain load-store lock or
 * a software algorithm is wrong because the CPU and the store buffer may
 * reorder a critical section's accesses around the unguarded lock variable.
 * The acquire/release exclusives are the correct hardware primitive. */

typedef struct { volatile uint32_t locked; } spinlock_t;

#define SPINLOCK_INIT { 0 }

/* Acquire. Does not return until l->locked transitions 0->1 under this caller.
 * ldaxr is load-acquire + an exclusive reservation: no later access in the
 * critical section is reordered before the lock is held. While the lock looks
 * held we cbnz back and only re-read (ldaxr) - we never attempt the store, so a
 * contended lock does not generate a storm of failing stlxr. stlxr is the
 * store-release conditional on the reservation: it returns nonzero (into the
 * same tmp) if another core touched the line and the reservation was lost, so
 * we retry. The "memory" clobber stops the compiler reordering C accesses
 * across the asm. */
static inline void spin_lock(spinlock_t *l) {
    uint32_t tmp, one;
    __asm__ volatile(
        "1: ldaxr   %w0, [%2]\n"      /* load-acquire current value */
        "   cbnz    %w0, 1b\n"        /* if held, spin (no store, no contention) */
        "   mov     %w1, #1\n"
        "   stlxr   %w0, %w1, [%2]\n" /* store-release 1; %w0=0 on success */
        "   cbnz    %w0, 1b\n"        /* lost the reservation race? retry */
        : "=&r"(tmp), "=&r"(one)
        : "r"(&l->locked)
        : "memory");
}

/* Release with store-release 0. The release ordering makes all of the critical
 * section's writes globally observable before the lock reads free; this pairs
 * with the next acquirer's load-acquire ldaxr. */
static inline void spin_unlock(spinlock_t *l) {
    __asm__ volatile("stlr wzr, [%0]" :: "r"(&l->locked) : "memory");
}

/* Non-blocking acquire. Returns nonzero iff it took a free lock (0->1); returns
 * zero WITHOUT spinning if the lock was already held. Used where a path must
 * never block (a stress scenario, or the single-core RED gate's held-lock
 * check, which would self-deadlock on a blocking spin_lock).
 *
 * The held path clrex's before failing: ldaxr sets the local exclusive monitor,
 * and branching past stlxr on a held lock would otherwise return with a live
 * reservation. A dangling reservation can only make a subsequent UNRELATED stxr
 * spuriously fail (never spuriously succeed), but the header promises a sane
 * state on failure, so drop the reservation explicitly. The free path is
 * unchanged: cur is 0 from ldaxr, stlxr writes cur=0 on success / cur=1 on a
 * lost reservation, and the caller returns cur == 0. */
static inline int spin_trylock(spinlock_t *l) {
    uint32_t cur, one;
    __asm__ volatile(
        "   ldaxr   %w0, [%2]\n"
        "   cbz     %w0, 2f\n"        /* free -> try the store */
        "   clrex\n"                  /* held -> drop the reservation, then fail */
        "   b       1f\n"
        "2: mov     %w1, #1\n"
        "   stlxr   %w0, %w1, [%2]\n" /* cur becomes 0 on success */
        "1:\n"
        : "=&r"(cur), "=&r"(one)
        : "r"(&l->locked)
        : "memory");
    return cur == 0;
}

/* The cpu id this core runs on. MPIDR_EL1 Aff0 (the low 8 bits) is the full id
 * on this flat single-cluster QEMU virt board (0 on the primary, 1..3 on the
 * secondaries), matching boot.asm's `and x1, x1, 0xFF`. A multi-cluster board
 * would compose (Aff1 << n) | Aff0, but virt is flat, so Aff0 alone is correct.
 * This is the key the per-core data and the last-cpu work key on. */
static inline uint32_t smp_cpu_id(void) {
    uint64_t m; __asm__ volatile("mrs %0, mpidr_el1" : "=r"(m));
    return (uint32_t)(m & 0xFFu);
}

#endif /* AEOS_SPINLOCK_H */

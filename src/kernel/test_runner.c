/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: src/kernel/test_runner.c
 * Description: Self-contained test runner. Built into the kernel image when
 *              `make TEST=1` is invoked: this file's `kernel_main` is linked
 *              instead of the normal one in main.c. Each scenario logs
 *              `PASS: <name>` or `FAIL: <name> (<why>)`. The runner exits
 *              back to the host via semihosting with status 0 (all pass) or 1
 *              (any failure). `make test` greps that output for PASS/FAIL.
 * ============================================================================ */

#include <aeos/uart.h>
#include <aeos/kprintf.h>
#include <aeos/types.h>
#include <aeos/mm.h>
#include <aeos/pmm.h>
#include <aeos/heap.h>
#include <aeos/vmm.h>
#include <aeos/interrupts.h>
#include <aeos/vfs.h>
#include <aeos/ramfs.h>
#include <aeos/process.h>
#include <aeos/scheduler.h>
#include <aeos/semihosting.h>
#include <aeos/shell.h>
#include <aeos/symbols.h>
#include <aeos/framebuffer.h>
#include <aeos/string.h>
#include <aeos/stack_guard.h>
#include <aeos/editor.h>
#include <aeos/usermode.h>
#include <aeos/syscall.h>
#include <aeos/exec.h>
#include <aeos/elf.h>
#include <aeos/spinlock.h>
#include <aeos/smp.h>
#include <aeos/net.h>
#include <aeos/virtio_net.h>

/* The embedded EL0 test binary (tests/user/hello.elf, 06-02), linked into the
 * TEST kernel via ALL_OBJECTS. The ELF-load scenario writes these bytes into the
 * test ramfs and execs them; the loaded binary writes "hello, EL0!\n\0\0" (14
 * bytes - mov x2, #14) then exits. */
extern const unsigned char _binary_hello_elf_start[];
extern const unsigned char _binary_hello_elf_end[];

/* The byte length the embedded binary passes to sys_write (mov x2, #14 in
 * tests/user/hello.S; confirmed by the 06-02 readelf/objdump). test_elf_load_run
 * asserts the observed EL0 write length equals this exactly. */
#define HELLO_WRITE_LEN 14

static uint32_t test_pass_count = 0;
static uint32_t test_fail_count = 0;

static void test_pass(const char *name)
{
    test_pass_count++;
    klog_info("PASS: %s", name);
}

static void test_fail(const char *name, const char *why)
{
    test_fail_count++;
    klog_error("FAIL: %s (%s)", name, why);
}

/* ============================================================================
 * PMM scenarios
 * ============================================================================ */

static void test_pmm_alloc_free(void)
{
    pmm_stats_t before, after;

    pmm_get_stats(&before);

    uint64_t p1 = pmm_alloc_page();
    if (p1 == 0) {
        test_fail("pmm_alloc_free", "pmm_alloc_page returned 0");
        return;
    }
    if ((p1 & 0xFFFu) != 0) {
        test_fail("pmm_alloc_free", "page not 4 KB aligned");
        return;
    }

    pmm_free_page(p1);
    pmm_get_stats(&after);

    if (after.free_pages != before.free_pages) {
        test_fail("pmm_alloc_free", "free count not restored after free");
        return;
    }

    test_pass("pmm_alloc_free");
}

static void test_pmm_multi_page(void)
{
    uint64_t p = pmm_alloc_pages(2); /* 4 pages = 16 KB */
    if (p == 0) {
        test_fail("pmm_multi_page", "pmm_alloc_pages(2) returned 0");
        return;
    }
    pmm_free_pages(p, 2);
    test_pass("pmm_multi_page");
}

/* ============================================================================
 * Heap scenarios
 * ============================================================================ */

static void test_heap_kmalloc_kfree(void)
{
    char *buf = kmalloc(128);
    if (buf == NULL) {
        test_fail("heap_kmalloc_kfree", "kmalloc returned NULL");
        return;
    }
    memset(buf, 0xAB, 128);
    if ((uint8_t)buf[0] != 0xAB || (uint8_t)buf[127] != 0xAB) {
        test_fail("heap_kmalloc_kfree", "memory write/read mismatch");
        kfree(buf);
        return;
    }
    kfree(buf);
    test_pass("heap_kmalloc_kfree");
}

static void test_heap_kfree_null(void)
{
    /* kfree(NULL) must be a no-op, not a crash. */
    kfree(NULL);
    test_pass("heap_kfree_null");
}

static void test_heap_balanced(void)
{
    /* A round-trip alloc/free should leave heap usage where we found it. */
    heap_stats_t before, after;
    heap_get_stats(&before);

    void *a = kmalloc(64);
    void *b = kmalloc(256);
    void *c = kmalloc(1024);
    if (!a || !b || !c) {
        test_fail("heap_balanced", "kmalloc returned NULL during setup");
        kfree(a); kfree(b); kfree(c);
        return;
    }
    kfree(a);
    kfree(c);
    kfree(b);

    heap_get_stats(&after);
    if (after.used_size != before.used_size) {
        test_fail("heap_balanced", "used_size not restored after frees");
        return;
    }
    test_pass("heap_balanced");
}

/* ============================================================================
 * Spinlock scenarios
 * ============================================================================ */

static void test_spinlock_uncontended(void)
{
    /* RED gate for include/aeos/spinlock.h before any SMP code depends on it.
     * On one core spin_lock is one ldaxr + one stlxr (no spin) and spin_unlock
     * one stlr, so this exercises the real acquire/release path, uncontended.
     * The held-lock check uses spin_trylock, never spin_lock: a blocking
     * acquire on a lock this same single thread holds would deadlock the
     * runner (the 30s timeout would then report a failure). */
    spinlock_t lk = SPINLOCK_INIT;
    volatile uint64_t counter = 0;
    const uint64_t N = 1000;

    /* Round trip + guarded counter: a lost update or a no-op lock diverges. */
    for (uint64_t i = 0; i < N; i++) {
        spin_lock(&lk);
        counter++;
        spin_unlock(&lk);
    }
    if (counter != N) {
        test_fail("spinlock_uncontended", "guarded counter != iterations");
        return;
    }

    /* trylock on a free lock takes it. */
    if (spin_trylock(&lk) == 0) {
        test_fail("spinlock_uncontended", "trylock failed on a free lock");
        return;
    }

    /* trylock on the lock now held from above must fail without spinning. */
    if (spin_trylock(&lk) != 0) {
        spin_unlock(&lk);
        test_fail("spinlock_uncontended", "trylock took a held lock");
        return;
    }

    /* Release, then it must be retakeable. */
    spin_unlock(&lk);
    if (spin_trylock(&lk) == 0) {
        test_fail("spinlock_uncontended", "lock not free after unlock");
        return;
    }
    spin_unlock(&lk);

    test_pass("spinlock_uncontended");
}

/* test_kprintf_ring_panic_bypass: the automated gate for the subtlest Phase 7
 * safety claim - a panic never deadlocks on the kprintf ring lock under SMP.
 *
 * The TEST_BUILD seam kprintf_test_panic_bypass_returns (kprintf.c) takes
 * kprintf_ring_lock and then, on THIS core, drives kprintf_ring_walk. Because
 * kprintf_ring_walk uses spin_trylock-or-bypass, its trylock fails on the held
 * lock and it reads/returns anyway, so the seam reaches `return 1`. If a
 * regression swapped that trylock for a blocking spin_lock, kprintf_ring_walk
 * would never return (the lock is held by this same core), the seam would never
 * reach `return 1`, and this scenario would hang to the 30s runner timeout - a
 * detected failure (exit nonzero), NOT a silent pass. So the no-deadlock claim
 * is keyed on the bypass actually returning while the lock is held; it is not a
 * tautology. NOT a sec_* scenario. */
static void test_kprintf_ring_panic_bypass(void)
{
    if (kprintf_test_panic_bypass_returns() != 1) {
        test_fail("kprintf_ring_panic_bypass",
                  "kprintf_ring_walk blocked while the ring lock was held - "
                  "the trylock-or-bypass deadlocks");
        return;
    }
    test_pass("kprintf_ring_panic_bypass");
}

/* ============================================================================
 * VFS scenarios
 * ============================================================================ */

static int test_vfs_setup(void)
{
    vfs_init();
    vfs_filesystem_t *fs = ramfs_create();
    if (fs == NULL) {
        return -1;
    }
    if (vfs_register_filesystem(fs) < 0) {
        return -1;
    }
    if (vfs_mount("/", fs) < 0) {
        return -1;
    }
    return 0;
}

static void test_vfs_create_read_write(void)
{
    int fd = vfs_open("/test.txt", O_CREAT | O_WRONLY, 0644);
    if (fd < 0) {
        test_fail("vfs_create_read_write", "open(O_CREAT) failed");
        return;
    }

    const char *msg = "hello vfs";
    if (vfs_write(fd, msg, 9) != 9) {
        test_fail("vfs_create_read_write", "write count mismatch");
        vfs_close(fd);
        return;
    }
    vfs_close(fd);

    fd = vfs_open("/test.txt", O_RDONLY, 0);
    if (fd < 0) {
        test_fail("vfs_create_read_write", "reopen for read failed");
        return;
    }

    char buf[16] = {0};
    ssize_t n = vfs_read(fd, buf, sizeof(buf));
    if (n != 9 || memcmp(buf, msg, 9) != 0) {
        test_fail("vfs_create_read_write", "read content mismatch");
        vfs_close(fd);
        return;
    }
    vfs_close(fd);

    test_pass("vfs_create_read_write");
}

static void test_vfs_unlink(void)
{
    int fd = vfs_open("/will_be_deleted", O_CREAT | O_WRONLY, 0644);
    if (fd < 0) {
        test_fail("vfs_unlink", "open(O_CREAT) failed");
        return;
    }
    vfs_close(fd);

    if (vfs_unlink("/will_be_deleted") != 0) {
        test_fail("vfs_unlink", "unlink failed");
        return;
    }

    /* Reopening without O_CREAT should fail because the file is gone. */
    fd = vfs_open("/will_be_deleted", O_RDONLY, 0);
    if (fd >= 0) {
        test_fail("vfs_unlink", "file still openable after unlink");
        vfs_close(fd);
        return;
    }
    test_pass("vfs_unlink");
}

/* ============================================================================
 * Process scenarios
 *
 * We cannot run scheduler_start() from the test runner because it never
 * returns, but creating + adding + removing processes exercises the heaviest
 * paths and is enough to catch regressions in PCB allocation, ready-queue
 * linkage, and stack setup.
 * ============================================================================ */

static void test_process_dummy_entry(void)
{
    /* Never invoked; exists only to give process_create a real entry point. */
    while (1) {
        __asm__ volatile("wfi");
    }
}

static void test_process_create_remove(void)
{
    /* process_init / scheduler_init are already done in kernel_main so the
     * earlier VFS tests have a current process; nothing extra to set up. */

    /* SEC-07: pass the name in a mutable local buffer, then mutate that buffer
     * after the create. The PCB owns a fixed-size copy, so proc->name must
     * still read the original; a stored caller pointer would read the mutated
     * bytes (a use-after-scope read in ps). */
    char name_buf[PROCESS_NAME_MAX];
    strncpy(name_buf, "test_proc", sizeof(name_buf) - 1);
    name_buf[sizeof(name_buf) - 1] = '\0';

    process_t *p = process_create(test_process_dummy_entry, name_buf);
    if (p == NULL) {
        test_fail("process_create_remove", "process_create returned NULL");
        return;
    }
    if (p->pid == 0) {
        test_fail("process_create_remove", "PID 0 returned");
        return;
    }

    /* Clobber the caller's buffer; the PCB copy must be unaffected. */
    memset(name_buf, 'Z', sizeof(name_buf) - 1);
    name_buf[sizeof(name_buf) - 1] = '\0';

    if (strcmp(p->name, "test_proc") != 0) {
        test_fail("process_create_remove", "PCB name aliased the caller buffer");
        scheduler_remove_process(p);
        process_unregister(p);
        kfree(p);
        return;
    }

    /* Capture the pid before freeing so the registry-clean check below does not
     * read the freed PCB (a use-after-free, the same class of bug 06-04 fixed). */
    uint64_t freed_pid = p->pid;

    /* Detach from BOTH lists. process_create (06-04) auto-registers the PCB on
     * the enumeration registry in addition to the scheduler run queue, so
     * scheduler_remove_process alone would leave a DETACHED PCB on registry_head
     * (and a later scenario walking the registry would touch freed memory once
     * we kfree it). Unregister, then free the PCB the create allocated. */
    scheduler_remove_process(p);
    process_unregister(p);
    kfree(p);

    /* The registry must hold no PCB with this pid now (no detached entry left). */
    for (process_t *it = process_registry_head(); it != NULL; it = it->reg_next) {
        if (it->pid == freed_pid) {
            test_fail("process_create_remove", "PCB still on the registry after teardown");
            return;
        }
    }

    test_pass("process_create_remove");
}

/* ============================================================================
 * Shell parser scenarios
 * ============================================================================ */

static void test_shell_parse_basic(void)
{
    /* shell_parse modifies its input buffer in place and fills argv from it. */
    char line[] = "ls -la /tmp";
    char *argv[SHELL_MAX_ARGS];
    int argc = 0;

    if (shell_parse(line, &argc, argv) != 0) {
        test_fail("shell_parse_basic", "non-zero return from shell_parse");
        return;
    }
    if (argc != 3) {
        test_fail("shell_parse_basic", "argc != 3");
        return;
    }
    if (strcmp(argv[0], "ls") != 0 ||
        strcmp(argv[1], "-la") != 0 ||
        strcmp(argv[2], "/tmp") != 0) {
        test_fail("shell_parse_basic", "argv content mismatch");
        return;
    }
    test_pass("shell_parse_basic");
}

static void test_shell_parse_whitespace(void)
{
    /* Multiple spaces, leading whitespace, and trailing whitespace must all
     * collapse into the same argv as a single-space version. */
    char line[] = "   echo    hello   world   ";
    char *argv[SHELL_MAX_ARGS];
    int argc = 0;

    shell_parse(line, &argc, argv);
    if (argc != 3 ||
        strcmp(argv[0], "echo") != 0 ||
        strcmp(argv[1], "hello") != 0 ||
        strcmp(argv[2], "world") != 0) {
        test_fail("shell_parse_whitespace", "argv content mismatch");
        return;
    }
    test_pass("shell_parse_whitespace");
}

static void test_shell_parse_empty(void)
{
    char line[] = "      ";
    char *argv[SHELL_MAX_ARGS];
    int argc = 0;

    shell_parse(line, &argc, argv);
    if (argc != 0) {
        test_fail("shell_parse_empty", "argc != 0 on whitespace-only input");
        return;
    }
    test_pass("shell_parse_empty");
}

/* ============================================================================
 * symbol_lookup scenarios — exercises the binary search over the generated
 * aeos_symbols[] table so a regression in the search bounds shows up
 * immediately.
 * ============================================================================ */

static void test_symbol_lookup_known(void)
{
    /* kernel_main is in the symbol table at a stable address. Look it up by
     * its known address (taken via the function pointer) and confirm the
     * formatted name starts with "kernel_main". */
    extern void kernel_main(void *dtb_addr);
    uint64_t addr = (uint64_t)(uintptr_t)&kernel_main;
    char buf[64];

    symbol_lookup(addr, buf, sizeof(buf));

    /* The result is "kernel_main+0x0" (or close) — anything else means the
     * search picked a wrong neighbour. Accept any "+0xN" suffix. */
    const char *expected = "kernel_main";
    if (strncmp(buf, expected, strlen(expected)) != 0) {
        test_fail("symbol_lookup_known", "wrong name resolved");
        return;
    }
    test_pass("symbol_lookup_known");
}

static void test_symbol_lookup_below_first(void)
{
    /* An address below the first text symbol must come back as "<unknown ...>"
     * rather than crashing or wrapping into the last entry. */
    char buf[64];
    symbol_lookup(0x100ULL, buf, sizeof(buf));
    if (buf[0] != '<') {
        test_fail("symbol_lookup_below_first", "expected <unknown ...> form");
        return;
    }
    test_pass("symbol_lookup_below_first");
}

/* ============================================================================
 * MMU scenarios — the in-suite proof that the TEST kernel_main enabled the MMU
 * (vmm_init runs before any scenario). test_mmu_enabled is FEAT-01 criterion 1
 * (SCTLR_EL1.M readable and set); test_mmu_ttbr1_alias is criterion 3 (the high-
 * half alias of the kernel image reads identically to the identity pointer).
 * ============================================================================ */

static void test_mmu_enabled(void)
{
    /* Criterion 1: read SCTLR_EL1 and assert the MMU enable bit (M, bit 0) is
     * set. After vmm_init in kernel_main this is true; if the enable regresses
     * or vmm_init is dropped from the TEST path, this fails. */
    uint64_t sctlr;
    __asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
    if ((sctlr & 1u) == 0) {
        test_fail("mmu_enabled", "SCTLR_EL1.M not set");
        return;
    }
    test_pass("mmu_enabled");
}

static void test_mmu_ttbr1_alias(void)
{
    /* Criterion 3: read the first word of the kernel image two ways and compare.
     * The identity pointer is the TTBR0 view at the physical address; the alias
     * is the TTBR1 high-half view (VMM_TTBR1_BASE + pa). A match proves the
     * high-half kernel mapping is live. _kernel_start is declared as an unbounded
     * array (not a scalar char) so the 4-byte read does not trip
     * -Werror=array-bounds, the same idiom vmm.c uses. */
    extern char _kernel_start[];
    uint64_t pa = (uint64_t)_kernel_start;
    uint32_t identity = *(volatile uint32_t *)pa;
    if (identity != vmm_ttbr1_alias_read(pa)) {
        test_fail("mmu_ttbr1_alias", "high-half read != identity read");
        return;
    }
    test_pass("mmu_ttbr1_alias");
}

/* ============================================================================
 * EL0 scenarios - the headless proof of FEAT-02. The TEST kernel_main does NOT
 * init the GIC/timer, so an EL0 run with a DAIF-masked SPSR (0x3C0) has no
 * preemption to fight: the one-shot is fully deterministic. scheduler_init ran
 * above, so process_current() is non-NULL and the user-page mapper is ready.
 * ============================================================================ */

static void test_el0_roundtrip(void)
{
    /* FEAT-02 criteria 2 + 3. The payload runs at EL0, issues svc #0 for
     * SYS_GETPID (the observable side effect) then svc #0 for SYS_EXIT, whose
     * one-shot branch calls usermode_return - restoring the kernel context and
     * ret'ing back HERE, into the line after usermode_run_payload. If the exit
     * had instead eret'd back to EL0 (RESEARCH Pitfall 4), control would never
     * return and the runner would hang to the 30s timeout = a detected failure. */
    volatile int returned = 0;
    uint64_t expected_pid = process_current()->pid;

    usermode_run_payload(USERMODE_PAYLOAD_ROUNDTRIP);

    /* Control returned via usermode_return -> the kernel regained control. */
    returned = 1;

    if (returned != 1) {
        test_fail("el0_roundtrip", "kernel did not regain control after exit svc");
        return;
    }

    /* The getpid svc must have reached syscall_handler -> sys_getpid_impl. */
    if (syscall_test_last_getpid() != expected_pid) {
        test_fail("el0_roundtrip", "getpid svc side effect not observed");
        return;
    }

    test_pass("el0_roundtrip");
}

static void test_el0_priv_trap(void)
{
    /* FEAT-02 criterion 1. The payload executes msr daifset, #2 at EL0. With
     * SCTLR_EL1.UMA=0 (the reset value the kernel never changes) that traps to
     * EL1 with ESR EC=0x18 (RESEARCH Pitfall 3), landing in el0_aarch64_sync's
     * non-SVC branch -> handle_el0_sync. With the capture armed, the seam records
     * the EC and usermode_returns instead of halting, so control comes back here. */
    usermode_arm_trap_capture();

    usermode_run_payload(USERMODE_PAYLOAD_PRIV_TRAP);

    /* If the capture flag is clear the trap did not occur (UMA set, or the wrong
     * instruction): the payload's fail-path exit svc returned instead. */
    if (!usermode_trap_was_captured()) {
        test_fail("el0_priv_trap", "no trap captured (privileged msr did not fault)");
        return;
    }

    uint32_t ec = usermode_captured_ec();
    if (ec != 0x18) {
        /* A different EC means a different fault than the expected DAIF trap. */
        test_fail("el0_priv_trap", "captured EC was not 0x18");
        return;
    }

    test_pass("el0_priv_trap");
}

/* ============================================================================
 * ELF loader scenarios (FEAT-03, criteria 1/2/3 headlessly). These run INSIDE
 * the VFS suite block (a ramfs must be mounted): the load-run scenario writes
 * the embedded ELF into ramfs and execs it, the reject scenario feeds malformed
 * inputs to elf_validate + elf_exec_file. No GIC/timer in the runner, so the
 * DAIF-masked one-shot the loader enters is deterministic, exactly like the EL0
 * scenarios above. test_elf_load_run mirrors test_el0_roundtrip's sentinel +
 * observable shape, but drives the real loader instead of a compiled-in payload.
 * ============================================================================ */

static void test_elf_load_run(void)
{
    /* Criteria 2/3: the real embedded ELF loads, runs at EL0, and its sys_write
     * svc side effect is observed. Write the embedded bytes to /hello in the
     * mounted ramfs, exec it, and assert (a) the loader returned 0, (b) control
     * came back to the kernel (the EL0 program exited and usermode_return ret'd
     * here - a hang would trip the 30s timeout = a detected failure), and (c)
     * the TEST write observable shows the binary's write reached sys_write_impl
     * with length 14. A vacuous pass (e.g. the exec never ran) would leave the
     * observable length at 0, so the equality is a tight criterion-3 proof. */
    int fd = vfs_open("/hello", O_CREAT | O_WRONLY | O_TRUNC, 0755);
    if (fd < 0) {
        test_fail("elf_load_run", "could not create /hello");
        return;
    }
    uint64_t n = (uint64_t)(_binary_hello_elf_end - _binary_hello_elf_start);
    if (vfs_write(fd, _binary_hello_elf_start, n) != (ssize_t)n) {
        test_fail("elf_load_run", "short write of the embedded ELF to /hello");
        vfs_close(fd);
        return;
    }
    vfs_close(fd);

    /* Clear the write observable so its post-run value is unambiguously from
     * this EL0 run, then run with a sentinel mirroring test_el0_roundtrip. */
    syscall_test_reset_last_write();
    volatile int returned = 0;

    int rc = elf_exec_file("/hello");
    returned = 1;

    if (rc != 0) {
        test_fail("elf_load_run", "elf_exec_file did not return 0");
        return;
    }
    if (returned != 1) {
        test_fail("elf_load_run", "kernel did not regain control after the ELF exited");
        return;
    }
    if (syscall_test_last_write_len() != HELLO_WRITE_LEN) {
        test_fail("elf_load_run", "ELF sys_write side effect not observed (wrong length)");
        return;
    }

    test_pass("elf_load_run");
}

static void test_elf_reject_malformed(void)
{
    /* Criterion 1: malformed / non-ELF / missing inputs are REJECTED with a
     * negative return and the kernel does NOT fault. Two layers: elf_validate as
     * the pure header unit (no fault possible - it reads only inside the buffer),
     * and elf_exec_file as the integration (a real ramfs file). Reaching each
     * assertion line proves the prior call returned (no fault). */

    /* elf_validate direct unit checks. */
    unsigned char tiny[8] = {0};
    if (elf_validate(tiny, sizeof(tiny)) >= 0) {
        test_fail("elf_reject_malformed", "elf_validate accepted a too-small buffer");
        return;
    }

    unsigned char bad[64];
    memset(bad, 0, sizeof(bad));
    bad[0] = 0x7F;
    bad[1] = 'X';                 /* not 'E' - bad magic */
    if (elf_validate(bad, sizeof(bad)) >= 0) {
        test_fail("elf_reject_malformed", "elf_validate accepted a bad-magic buffer");
        return;
    }

    /* elf_exec_file integration: a non-ELF file must be rejected (negative) with
     * no fault. Write junk bytes to /notelf, exec it, assert negative + that
     * control is still here. */
    const char *junk = "not an elf file at all\n";
    int fd = vfs_open("/notelf", O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0) {
        test_fail("elf_reject_malformed", "could not create /notelf");
        return;
    }
    if (vfs_write(fd, junk, strlen(junk)) != (ssize_t)strlen(junk)) {
        test_fail("elf_reject_malformed", "short write of junk to /notelf");
        vfs_close(fd);
        return;
    }
    vfs_close(fd);

    if (elf_exec_file("/notelf") >= 0) {
        test_fail("elf_reject_malformed", "elf_exec_file accepted a non-ELF file");
        return;
    }

    /* A missing file must also be rejected (vfs_open fails inside the loader). */
    if (elf_exec_file("/does_not_exist") >= 0) {
        test_fail("elf_reject_malformed", "elf_exec_file accepted a missing file");
        return;
    }

    test_pass("elf_reject_malformed");
}

/* Write a little-endian value of width bytes at buf+off (no unaligned struct
 * stores; the crafted header is built byte-exact). */
static void le_store(unsigned char *buf, uint64_t off, uint64_t val, unsigned width)
{
    for (unsigned k = 0; k < width; k++) {
        buf[off + k] = (unsigned char)(val >> (8u * k));
    }
}

static void test_elf_reject_oversized_segment(void)
{
    /* CR-01/CR-03 regression: a header-valid ELF whose single PT_LOAD declares a
     * p_memsz that spills past the mapped 2MB L3 ceiling (USER_L3_TOP =
     * 0x80200000) must be REJECTED with a negative return and NO fault, and must
     * leak no pmm pages. Before the fix, exec.c validated against the 1GB window
     * and let the page loop alias L3 leaves / drain the pmm. The segment is
     * BSS-only (p_filesz=0, p_memsz=3MB at p_vaddr=0x80000000); the file-extent
     * and memsz>=filesz checks pass, but vaddr+memsz = 0x80300000 > USER_L3_TOP.
     *
     * Built byte-exact in a small buffer (Ehdr at 0, one Phdr at e_phoff=64) so
     * elf_validate accepts the header, then written to ramfs and run through the
     * real elf_exec_file - the same VFS seam test_elf_load_run uses. */
    unsigned char elf[128];
    memset(elf, 0, sizeof(elf));

    /* Ehdr: magic + class/data/version, ET_EXEC, EM_AARCH64, one 56-byte phdr at
     * offset 64. e_entry is irrelevant (the run is rejected before any enter). */
    elf[EI_MAG0] = ELFMAG0;
    elf[EI_MAG1] = ELFMAG1;
    elf[EI_MAG2] = ELFMAG2;
    elf[EI_MAG3] = ELFMAG3;
    elf[EI_CLASS] = ELFCLASS64;
    elf[EI_DATA]  = ELFDATA2LSB;
    elf[EI_VERSION] = EV_CURRENT;
    le_store(elf, 0x10, ET_EXEC, 2);        /* e_type */
    le_store(elf, 0x12, EM_AARCH64, 2);     /* e_machine */
    le_store(elf, 0x14, EV_CURRENT, 4);     /* e_version */
    le_store(elf, 0x18, 0x80000000ULL, 8);  /* e_entry */
    le_store(elf, 0x20, 64, 8);             /* e_phoff (phdr right after Ehdr) */
    le_store(elf, 0x36, 56, 2);             /* e_phentsize */
    le_store(elf, 0x38, 1, 2);              /* e_phnum */

    /* One PT_LOAD: BSS-only (filesz 0), memsz 3MB at VA 0x80000000. */
    uint64_t ph = 64;
    le_store(elf, ph + 0x00, PT_LOAD, 4);          /* p_type */
    le_store(elf, ph + 0x04, PF_R | PF_W, 4);      /* p_flags */
    le_store(elf, ph + 0x08, 0, 8);                /* p_offset */
    le_store(elf, ph + 0x10, 0x80000000ULL, 8);    /* p_vaddr */
    le_store(elf, ph + 0x18, 0x80000000ULL, 8);    /* p_paddr */
    le_store(elf, ph + 0x20, 0, 8);                /* p_filesz */
    le_store(elf, ph + 0x28, 0x300000ULL, 8);      /* p_memsz = 3MB, past 2MB top */
    le_store(elf, ph + 0x30, 0x1000ULL, 8);        /* p_align */

    /* Sanity: the header itself must be valid, or the test would pass for the
     * wrong reason (rejected at the header gate, not the 2MB ceiling). */
    if (elf_validate(elf, sizeof(elf)) != 0) {
        test_fail("elf_reject_oversized_segment", "crafted header failed elf_validate");
        return;
    }

    int fd = vfs_open("/oversized", O_CREAT | O_WRONLY | O_TRUNC, 0755);
    if (fd < 0) {
        test_fail("elf_reject_oversized_segment", "could not create /oversized");
        return;
    }
    if (vfs_write(fd, elf, sizeof(elf)) != (ssize_t)sizeof(elf)) {
        test_fail("elf_reject_oversized_segment", "short write of the crafted ELF");
        vfs_close(fd);
        return;
    }
    vfs_close(fd);

    /* Capture pmm free pages so the reject can be proven leak-free. */
    pmm_stats_t before, after;
    pmm_get_stats(&before);

    volatile int returned = 0;
    int rc = elf_exec_file("/oversized");
    returned = 1;   /* reaching here proves no fault took the kernel down */

    if (returned != 1) {
        test_fail("elf_reject_oversized_segment", "kernel did not return from the loader");
        return;
    }
    if (rc >= 0) {
        test_fail("elf_reject_oversized_segment", "oversized segment was not rejected");
        return;
    }
    pmm_get_stats(&after);
    if (after.free_pages != before.free_pages) {
        test_fail("elf_reject_oversized_segment", "rejected load leaked pmm pages");
        return;
    }

    test_pass("elf_reject_oversized_segment");
}

/* ============================================================================
 * Process kill-reap scenario (FEAT-03 criterion 4, headless, NON-VACUOUS).
 *
 * Proves the kill mechanism the way Scope B can prove it without a display:
 * register a user process via user_proc_register, point the loader's
 * current_user_proc at it (the TEST hook), arm its kill flag via
 * process_kill(thatpid) - by PID, NOT process_current()->pid (idle's pid, a
 * tautology) - then enter EL0 with the getpid-first roundtrip payload. The kill
 * seam sits at the TOP of syscall_handler and reads current_user_proc->
 * kill_requested BEFORE the syscall dispatches (06-04), so the kill-armed run is
 * reaped via usermode_return at the first svc BEFORE sys_getpid_impl runs.
 *
 * The non-vacuous distinguisher: pre-set the getpid observable to a sentinel a
 * real pid can never equal, then assert it is STILL the sentinel after the run.
 * If the seam had NOT fired, the getpid svc would overwrite the sentinel with
 * the idle pid and the assertion would fail. This is distinct from
 * test_el0_roundtrip, which runs the SAME payload with NO kill flag and asserts
 * the observable DOES advance to the current pid - the two together prove the
 * flag is what caused the early reap, not normal flow.
 * ============================================================================ */

static void test_process_kill_reap(void)
{
    /* A value sys_getpid can never return (real pids are small integers). */
    const uint64_t GETPID_SENTINEL = 0xDEADBEEFULL;

    /* 1. Register the process the kill applies to (registry-only, not enqueued). */
    process_t *p = user_proc_register("killtest");
    if (p == NULL) {
        test_fail("process_kill_reap", "user_proc_register returned NULL");
        return;
    }
    uint64_t kpid = p->pid;

    /* The ps-visibility half of criterion 4: the registered PCB is enumerable on
     * the registry by pid (what cmd_ps walks) before we reap it. */
    bool found = false;
    for (process_t *it = process_registry_head(); it != NULL; it = it->reg_next) {
        if (it->pid == kpid) {
            found = true;
            break;
        }
    }
    if (!found) {
        test_fail("process_kill_reap", "registered PCB not enumerable in the registry");
        current_user_proc_set(NULL);
        process_unregister(p);
        kfree(p);
        return;
    }

    /* 2. Point the kill seam at this PCB (NOT process_current(), which is idle). */
    current_user_proc_set(p);

    /* 3. Arm the kill by PID via the registry. */
    int kr = process_kill(kpid);
    if (kr != 0) {
        test_fail("process_kill_reap", "process_kill did not find the registered pid");
        current_user_proc_set(NULL);
        process_unregister(p);
        kfree(p);
        return;
    }

    /* 4. Sentinel the getpid observable so the post-run check is non-vacuous. */
    syscall_test_set_last_getpid(GETPID_SENTINEL);

    /* 5. Enter EL0 with the getpid-first roundtrip payload. The seam should reap
     * the first svc (getpid) before it dispatches, returning control here. */
    volatile int returned = 0;
    usermode_run_payload(USERMODE_PAYLOAD_ROUNDTRIP);
    returned = 1;

    /* 6. Assert (non-vacuous). */
    if (returned != 1) {
        test_fail("process_kill_reap", "kernel did not regain control after the reap");
        current_user_proc_set(NULL);
        process_unregister(p);
        kfree(p);
        return;
    }
    if (syscall_test_last_getpid() != GETPID_SENTINEL) {
        /* The getpid svc dispatched - the seam did NOT reap before it. */
        test_fail("process_kill_reap", "getpid observable advanced - run was not reaped before dispatch");
        current_user_proc_set(NULL);
        process_unregister(p);
        kfree(p);
        return;
    }
    if (process_kill(0xDEADBEEFULL) >= 0) {
        test_fail("process_kill_reap", "process_kill accepted a bogus pid");
        current_user_proc_set(NULL);
        process_unregister(p);
        kfree(p);
        return;
    }

    /* 7. Cleanup: clear the seam pointer, unregister + free the test PCB so no
     * dangling registered PCB remains, and confirm idle is still current. */
    current_user_proc_set(NULL);
    process_unregister(p);
    kfree(p);

    if (process_current() == NULL) {
        test_fail("process_kill_reap", "process_current() became NULL after cleanup");
        return;
    }

    test_pass("process_kill_reap");
}

/* ============================================================================
 * Framebuffer scenarios — exercise the graphics path (init, fill, getpixel)
 * end-to-end without booting the full GUI. The test runner doesn't have a
 * VirtIO GPU attached, but `fb_init` allocates an in-memory framebuffer and
 * the primitives operate on it directly, so we can read back what we wrote.
 * ============================================================================ */

static void test_fb_init_and_fill(void)
{
    if (fb_init() < 0) {
        test_fail("fb_init", "fb_init returned <0");
        return;
    }

    fb_info_t *fb = fb_get_info();
    if (fb == NULL || fb->base == NULL || fb->width == 0 || fb->height == 0) {
        test_fail("fb_init", "fb_get_info returned an unusable framebuffer");
        return;
    }
    test_pass("fb_init");

    /* Fill a known rectangle and read back a pixel inside it and one outside.
     * The clipping logic in fb_fill_rect protects against out-of-bounds, and
     * fb_getpixel returns 0 for OOB reads — combined, this exercises both
     * the write path and the bounds check. */
    const uint32_t color   = 0xFF112233u;
    const int32_t  rect_x  = 10;
    const int32_t  rect_y  = 10;
    const int32_t  rect_w  = 20;
    const int32_t  rect_h  = 20;

    fb_fill_rect(rect_x, rect_y, rect_w, rect_h, color);

    uint32_t inside  = fb_getpixel(rect_x + 5, rect_y + 5);
    uint32_t outside = fb_getpixel(rect_x + rect_w + 5, rect_y);

    if (inside != color) {
        test_fail("fb_fill_rect", "pixel inside the filled rect did not match");
        return;
    }
    if (outside == color) {
        test_fail("fb_fill_rect", "pixel outside the rect was overwritten");
        return;
    }
    test_pass("fb_fill_rect");
}

/* ============================================================================
 * Security smoke scenarios
 *
 * One scenario per testable invariant the SECURITY_AUDIT closed in 13.B. Each
 * follows the guard-and-early-return shape above. `make audit` asserts these
 * PASS lines are present so a future change that drops one fails the gate.
 * The scenarios are non-destructive: the stack-guard one restores the sentinel
 * it corrupts, and the double-free one relies on kfree refusing (not halting)
 * a bad pointer so the runner never times out.
 * ============================================================================ */

static void test_sec_kcalloc_overflow(void)
{
    /* SEC-06 idiom shared with the editor guard: a count*size that overflows
     * must be rejected, not turned into an undersized buffer. There is no
     * SIZE_MAX in this tree, so the bound is ((size_t)-1). */
    void *p = kcalloc(((size_t)-1) / 2 + 2, 2);
    if (p != NULL) {
        test_fail("sec_kcalloc_overflow", "kcalloc did not reject overflow");
        kfree(p);
        return;
    }
    test_pass("sec_kcalloc_overflow");
}

static void test_sec_stack_guard(void)
{
    /* SEC-01: corrupt the sentinel, assert the non-halting predicate reports
     * overflow, then restore it before anything else reads it. Calling
     * stack_guard_check here would halt the kernel; the test uses the
     * read-only stack_guard_intact seam instead. */
    volatile uint64_t *limit = (volatile uint64_t *)&__stack_limit;
    uint64_t saved = *limit;

    *limit = 0;
    bool detected = !stack_guard_intact();
    *limit = saved;

    if (!detected) {
        test_fail("sec_stack_guard", "guard did not detect corrupted sentinel");
        return;
    }
    if (!stack_guard_intact()) {
        test_fail("sec_stack_guard", "sentinel not restored after the check");
        return;
    }
    test_pass("sec_stack_guard");
}

static void test_sec_double_free_after_merge(void)
{
    /* SEC-06 worst case from the audit: free two adjacent blocks so they merge,
     * reuse the merged region, then free a pointer whose old header now lands
     * strictly mid-block of the reused allocation. kfree must refuse it via the
     * bad-magic check (klog_error + return), not corrupt the free list. The
     * refusal must not halt or this scenario hangs the runner.
     *
     * Geometry on this target (heap_block_t is 40 bytes, kmalloc(64) rounds to a
     * 104-byte block): `a` = [A, A+104) payload A+40, `b` = [A+104, A+208)
     * payload A+144. kfree(a); kfree(b) folds `b` into `a` (absorb-prev), so the
     * 208-byte survivor starts at A. c = kmalloc(128) reuses that block; its
     * payload base is A+40, identical to `a`'s old payload. So `a` is NOT a
     * mid-block pointer -- freeing it would hit the live header at A and the
     * pre-existing double-free / valid-header path, never the bad-magic check.
     *
     * The genuine mid-block pointer is `b`: its header at A+104 is strictly
     * interior to `c`'s allocation. merge_free_blocks zeroed that absorbed
     * header's magic during the fold (WR-02), so kfree(b) reads a cleared magic
     * and is refused via the bad-magic path deterministically -- no reliance on
     * uninitialized reuse bytes happening to differ from HEAP_MAGIC. */
    heap_stats_t before, after;
    heap_get_stats(&before);

    char *a = kmalloc(64);
    char *b = kmalloc(64);
    if (a == NULL || b == NULL) {
        test_fail("sec_double_free_after_merge", "kmalloc returned NULL during setup");
        kfree(a);
        kfree(b);
        return;
    }

    kfree(a);
    kfree(b);

    char *c = kmalloc(128);
    if (c == NULL) {
        test_fail("sec_double_free_after_merge", "merged block not reusable");
        return;
    }

    /* `b` lands mid-block of `c`; its absorbed header carries a cleared magic.
     * kfree must refuse it via the bad-magic path. Reaching the next line proves
     * the refusal did not halt. */
    kfree(b);

    /* `c` is still a valid live allocation -- the refused free of `b` left it
     * untouched. Releasing it returns the whole merged region to baseline. */
    kfree(c);

    heap_get_stats(&after);
    if (after.used_size != before.used_size) {
        test_fail("sec_double_free_after_merge", "heap usage drifted after refused free");
        return;
    }
    test_pass("sec_double_free_after_merge");
}

static void test_sec_vfs_path_too_long(void)
{
    /* SEC-03: split_path rejects a path over VFS_PATH_MAX and any component at
     * or over MAX_FILENAME_LEN before the per-token kmalloc. Both forms must
     * fail the open with a negative return. Reuses the ramfs mounted by
     * test_vfs_setup. */
    char long_path[VFS_PATH_MAX + 16];
    long_path[0] = '/';
    for (size_t i = 1; i < sizeof(long_path) - 1; i++) {
        long_path[i] = 'a';
    }
    long_path[sizeof(long_path) - 1] = '\0';

    if (vfs_open(long_path, O_RDONLY, 0) >= 0) {
        test_fail("sec_vfs_path_too_long", "over-length path was not rejected");
        return;
    }

    /* A single component longer than MAX_FILENAME_LEN - 1 within a short path. */
    char long_component[MAX_FILENAME_LEN + 8];
    long_component[0] = '/';
    for (size_t i = 1; i < sizeof(long_component) - 1; i++) {
        long_component[i] = 'b';
    }
    long_component[sizeof(long_component) - 1] = '\0';

    if (vfs_open(long_component, O_RDONLY, 0) >= 0) {
        test_fail("sec_vfs_path_too_long", "over-length component was not rejected");
        return;
    }
    test_pass("sec_vfs_path_too_long");
}

static void test_sec_editor_growth_overflow(void)
{
    /* SEC-02: the public editor path only doubles from a small int capacity and
     * reaches the kmalloc-NULL OOM branch, never the integer-overflow branch.
     * The TEST_BUILD-gated seam forces an overflowing new_cap through the
     * guarded line-array growth and returns nonzero only when the growth was
     * refused with the buffer left intact. */
    if (editor_test_growth_overflow_refused() == 0) {
        test_fail("sec_editor_growth_overflow", "overflowing growth was not refused");
        return;
    }
    test_pass("sec_editor_growth_overflow");
}

/* ============================================================================
 * SMP cross-core runqueue lock proof (criterion 2)
 * ============================================================================ */

/* Iterations each stress secondary runs. Matched to the validated probe shape
 * (the 07-RESEARCH 30000/30000 proof used 10000 per core). The proof is the
 * EXACT equality counter == online * ITERS, not the magnitude. */
#define SMP_STRESS_ITERS 10000

static void test_smp_runqueue_lock(void)
{
    /* The GENUINELY cross-core proof of the runqueue lock (criterion 2). Brings
     * up REAL secondaries that concurrently hammer scheduler_add_process /
     * scheduler_remove_process (the self-locking mutators) on their own per-core
     * dummy PCBs AND bump a shared lock-protected counter. We assert:
     *   (a) the shared counter == participating_cores * ITERS EXACTLY (zero lost
     *       updates - the cross-core proof; a missing lock loses updates under
     *       real contention), and
     *   (b) the runqueue is well-formed afterward: the running count is back to
     *       the pre-stress baseline (each core's adds and removes balanced, so a
     *       lost/duplicated node would drift the count) AND the ready queue
     *       drains empty (schedule() returns idle, i.e. ready_head is NULL).
     * It is non-vacuous: if NO secondary comes up (online == 0) it FAILS rather
     * than silently passing - the cross-core proof could not run. */

    scheduler_stats_t before;
    scheduler_get_stats(&before);

    uint32_t online = 0;
    uint64_t counter = 0;
    smp_run_runqueue_stress(SMP_STRESS_ITERS, &online, &counter);

    /* Non-vacuity: a zero-core run is NOT a pass. The proof requires genuine
     * cross-core contention. */
    if (online == 0) {
        test_fail("smp_runqueue_lock",
                  "no secondary participated - cross-core proof could not run");
        return;
    }

    /* (a) The shared counter must be EXACT: every one of online*ITERS bumps
     * landed. A non-serializing (missing) lock loses updates here. */
    uint64_t expected = (uint64_t)online * (uint64_t)SMP_STRESS_ITERS;
    if (counter != expected) {
        test_fail("smp_runqueue_lock",
                  "lost counter updates - the runqueue lock did not serialize");
        return;
    }

    /* (b1) The running count must be back to baseline: the per-core add/remove
     * pairs balanced, so a lost or duplicated node would drift it. */
    scheduler_stats_t after;
    scheduler_get_stats(&after);
    if (after.running_processes != before.running_processes) {
        test_fail("smp_runqueue_lock",
                  "runqueue corrupted - running count drifted from baseline");
        return;
    }

    /* (b2) The ready queue must drain empty. On the primary scheduler.current is
     * idle (RUNNING) and the queue should be empty after the balanced stress, so
     * schedule() hits the ready_head==NULL early-return and returns idle without
     * mutating the queue - a non-destructive drain check. A dangling/duplicated
     * node would leave ready_head non-NULL and schedule() would return it. */
    process_t *drained = schedule();
    process_t *cur = process_current();
    if (drained != cur) {
        test_fail("smp_runqueue_lock",
                  "runqueue not drained - a dangling node remained in the queue");
        return;
    }

    /* The per-core dummy PCBs are static (no kfree). The registry and the
     * current process are untouched by the stress (it only used the scheduler
     * runqueue, never the registry). */
    test_pass("smp_runqueue_lock");
}

/* ============================================================================
 * SMP last_cpu (criterion 3): the PCB records the core a process last ran on.
 *
 * The NON-VACUOUS proof that last_cpu is actually WRITTEN at the run/touch
 * point (process_set_current), using the 06-05 sentinel technique. On the
 * primary smp_cpu_id() is 0 and a field left uninitialized could also be 0, so
 * "last_cpu == 0" would be vacuous. Instead we pre-set last_cpu to a SENTINEL
 * (0xFFu) that smp_cpu_id() can never return (cpu ids are 0..3 on this flat
 * board), call process_set_current, and assert last_cpu became smp_cpu_id() -
 * proving the write happened. A missing write leaves the sentinel and FAILS.
 *
 * Paired against the create-time init: process_create already set last_cpu to
 * smp_cpu_id() (asserted first), so the scenario also catches a create path
 * that left the field garbage.
 * ============================================================================ */

static void test_smp_last_cpu(void)
{
    /* A value smp_cpu_id() can never return (cpu ids are 0..3 on this flat
     * single-cluster board), so an assertion against smp_cpu_id() is real, not
     * an accidental 0-equals-0 on the primary. */
    const uint32_t LAST_CPU_SENTINEL = 0xFFu;

    uint32_t self = smp_cpu_id();

    /* Save the current process so process_set_current's global mutation is
     * restored after the test (idle is current on the primary one-shot). */
    process_t *saved_current = process_current();

    process_t *p = process_create(test_process_dummy_entry, "last_cpu_test");
    if (p == NULL) {
        test_fail("smp_last_cpu", "process_create returned NULL");
        return;
    }

    /* (1) process_create initializes last_cpu to the creating core. */
    if (p->last_cpu != self) {
        test_fail("smp_last_cpu", "process_create did not initialize last_cpu to smp_cpu_id()");
        scheduler_remove_process(p);
        process_unregister(p);
        kfree(p);
        return;
    }

    /* (2) The run/touch point WRITES last_cpu. Sentinel it to a value
     * smp_cpu_id() can never be, touch the PCB via process_set_current, and
     * assert the sentinel was overwritten with smp_cpu_id() - the non-vacuous
     * proof the write happens. */
    p->last_cpu = LAST_CPU_SENTINEL;
    process_set_current(p);

    if (p->last_cpu == LAST_CPU_SENTINEL) {
        test_fail("smp_last_cpu", "process_set_current did not write last_cpu (sentinel survived)");
        process_set_current(saved_current);
        scheduler_remove_process(p);
        process_unregister(p);
        kfree(p);
        return;
    }
    if (p->last_cpu != self) {
        test_fail("smp_last_cpu", "last_cpu not set to smp_cpu_id() at the touch point");
        process_set_current(saved_current);
        scheduler_remove_process(p);
        process_unregister(p);
        kfree(p);
        return;
    }

    /* Restore the current process the one-shot started with, then tear down the
     * test PCB (detach from both lists, free) - the 06-04/06-05 teardown. */
    process_set_current(saved_current);
    scheduler_remove_process(p);
    process_unregister(p);
    kfree(p);

    test_pass("smp_last_cpu");
}

/* ============================================================================
 * Network scenarios (FEAT-05). Three headless, non-vacuous scenarios:
 *
 *   test_net_arp_reply  - the criterion-2 builder unit. PURE: feeds the
 *     side-effect-free arp_build_reply (08-03) a synthetic inbound ARP request
 *     for OUR_IP and asserts the exact 42-byte reply, plus asserts it returns
 *     WITHOUT writing for a wrong-target-IP and a too-short input (so a blind
 *     builder fails). No device needed.
 *   test_net_icmp_echo  - the criterion-3 live round trip to the slirp gateway
 *     10.0.2.2. Drives net_ping (which arp_resolves, sends one echo, and
 *     bounded-polls for the matching type-0 reply via a got_reply sentinel that
 *     flips only on a matched id+seq). test_fails on no device / no reply, so a
 *     silent no-op FAILS. The TEST kernel has no GIC/timer, so the poll path is
 *     the ONLY path that works - exactly the production code.
 *   test_net_rx_bounds  - the malformed-frame RED gate for the dominant
 *     non-negotiable (a malformed RX frame must not fault the WM loop). Feeds
 *     net_rx_dispatch a battery of out-of-spec frames and asserts each call
 *     RETURNS (drops) without faulting - reaching test_pass after the whole
 *     battery is the proof the bounds guards hold (a missing guard faults
 *     first). Mirrors the Phase 6 oversized-segment regression scenario. No
 *     device needed (synthetic buffers).
 *
 * None is a sec_* scenario; the five sec_* lines stay the audit gate.
 * ============================================================================ */

static void test_net_arp_reply(void)
{
    /* A fixed test identity - the builder is pure, so no device is needed. */
    const uint8_t our_mac[MAC_ADDR_LEN]  = { 0x52, 0x54, 0x00, 0x12, 0x34, 0x56 };
    const uint8_t our_ip[IP_ADDR_LEN]    = { OUR_IP_0, OUR_IP_1, OUR_IP_2, OUR_IP_3 };
    const uint8_t req_mac[MAC_ADDR_LEN]  = { 0x52, 0x54, 0x00, 0x00, 0x00, 0x99 };
    const uint8_t req_ip[IP_ADDR_LEN]    = { 10, 0, 2, 99 };

    /* Build a synthetic inbound ARP request (opcode 1) FOR our IP: Ethernet
     * dst = broadcast, src = the fake requester; ARP sender = the fake MAC/IP,
     * target MAC zero, target IP = OUR_IP. */
    uint8_t req[ARP_FRAME_LEN];
    memset(req, 0, sizeof(req));
    memset(req + ETH_OFF_DST, 0xff, MAC_ADDR_LEN);
    memcpy(req + ETH_OFF_SRC, req_mac, MAC_ADDR_LEN);
    req[ETH_OFF_ETHERTYPE]     = (uint8_t)(ETHERTYPE_ARP >> 8);
    req[ETH_OFF_ETHERTYPE + 1] = (uint8_t)(ETHERTYPE_ARP & 0xff);
    uint8_t *arp = req + ETH_HDR_LEN;
    arp[ARP_OFF_HTYPE]     = 0x00; arp[ARP_OFF_HTYPE + 1]  = 0x01;   /* Ethernet */
    arp[ARP_OFF_PTYPE]     = 0x08; arp[ARP_OFF_PTYPE + 1]  = 0x00;   /* IPv4 */
    arp[ARP_OFF_HLEN]      = ARP_HLEN_ETHERNET;
    arp[ARP_OFF_PLEN]      = ARP_PLEN_IPV4;
    arp[ARP_OFF_OPCODE]    = 0x00; arp[ARP_OFF_OPCODE + 1] = ARP_OP_REQUEST;
    memcpy(arp + ARP_OFF_SENDER_MAC, req_mac, MAC_ADDR_LEN);
    memcpy(arp + ARP_OFF_SENDER_IP,  req_ip,  IP_ADDR_LEN);
    /* target MAC stays zero; target IP = OUR_IP. */
    memcpy(arp + ARP_OFF_TARGET_IP,  our_ip,  IP_ADDR_LEN);

    /* Pre-fill the output buffer with a sentinel so the return-without-writing
     * guards below can prove the builder left it UNTOUCHED. */
    uint8_t out[ARP_FRAME_LEN];
    memset(out, 0xAA, sizeof(out));

    int n = arp_build_reply(req, sizeof(req), our_mac, our_ip, out);
    if (n != (int)ARP_FRAME_LEN) {
        test_fail("net_arp_reply", "valid request did not produce a 42-byte reply");
        return;
    }

    /* Assert the exact reply bytes (08-03 contract). Ethernet: dst = the
     * requester's MAC, src = our MAC, type = ARP. */
    if (memcmp(out + ETH_OFF_DST, req_mac, MAC_ADDR_LEN) != 0) {
        test_fail("net_arp_reply", "reply Ethernet dst != requester MAC");
        return;
    }
    if (memcmp(out + ETH_OFF_SRC, our_mac, MAC_ADDR_LEN) != 0) {
        test_fail("net_arp_reply", "reply Ethernet src != our MAC");
        return;
    }
    if (out[ETH_OFF_ETHERTYPE] != 0x08 || out[ETH_OFF_ETHERTYPE + 1] != 0x06) {
        test_fail("net_arp_reply", "reply ethertype != 0x0806");
        return;
    }
    /* ARP: opcode 2; sender = us; target = the requester. */
    const uint8_t *oarp = out + ETH_HDR_LEN;
    if (oarp[ARP_OFF_OPCODE] != 0x00 || oarp[ARP_OFF_OPCODE + 1] != ARP_OP_REPLY) {
        test_fail("net_arp_reply", "reply opcode != 2");
        return;
    }
    if (memcmp(oarp + ARP_OFF_SENDER_MAC, our_mac, MAC_ADDR_LEN) != 0 ||
        memcmp(oarp + ARP_OFF_SENDER_IP,  our_ip,  IP_ADDR_LEN) != 0) {
        test_fail("net_arp_reply", "reply ARP sender != our MAC/IP");
        return;
    }
    if (memcmp(oarp + ARP_OFF_TARGET_MAC, req_mac, MAC_ADDR_LEN) != 0 ||
        memcmp(oarp + ARP_OFF_TARGET_IP,  req_ip,  IP_ADDR_LEN) != 0) {
        test_fail("net_arp_reply", "reply ARP target != requester MAC/IP");
        return;
    }

    /* NON-VACUOUS guard 1: a request whose target IP is NOT ours must return 0
     * and leave out untouched (a blind echoing builder would fail this). */
    uint8_t wrong[ARP_FRAME_LEN];
    memcpy(wrong, req, sizeof(wrong));
    uint8_t not_our_ip[IP_ADDR_LEN] = { 10, 0, 2, 50 };
    memcpy(wrong + ETH_HDR_LEN + ARP_OFF_TARGET_IP, not_our_ip, IP_ADDR_LEN);
    uint8_t out2[ARP_FRAME_LEN];
    memset(out2, 0xAA, sizeof(out2));
    if (arp_build_reply(wrong, sizeof(wrong), our_mac, our_ip, out2) != 0) {
        test_fail("net_arp_reply", "wrong-target-IP request was answered");
        return;
    }
    for (uint32_t i = 0; i < sizeof(out2); i++) {
        if (out2[i] != 0xAA) {
            test_fail("net_arp_reply", "wrong-target-IP request wrote to out");
            return;
        }
    }

    /* NON-VACUOUS guard 2: a too-short input (41 bytes) must return 0 and leave
     * out untouched (no read past the short buffer). */
    uint8_t out3[ARP_FRAME_LEN];
    memset(out3, 0xAA, sizeof(out3));
    if (arp_build_reply(req, ARP_FRAME_LEN - 1, our_mac, our_ip, out3) != 0) {
        test_fail("net_arp_reply", "too-short request was answered");
        return;
    }
    for (uint32_t i = 0; i < sizeof(out3); i++) {
        if (out3[i] != 0xAA) {
            test_fail("net_arp_reply", "too-short request wrote to out");
            return;
        }
    }

    test_pass("net_arp_reply");
}

static void test_net_icmp_echo(void)
{
    /* The device must be present - the -netdev flag on the test QEMU line
     * (08-01) makes this real. A missing device is a REAL failure, not a skip:
     * a silent skip would make criterion 3 vacuous. virtio_net_init is
     * idempotent if a prior scenario inited it; init here so the scenario is
     * self-contained. */
    if (virtio_net_init() < 0 || !virtio_net_available()) {
        test_fail("net_icmp_echo", "no net device (the -netdev flag must be on the test line)");
        return;
    }

    /* Corroborate criterion 2 with the LIVE resolve (the builder unit is the
     * primary c2 proof): the gateway ARP must resolve within the bound. */
    uint8_t gw_ip[IP_ADDR_LEN] = { GW_IP_0, GW_IP_1, GW_IP_2, GW_IP_3 };
    uint8_t gw_mac[MAC_ADDR_LEN];
    if (arp_resolve(gw_ip, gw_mac) != 0) {
        test_fail("net_icmp_echo", "ARP timeout resolving the gateway");
        return;
    }

    /* The round trip. net_ping arms a pending-ping (id+seq) under net_lock,
     * sends one ICMP echo, and bounded-polls for a type-0 reply matching id+seq
     * via a got_reply sentinel that flips ONLY on the match (08-03). It returns
     * 0 on the matched reply, -1 on the bounded-wait expiry. A silent no-op
     * (wrong queue setup, wrong header size, wrong checksum) leaves the sentinel
     * clear and net_ping returns -1 - so this assert is non-vacuous. */
    if (net_ping(gw_ip) != 0) {
        test_fail("net_icmp_echo", "no echo reply from 10.0.2.2 within the bound");
        return;
    }

    test_pass("net_icmp_echo");
}

static void test_net_rx_bounds(void)
{
    /* The dominant non-negotiable made a non-vacuous headless RED gate: feed
     * net_rx_dispatch a battery of malformed inbound frames and assert each call
     * RETURNS (drops) without faulting. A missing bounds guard would read past
     * the buffer and fault, so control would never reach the test_pass below -
     * reaching it after the whole battery IS the proof the guards hold. The drop
     * signal is exactly "control returns and no reply was sent": all three
     * frames are dropped before any answer logic (before any net_tx), so no
     * reply is even attempted. (No device needed - synthetic buffers.) */
    volatile int returned = 0;

    /* 1. A 13-byte sub-Ethernet frame: shorter than the 14-byte Ethernet
     * header, so reading the ethertype at offset 12-13 would be out of bounds
     * without the len >= 14 guard. */
    uint8_t f1[13];
    memset(f1, 0x41, sizeof(f1));
    net_rx_dispatch(f1, sizeof(f1));

    /* 2. A 41-byte sub-ARP frame: ethertype 0x0806 (ARP) but one byte short of
     * the 42-byte minimum, so the ARP body read would run past len without the
     * len >= 42 guard. */
    uint8_t f2[41];
    memset(f2, 0, sizeof(f2));
    f2[ETH_OFF_ETHERTYPE]     = (uint8_t)(ETHERTYPE_ARP >> 8);
    f2[ETH_OFF_ETHERTYPE + 1] = (uint8_t)(ETHERTYPE_ARP & 0xff);
    net_rx_dispatch(f2, sizeof(f2));

    /* 3. An IPv4 frame (ethertype 0x0800) with ver/IHL = 0x4F (IHL=15 ->
     * ihl_bytes=60, which exceeds this short frame) AND an oversized
     * total_length (far larger than the buffer), so the ICMP-payload index
     * would run past the buffer without the IHL and total_length guards. The
     * frame is 34 bytes (14 + 20) - just enough to reach the IPv4 header. */
    uint8_t f3[ETH_HDR_LEN + IP_HDR_MIN_LEN];
    memset(f3, 0, sizeof(f3));
    f3[ETH_OFF_ETHERTYPE]     = (uint8_t)(ETHERTYPE_IPV4 >> 8);
    f3[ETH_OFF_ETHERTYPE + 1] = (uint8_t)(ETHERTYPE_IPV4 & 0xff);
    uint8_t *ip = f3 + ETH_HDR_LEN;
    ip[IP_OFF_VER_IHL]       = 0x4F;     /* version 4, IHL 15 -> 60 bytes */
    ip[IP_OFF_TOTAL_LEN]     = 0xFF;     /* total_length = 0xFFFF, way past len */
    ip[IP_OFF_TOTAL_LEN + 1] = 0xFF;
    ip[IP_OFF_PROTO]         = IP_PROTO_ICMP;
    net_rx_dispatch(f3, sizeof(f3));

    /* Reaching here means all three calls returned with no fault. */
    returned = 1;
    if (returned != 1) {
        test_fail("net_rx_bounds", "net_rx_dispatch did not return from a malformed frame");
        return;
    }

    test_pass("net_rx_bounds");
}

/* ============================================================================
 * Entry point
 *
 * Replaces kernel_main from main.c when TEST=1. Brings up only the subsystems
 * the tests need, runs everything, prints a summary, and exits via
 * semihosting so `make test` can read the status code.
 * ============================================================================ */

void kernel_main(void *dtb_addr)
{
    (void)dtb_addr;

    uart_init();

    klog_info("==== AEOS test runner starting ====");

    mm_init();

    /* Enable the MMU and caches right after the heap is up, mirroring the
     * production kernel_main in main.c. Without this the TEST kernel runs
     * MMU-off and the suite proves nothing about the MMU: every scenario below
     * (PMM, heap, VFS, the framebuffer readback) then executes under virtual
     * addressing, and the dedicated MMU scenarios assert the enable directly. */
    vmm_init();
    vmm_report();

    interrupts_init();
    semihost_init();

    /* The process subsystem owns the per-process fd table that vfs_open
     * requires, so bring it up before anything that touches the VFS. */
    process_init();
    scheduler_init();

    /* Subsystem-light tests first so a memory bug doesn't take everything else
     * down with it. */
    test_pmm_alloc_free();
    test_pmm_multi_page();
    test_heap_kmalloc_kfree();
    test_heap_kfree_null();
    test_heap_balanced();

    /* Spinlock acquire/release RED gate. Needs no subsystem beyond the lock
     * itself, so it runs right after the heap block. */
    test_spinlock_uncontended();

    /* The kprintf ring-lock panic-path bypass gate (FEAT-04). Takes the ring
     * lock then drives kprintf_ring_walk on this core and asserts it RETURNS -
     * proving the panic path's trylock-or-bypass does not deadlock behind a held
     * lock. Needs only kprintf (uart_init ran above). NOT a sec_* scenario. */
    test_kprintf_ring_panic_bypass();

    /* Security smoke scenarios that only need the heap, the stack-guard
     * sentinel, and the editor seam run alongside the heap tests. */
    test_sec_kcalloc_overflow();
    test_sec_stack_guard();
    test_sec_double_free_after_merge();
    test_sec_editor_growth_overflow();

    /* MMU enable proof (vmm_init ran above): assert SCTLR_EL1.M=1 and that the
     * TTBR1 high-half alias matches the identity read, before the heavier
     * suites so a mapping failure surfaces early. */
    test_mmu_enabled();
    test_mmu_ttbr1_alias();

    /* EL0 round trip + privileged-instruction trap (FEAT-02). vmm_init and
     * scheduler_init ran above, so the user-page mapper and process_current()
     * are available; no GIC/timer here means the DAIF-masked one-shot is
     * deterministic. */
    test_el0_roundtrip();
    test_el0_priv_trap();

    /* VFS tests need a mounted ramfs. If setup fails, skip the VFS suite
     * outright and record one failure so the run is correctly marked bad. */
    if (test_vfs_setup() == 0) {
        test_vfs_create_read_write();
        test_vfs_unlink();
        test_sec_vfs_path_too_long();

        /* ELF loader scenarios (FEAT-03). They need the mounted ramfs to write
         * the embedded ELF (load-run) and a junk file (reject). The DAIF-masked
         * one-shot the loader enters is deterministic - no GIC/timer here. */
        test_elf_load_run();
        test_elf_reject_malformed();
        test_elf_reject_oversized_segment();
    } else {
        test_fail("vfs_setup", "could not register/mount ramfs");
    }

    test_process_create_remove();

    /* FEAT-03 criterion 4 (headless, non-vacuous). Runs after the ELF scenarios:
     * register a user process, arm its kill flag by pid, enter EL0, and assert
     * the seam reaped the run at the first svc before getpid dispatched. */
    test_process_kill_reap();

    /* FEAT-05 (the net stack). The arp-reply builder unit and the rx-bounds gate
     * are PURE/synthetic-buffer scenarios that need nothing beyond the heap +
     * the stack, so they run here on the primary. The icmp-echo round trip needs
     * the virtio-net device the -netdev flag on the test QEMU line provides, and
     * the poll path is the ONLY path that works without a GIC/timer - so it
     * exercises exactly the production code. None is a sec_* scenario. */
    test_net_arp_reply();
    test_net_icmp_echo();
    test_net_rx_bounds();

    /* FEAT-04 criterion 2 (the headline). Brings up REAL secondaries that
     * concurrently hammer the self-locking runqueue mutators + a shared counter,
     * and asserts the counter is exact (zero lost updates) AND the runqueue stays
     * well-formed - the genuinely cross-core proof of the runqueue lock. Runs
     * after the process scenarios; the stress secondaries park (wfe) afterward so
     * the remaining scenarios run on the primary undisturbed. */
    test_smp_runqueue_lock();

    /* FEAT-04 criterion 3 (the ps CPU column data half). Proves last_cpu is
     * WRITTEN at the run/touch point non-vacuously: a 0xFF sentinel (a value
     * smp_cpu_id() can never return) is overwritten by process_set_current. NOT
     * a sec_* scenario. */
    test_smp_last_cpu();

    test_shell_parse_basic();
    test_shell_parse_whitespace();
    test_shell_parse_empty();

    test_symbol_lookup_known();
    test_symbol_lookup_below_first();

    test_fb_init_and_fill();

    kprintf("\n");
    klog_info("==== TEST RESULTS: %u PASSED, %u FAILED ====",
             test_pass_count, test_fail_count);

    semihost_exit(test_fail_count == 0 ? 0 : 1);
}

/* ============================================================================
 * End of test_runner.c
 * ============================================================================ */

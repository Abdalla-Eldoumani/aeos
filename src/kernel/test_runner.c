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
        return;
    }

    /* Detach so the test runner's exit path doesn't try to schedule it. */
    scheduler_remove_process(p);

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
    } else {
        test_fail("vfs_setup", "could not register/mount ramfs");
    }

    test_process_create_remove();

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

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
    process_t *p = process_create(test_process_dummy_entry, "test_proc");
    if (p == NULL) {
        test_fail("process_create_remove", "process_create returned NULL");
        return;
    }
    if (p->pid == 0) {
        test_fail("process_create_remove", "PID 0 returned");
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

    /* VFS tests need a mounted ramfs. If setup fails, skip the VFS suite
     * outright and record one failure so the run is correctly marked bad. */
    if (test_vfs_setup() == 0) {
        test_vfs_create_read_write();
        test_vfs_unlink();
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

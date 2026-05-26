/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: src/kernel/main.c
 * Description: Kernel main entry point
 * ============================================================================ */

#include <aeos/uart.h>
#include <aeos/kprintf.h>
#include <aeos/types.h>
#include <aeos/mm.h>
#include <aeos/pmm.h>
#include <aeos/heap.h>
#include <aeos/vmm.h>
#include <aeos/interrupts.h>
#include <aeos/gic.h>
#include <aeos/timer.h>
#include <aeos/process.h>
#include <aeos/scheduler.h>
#include <aeos/smp.h>
#include <aeos/syscall.h>
#include <aeos/vfs.h>
#include <aeos/ramfs.h>
#include <aeos/fs_persist.h>
#include <aeos/string.h>
#include <aeos/framebuffer.h>
#include <aeos/dtb.h>
#include <aeos/ramfb.h>
#include <aeos/virtio_gpu.h>
#include <aeos/virtio_net.h>
#include <aeos/net.h>
#include <aeos/pflash.h>
#include <aeos/semihosting.h>
#include <aeos/shell.h>
#include <aeos/bootscreen.h>
#include <aeos/gui.h>
#include <aeos/usermode.h>
#include <aeos/exec.h>

/* External symbols from linker script */
extern char _kernel_start;
extern char _kernel_end;
extern char __bss_start;
extern char __bss_end;
extern char __stack_top;

/* External symbols from vectors.asm */
extern uint64_t exception_counters[16];

/* The embedded static ELF (objcopy -I binary) - the EL0 test binary built and
 * embedded in 06-02. Written into ramfs at /hello in test_vfs, then run once on
 * the boot path via elf_exec_file to prove the loader on the production boot. */
extern const unsigned char _binary_hello_elf_start[];
extern const unsigned char _binary_hello_elf_end[];

/**
 * Display AEOS banner
 */
static void display_banner(void)
{
    kprintf("\n");
    kprintf("========================================\n");
    kprintf("  AEOS - Abdalla's Educational OS\n");
    kprintf("  ARMv8-A AArch64 Kernel\n");
    kprintf("========================================\n");
    kprintf("\n");
}

/**
 * Display kernel memory layout information
 */
static void display_memory_info(void)
{
    klog_info("Kernel memory layout:");
    kprintf("  Kernel start: %p\n", &_kernel_start);
    kprintf("  Kernel end:   %p\n", &_kernel_end);
    kprintf("  BSS start:    %p\n", &__bss_start);
    kprintf("  BSS end:      %p\n", &__bss_end);
    kprintf("  Stack top:    %p\n", &__stack_top);

    /* Calculate kernel size */
    size_t kernel_size = (size_t)(&_kernel_end - &_kernel_start);
    kprintf("  Kernel size:  %u bytes (%u KB)\n",
            (unsigned int)kernel_size,
            (unsigned int)(kernel_size / 1024));
}

/**
 * Test kernel printf functionality
 */
static void test_kprintf(void)
{
    klog_info("Testing kprintf functionality:");

    /* Test various format specifiers */
    kprintf("  Decimal: %d\n", 42);
    kprintf("  Negative: %d\n", -123);
    kprintf("  Unsigned: %u\n", 4294967295U);
    kprintf("  Hex (lower): 0x%x\n", 0xDEADBEEF);
    kprintf("  Hex (upper): 0x%X\n", 0xCAFEBABE);
    kprintf("  String: %s\n", "Hello, AEOS!");
    kprintf("  Character: %c\n", 'A');
    kprintf("  Percent: 100%%\n");

    /* Test NULL pointer handling */
    kprintf("  NULL string: %s\n", NULL);

    klog_debug("This is a debug message");
    klog_info("This is an info message");
    klog_warn("This is a warning message");
    klog_error("This is an error message");
}

/**
 * Test Physical Memory Manager
 */
static void test_pmm(void)
{
    uint64_t page1, page2, page3, page4;
    pmm_stats_t stats;

    klog_info("Testing Physical Memory Manager:");

    /* Get initial stats */
    pmm_get_stats(&stats);
    kprintf("  Initial free pages: %u\n", (uint32_t)stats.free_pages);

    /* Allocate single pages */
    page1 = pmm_alloc_page();
    page2 = pmm_alloc_page();
    kprintf("  Allocated page1: %p\n", (void *)page1);
    kprintf("  Allocated page2: %p\n", (void *)page2);

    /* Allocate multi-page blocks */
    page3 = pmm_alloc_pages(2);  /* 4 pages = 16KB */
    kprintf("  Allocated 4 pages: %p\n", (void *)page3);

    /* Check stats after allocation */
    pmm_get_stats(&stats);
    kprintf("  Free pages after alloc: %u\n", (uint32_t)stats.free_pages);

    /* Free pages */
    pmm_free_page(page1);
    pmm_free_page(page2);
    pmm_free_pages(page3, 2);
    klog_info("Freed all allocated pages");

    /* Verify pages were freed */
    pmm_get_stats(&stats);
    kprintf("  Free pages after free: %u\n", (uint32_t)stats.free_pages);

    /* Test large allocation */
    page4 = pmm_alloc_pages(5);  /* 32 pages = 128KB */
    if (page4) {
        kprintf("  Allocated 32 pages: %p\n", (void *)page4);
        pmm_free_pages(page4, 5);
        klog_info("Freed large allocation");
    }
}

/**
 * Test kernel heap allocator
 */
static void test_heap(void)
{
    void *ptr1, *ptr2, *ptr3;
    char *str;
    heap_stats_t stats;
    int i;

    klog_info("Testing Kernel Heap Allocator:");

    /* Get initial stats */
    heap_get_stats(&stats);
    kprintf("  Initial heap: %u KB free\n", (uint32_t)(stats.free_size / 1024));

    /* Test kmalloc */
    ptr1 = kmalloc(64);
    ptr2 = kmalloc(128);
    ptr3 = kmalloc(256);
    kprintf("  Allocated 3 blocks: %p, %p, %p\n", ptr1, ptr2, ptr3);

    /* Test kcalloc (zero-initialized) */
    str = (char *)kcalloc(16, sizeof(char));
    if (str) {
        kprintf("  kcalloc: ");
        for (i = 0; i < 16; i++) {
            kprintf("%d", str[i]);  /* Should all be 0 */
        }
        kprintf(" (should be all zeros)\n");

        /* Write test string */
        str[0] = 'H';
        str[1] = 'e';
        str[2] = 'l';
        str[3] = 'l';
        str[4] = 'o';
        str[5] = '\0';
        kprintf("  String test: %s\n", str);
    }

    /* Check stats */
    heap_get_stats(&stats);
    kprintf("  Used heap: %u bytes\n", (uint32_t)stats.used_size);
    kprintf("  Free heap: %u KB\n", (uint32_t)(stats.free_size / 1024));

    /* Free memory */
    kfree(ptr1);
    kfree(ptr2);
    kfree(ptr3);
    kfree(str);
    klog_info("Freed all heap allocations");

    /* Verify heap is freed */
    heap_get_stats(&stats);
    kprintf("  Free heap after free: %u KB\n", (uint32_t)(stats.free_size / 1024));
}

/**
 * Initialize graphical display
 * Returns true if graphical mode available
 */
static bool init_graphics(void)
{
    klog_info("Initializing graphical display...");

    /* Initialize framebuffer */
    if (fb_init() < 0) {
        klog_error("Failed to initialize framebuffer");
        return false;
    }

    /* Try virtio-gpu */
    if (virtio_gpu_init() == 0) {
        klog_info("VirtIO GPU driver initialized");

        /* Set up the display with our framebuffer */
        if (virtio_gpu_update_display() == 0) {
            klog_info("Display activated - graphics visible in window");
            return true;
        } else {
            klog_warn("Failed to activate display");
        }
    } else {
        klog_warn("VirtIO GPU not available");
    }

    klog_info("Graphics rendered to memory only");
    return false;
}

/**
 * Test Virtual File System
 */
static void test_vfs(void)
{
    vfs_filesystem_t *ramfs;
    int load_ret;

    klog_info("Testing Virtual File System:");

    /* Initialize semihosting for filesystem persistence */
    semihost_init();

    /* Create ramfs */
    ramfs = ramfs_create();
    if (ramfs == NULL) {
        klog_error("Failed to create ramfs");
        return;
    }

    /* Try to load saved filesystem from host via semihosting */
#ifdef FS_NO_LOAD
    kprintf("  [INFO] Fresh filesystem mode (FS_NO_LOAD defined)\n");
    load_ret = -1;
#else
    kprintf("  Attempting to load filesystem from host...\n");
    load_ret = fs_load_from_disk(ramfs);

    if (load_ret == 0) {
        kprintf("  [OK] Loaded filesystem from host (aeos_fs.img)\n");
    } else {
        kprintf("  [INFO] No saved filesystem on host, creating fresh filesystem\n");
        /* ramfs_create already created an empty root, so we're good */
    }
#endif

    vfs_register_filesystem(ramfs);
    kprintf("  Registered ramfs\n");

    /* Mount ramfs as root */
    if (vfs_mount("/", ramfs) < 0) {
        klog_error("Failed to mount root filesystem");
        return;
    }
    kprintf("  Mounted ramfs at /\n");

    klog_info("VFS initialized successfully!");
}

/**
 * Write the embedded static ELF into ramfs at /hello. Called AFTER
 * scheduler_init: vfs_open allocates the fd through process_current()'s fd
 * table, which only exists once the idle process is created in scheduler_init,
 * so this cannot run inside test_vfs (which precedes process/scheduler init).
 * The file lives only in ramfs (it is not persisted to the host image), and is
 * available to both the boot-path exec and an interactive `exec /hello`. A write
 * failure is logged and skipped, not fatal - the exec then reports not-found and
 * boot continues (reaching the WM loop is the dominant non-negotiable). */
static void write_embedded_hello(void)
{
    uint64_t hello_size =
        (uint64_t)(_binary_hello_elf_end - _binary_hello_elf_start);
    int hfd = vfs_open("/hello", O_CREAT | O_WRONLY | O_TRUNC, 0755);
    if (hfd < 0) {
        klog_error("Failed to create /hello (embedded ELF)");
        return;
    }
    vfs_write(hfd, _binary_hello_elf_start, (size_t)hello_size);
    vfs_close(hfd);
    klog_info("Wrote embedded /hello (%u bytes)", (uint32_t)hello_size);
}

/**
 * Read current exception level
 */
static uint32_t get_current_el(void)
{
    uint64_t el;
    __asm__ volatile("mrs %0, CurrentEL" : "=r"(el));
    return (uint32_t)((el >> 2) & 0x3);
}

/**
 * Display CPU information
 */
static void display_cpu_info(void *dtb_addr)
{
    uint32_t el = get_current_el();
    klog_info("CPU Information:");
    kprintf("  Current Exception Level: EL%u\n", el);
    kprintf("  Architecture: AArch64 (ARMv8-A)\n");
    kprintf("  Device Tree Blob at: %p\n", dtb_addr);
}

/**
 * Kernel main entry point
 * Called from boot.asm after basic initialization
 *
 * @param dtb_addr Device tree blob address (passed in x0)
 */
void kernel_main(void *dtb_addr)
{
    bool graphical_mode = false;
    bool use_gui = false;

    /* Initialize UART for console output */
    uart_init();

    /* Display banner */
    display_banner();

    /* Display CPU information */
    display_cpu_info(dtb_addr);

    /* Display memory layout */
    display_memory_info();

    /* Initialize memory management */
    kprintf("\n");
    mm_init();

    /* Enable the MMU and caches right after the heap is up and before any
     * driver, the GIC, or the first timer FIQ, so every later MMIO and DMA
     * access runs under the final mapping. The kernel is identity-mapped, so
     * the PC is unchanged across the enable. */
    vmm_init();
    vmm_report();

    /* Initialize exception vector table */
    kprintf("\n");
    klog_info("Installing exception vector table...");
    interrupts_init();

    /* Initialize GIC */
    kprintf("\n");
    klog_info("Initializing interrupt subsystem...");
    gic_init();
    klog_info("GIC initialized");

    /* Initialize timer */
    timer_init();

    /* Enable interrupts and start timer */
    klog_info("Enabling IRQs...");
    interrupts_enable();
    timer_start();
    klog_info("Timer started - preemptive scheduling active");

    /* Test basic functionality */
    kprintf("\n");
    test_kprintf();
    kprintf("\n");
    test_pmm();
    kprintf("\n");
    test_heap();

    /* Initialize Virtual File System */
    kprintf("\n");
    klog_info("Initializing Virtual File System...");
    vfs_init();
    test_vfs();

    /* Initialize graphics */
    kprintf("\n");
    graphical_mode = init_graphics();

    /* Initialize Process Management */
    kprintf("\n");
    klog_info("Initializing Process Management...");
    process_init();
    scheduler_init();
    klog_info("Process management initialized");

    /* Bring up the secondary cores (FEAT-04). smp_init CPU_ONs cores 1..3 via
     * PSCI with a BOUNDED handshake and ALWAYS returns: a CPU_ON failure or a
     * secondary that does not report within the spin bound is logged and skipped,
     * so a stuck secondary can NEVER block the primary's path to the WM loop (the
     * dominant non-negotiable). No wait or fail-if-not-all-online gate is added
     * here - a partial bringup is fine, the primary proceeds regardless.
     *
     * Placed AFTER gic_init/timer_init/interrupts_enable (so the GIC distributor
     * + the per-core GICC path exist for the secondaries to attach to) and after
     * scheduler_init (so the process registry exists for the per-core idle
     * markers smp_init registers), and BEFORE the EL0 block / shell / GUI branch.
     * Each online core gets a registry-only "idle/cpuN" PCB so ps lists cores
     * 0..3; the secondaries then park in wfe (no scheduled work). The "smp: core
     * N online" prints race the primary's kprintf - the ring lock that orders
     * them is a later plan; the markers are still present. */
    kprintf("\n");
    klog_info("Bringing up secondary cores...");
    smp_init();

    /* Initialize System Calls */
    kprintf("\n");
    klog_info("Initializing System Calls...");
    syscall_init();

    /* Probe the virtio-net device (FEAT-05 criterion 1: MAC on serial). It is
     * POLL-DRIVEN (no IRQ): the criterion-3 TEST kernel_main has no GIC/timer,
     * so an IRQ path could never fire there, and the same poll path runs in
     * both builds. Placed after the heap + scheduler are up so its DMA ring
     * buffers can be kmalloc'd; ordering vs gic/timer is irrelevant because no
     * IRQ is registered. A -1 (no device) is logged and IGNORED so a boot
     * WITHOUT -device virtio-net-device (e.g. text-mode make run) still reaches
     * the WM loop - the dominant non-negotiable (RESEARCH Q2 + Pitfall 6). */
    kprintf("\n");
    klog_info("Probing virtio-net (FEAT-05)...");
    if (virtio_net_init() < 0) {
        klog_warn("virtio-net absent - continuing boot");
    }

    /* Prove the EL0/EL1 boundary once on the production boot path (FEAT-02).
     * The payload runs at EL0, issues svc #0 for getpid then exit; the serial
     * shows the round trip (entered EL0 / svc N from EL0 / returned to kernel).
     * SPSR=0x3C0 masks IRQ/FIQ for the payload's brief life so the running timer
     * cannot preempt it; the masked FIQ is taken once control is back at EL1.
     * One-shot only - never looped - and boot continues to the WM loop after. */
    kprintf("\n");
    klog_info("Running EL0 round trip...");
    usermode_run_payload(USERMODE_PAYLOAD_ROUNDTRIP);

    /* Run the embedded static ELF once on the production boot path (FEAT-03):
     * the loader reads /hello from ramfs, maps its PT_LOAD segments into the EL0
     * window, and enters EL0 at e_entry; the binary writes "hello, EL0!" via
     * sys_write and exits, control returns here, and boot continues. This is the
     * criterion-3 production proof. A negative return is logged and IGNORED -
     * never halt - because reaching the WM loop is the dominant non-negotiable.
     * One-shot only, never looped. The embedded ELF is written into ramfs here
     * (not in test_vfs) because vfs_open needs the idle process's fd table,
     * which scheduler_init created above. */
    kprintf("\n");
    write_embedded_hello();
    klog_info("Running exec /hello (FEAT-03)...");
    int hello_rc = elf_exec_file("/hello");
    if (hello_rc < 0) {
        klog_warn("exec /hello failed (%d) - continuing boot", hello_rc);
    }

    /* One-shot boot-path ICMP echo to the slirp gateway 10.0.2.2 (FEAT-05
     * criterion 3, the production serial proof). Mirrors the /hello EL0 demo
     * above: it runs ONCE, never looped, and is gated on the device being
     * present so a normal boot is not spammy (the MAC line from the probe + this
     * one result line, no per-frame trace). net_ping is bounded internally, so
     * this cannot block; a timeout or an absent device is klog_warn'd and
     * IGNORED so boot ALWAYS reaches the WM loop (T-08-12, the dominant
     * non-negotiable - BOOT-GATE 2 proves the absent path). */
    kprintf("\n");
    if (virtio_net_available()) {
        uint8_t gw_ip[IP_ADDR_LEN] = { GW_IP_0, GW_IP_1, GW_IP_2, GW_IP_3 };
        klog_info("Pinging gateway 10.0.2.2 (FEAT-05)...");
        if (net_ping(gw_ip) == 0) {
            klog_info("ping 10.0.2.2: reply");
        } else {
            klog_warn("ping 10.0.2.2: no reply - continuing boot");
        }
    } else {
        klog_info("ping demo skipped - no network device");
    }

    /* Initialize Shell */
    kprintf("\n");
    klog_info("Initializing Shell...");
    shell_init();

    /* If graphics available, show boot screen and launch GUI */
    if (graphical_mode) {
        kprintf("\n");
        klog_info("Graphical mode available!");

        /* Initialize and show boot screen */
        bootscreen_init();

        /* Update boot progress */
        bootscreen_update(BOOT_STAGE_MEMORY);
        bootscreen_update(BOOT_STAGE_INTERRUPTS);
        bootscreen_update(BOOT_STAGE_TIMER);
        bootscreen_update(BOOT_STAGE_FILESYSTEM);
        bootscreen_update(BOOT_STAGE_PROCESSES);
        bootscreen_update(BOOT_STAGE_INPUT);
        bootscreen_update(BOOT_STAGE_DESKTOP);

        /* Check if user wants GUI or text mode */
        use_gui = bootscreen_complete();

        if (use_gui) {
            klog_info("Starting graphical desktop environment...");

            /* Initialize GUI */
            if (gui_init() == 0) {
                /* Run GUI - this runs until user exits */
                gui_run();

                /* After GUI exits, fall through to text shell */
                klog_info("Returning to text shell...");
            } else {
                klog_error("Failed to initialize GUI, falling back to shell");
            }
        } else {
            klog_info("Text mode selected by user");
        }
    }

    /* Text mode: Run shell */
    kprintf("\n");
    klog_info("==================================================");
    klog_info("  AEOS Interactive Shell");
    klog_info("  Type 'help' for available commands");
    if (graphical_mode) {
        klog_info("  Type 'startx' to launch GUI desktop");
    }
    klog_info("==================================================");
    kprintf("\n");

    /* Start the shell - this never returns */
    shell_run();

    /* Should never reach here */
    klog_fatal("shell_run() returned!");
    while (1) {
        __asm__ volatile("wfi");
    }
}

/* ============================================================================
 * End of main.c
 * ============================================================================ */

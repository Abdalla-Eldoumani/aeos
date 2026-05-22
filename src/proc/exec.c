/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: src/proc/exec.c
 * Description: Static ELF64 loader/runner. elf_exec_file reads a static ELF off
 *              the VFS into a size-bounded buffer, validates the header, maps
 *              each PT_LOAD into the single EL0 window (USER_TEXT for PF_X code,
 *              USER_DATA otherwise) with BSS zero-fill, maps a fresh EL0 stack
 *              above the highest segment, and enters EL0 at e_entry by reusing
 *              the proven Phase 5 usermode_enter one-shot (NOT
 *              usermode_run_payload, which hardcodes the compile-time payload
 *              VAs). The reject-never-fault discipline of elf_validate is
 *              continued by per-segment bounds checks before any page is mapped.
 * ============================================================================ */

#include <aeos/exec.h>
#include <aeos/elf.h>
#include <aeos/vmm.h>
#include <aeos/pmm.h>
#include <aeos/heap.h>
#include <aeos/vfs.h>
#include <aeos/usermode.h>
#include <aeos/process.h>
#include <aeos/kprintf.h>
#include <aeos/string.h>
#include <aeos/types.h>

/* Reject before kmalloc above this size (DoS guard, SEC-03 discipline). A real
 * static AArch64 test binary is a few KB; 16 MB is generous and bounds the
 * heap allocation an attacker-influenced file size could drive. */
#define EXEC_MAX_ELF_SIZE   (16u * 1024u * 1024u)

/* The EL0 user window base: TTBR0 L1 index 2 (0x80000000). The L1 entry covers
 * a 1GB region, but the loader's REAL mappable region is much smaller: see
 * USER_L3_TOP below. */
#define USER_WINDOW_BASE    0x80000000ULL

/* The single 2MB region the loader can actually map. vmm_map_user_page backs the
 * window with ONE static user_l3[512] = 512 pages = 2MB at [0x80000000,
 * 0x80200000). Two VAs in different 2MB regions collapse onto the same L3 leaf
 * (CR-01/CR-03), so the loader rejects any segment or stack page reaching past
 * this ceiling. The 1GB L1 window is NOT the mappable extent - only this 2MB L3
 * is. The advertised USER_WINDOW_TOP (0xC0000000) was a correctness trap: it let
 * the validator accept p_memsz the mapper could not honor. It is retired; all
 * bounds checks below validate against USER_L3_TOP. */
#define USER_L3_TOP         (USER_WINDOW_BASE + 0x200000ULL)   /* 0x80200000 */

/* Extent of the currently-mapped EL0 window, published for the syscall layer's
 * user-pointer bound check. Set by elf_exec_file when it maps segments + stack;
 * valid while an EL0 one-shot is in flight. */
static uint64_t user_map_base;
static uint64_t user_map_end;

/* The registry PCB for the EL0 program currently running (set by elf_exec_file
 * for the duration of usermode_enter, NULL otherwise). The syscall layer reads
 * its kill_requested flag at the EL0 syscall boundary - this is the LOADED
 * process, NOT process_current() (which is idle during the synchronous one-shot,
 * so reading it would be a tautology and kill <pid> would never reap the run). */
static process_t *current_user_proc;

/* Physical pages backing the EL0 mapping (segments + stack), tracked so they
 * are returned to the pmm after the run - closing the page leak across repeated
 * exec calls. With the USER_L3_TOP 2MB ceiling the whole mapped extent is at
 * most 512 segment pages + 1 stack page, so the array is sized to the real
 * capacity and the tracker FAILS CLOSED (rejects the run) if it would ever
 * overflow, rather than silently dropping a page off the free list (the old
 * cap-and-drop leaked every page past the cap, CR-02). */
#define EXEC_MAX_USER_PAGES 512
static uint64_t mapped_pas[EXEC_MAX_USER_PAGES + 1];   /* +1 for the stack page */
static unsigned mapped_count;

process_t *current_user_proc_get(void)
{
    return current_user_proc;
}

#ifdef TEST_BUILD
/* Point the kill seam at a process WITHOUT a full elf_exec_file run. elf_exec_file
 * is the only production setter of current_user_proc; test_process_kill_reap uses
 * this hook to arm the seam against a user_proc_register'd PCB before entering EL0
 * with a bare payload, then clears it (set NULL) after. TEST_BUILD only - the
 * production loader still owns the pointer's lifecycle. */
void current_user_proc_set(process_t *proc)
{
    current_user_proc = proc;
}
#endif /* TEST_BUILD */

uint64_t usermode_map_base(void)
{
    return user_map_base;
}

uint64_t usermode_map_end(void)
{
    return user_map_end;
}

/* Return every tracked page to the pmm and reset the count. Called on every
 * error path after the first page is mapped, and after a completed run, so a
 * reject never leaks the segment/stack pages it already allocated (CR-02). */
static void free_mapped_pages(void)
{
    for (unsigned k = 0; k < mapped_count; k++) {
        pmm_free_page(mapped_pas[k]);
    }
    mapped_count = 0;
}

/* Round a VA up to the next 4KB boundary. */
static inline uint64_t round_up_page(uint64_t va)
{
    return (va + 0xFFFULL) & ~0xFFFULL;
}

/* One-at-a-time guard (WR-02). The loader's window/page state (user_map_base/
 * end, mapped_pas[], mapped_count, current_user_proc) is file-static and not
 * re-entrancy safe: a nested elf_exec_file would reset mapped_count and the
 * outer cleanup would then free the inner run's pages and leak its own. The
 * Scope B model is documented one-at-a-time; this flag fails closed on re-entry
 * instead of silently corrupting that state. Set/cleared at a SINGLE point by
 * the elf_exec_file wrapper around elf_exec_file_inner so every inner return
 * path clears it. */
static bool exec_in_flight;

static int elf_exec_file_inner(const char *path);

/**
 * Public entry: enforce the one-at-a-time guard, then run the loader. See exec.h.
 */
int elf_exec_file(const char *path)
{
    if (exec_in_flight) {
        klog_error("exec: refusing re-entrant load of %s (one EL0 program at a time)", path);
        return -9;
    }
    exec_in_flight = true;
    int rc = elf_exec_file_inner(path);
    exec_in_flight = false;
    return rc;
}

/**
 * Load, validate, map, and run a static ELF64 at EL0. See exec.h. The flow:
 * vfs read into a size-bounded buffer -> elf_validate -> per-PT_LOAD bounds +
 * map (copy segment bytes to the per-page PA, NOT the user VA) -> dynamic EL0
 * stack -> usermode_enter. Every reject path frees what it allocated and
 * returns negative without entering EL0; no path dereferences user memory.
 */
static int elf_exec_file_inner(const char *path)
{
    /* 1. Open the file read-only. */
    int fd = vfs_open(path, O_RDONLY, 0);
    if (fd < 0) {
        klog_error("exec: cannot open %s", path);
        return -1;
    }

    /* 2. Size from the inode, bounded BEFORE any allocation (SEC-03). */
    vfs_file_t *file = vfs_fd_to_file(fd);
    if (file == NULL || file->inode == NULL) {
        klog_error("exec: %s has no inode", path);
        vfs_close(fd);
        return -2;
    }
    uint64_t size = file->inode->size;
    if (size < sizeof(Elf64_Ehdr) || size > EXEC_MAX_ELF_SIZE) {
        klog_error("exec: %s size %llu out of bounds", path, (unsigned long long)size);
        vfs_close(fd);
        return -2;
    }

    /* 3. Read the whole file into a heap buffer, then close the fd. */
    unsigned char *buf = (unsigned char *)kmalloc(size);
    if (buf == NULL) {
        klog_error("exec: out of memory loading %s (%llu bytes)", path,
                   (unsigned long long)size);
        vfs_close(fd);
        return -3;
    }
    ssize_t got = vfs_read(fd, buf, size);
    vfs_close(fd);
    if (got < 0 || (uint64_t)got < size) {
        klog_error("exec: short read on %s (%d of %llu)", path, (int)got,
                   (unsigned long long)size);
        kfree(buf);
        return -4;
    }

    /* 4. Header gate (criterion 1 via the real loader). elf_validate already
     * logged the specific reason; never maps anything on failure. */
    if (elf_validate(buf, size) != 0) {
        klog_error("exec: %s is not a loadable static AArch64 ELF64", path);
        kfree(buf);
        return -5;
    }

    /* 5. Map each PT_LOAD. Bounds-check every segment against the file size AND
     * the EL0 window before mapping or copying a single byte. */
    const Elf64_Ehdr *e  = (const Elf64_Ehdr *)buf;
    const Elf64_Phdr *ph = (const Elf64_Phdr *)(buf + e->e_phoff);
    user_map_base = USER_WINDOW_BASE;
    uint64_t top_seg_end = USER_WINDOW_BASE;
    mapped_count = 0;

    for (unsigned i = 0; i < e->e_phnum; i++) {
        if (ph[i].p_type != PT_LOAD) {
            continue;
        }

        uint64_t off    = ph[i].p_offset;
        uint64_t filesz = ph[i].p_filesz;
        uint64_t memsz  = ph[i].p_memsz;
        uint64_t vaddr  = ph[i].p_vaddr;

        /* File-extent bounds (overflow-safe). */
        if (off + filesz < off || off + filesz > size) {
            klog_error("exec: %s segment %u file range out of bounds", path, i);
            free_mapped_pages();
            kfree(buf);
            return -11;
        }
        /* memsz must cover filesz (the BSS tail is [filesz, memsz)). */
        if (memsz < filesz) {
            klog_error("exec: %s segment %u memsz < filesz", path, i);
            free_mapped_pages();
            kfree(buf);
            return -12;
        }
        /* Window bounds (overflow-safe). The mappable region is the single 2MB
         * L3 [USER_WINDOW_BASE, USER_L3_TOP), NOT the 1GB L1 window: a segment
         * crossing USER_L3_TOP would alias an earlier 2MB band's L3 leaf and run
         * a corrupt mapping at EL0 (CR-01). Reject below the base or past the
         * real ceiling, and reject the vaddr+memsz unsigned overflow. */
        if (vaddr < USER_WINDOW_BASE || vaddr + memsz < vaddr ||
            vaddr + memsz > USER_L3_TOP) {
            klog_error("exec: %s segment %u exceeds the mapped 2MB user window", path, i);
            free_mapped_pages();
            kfree(buf);
            return -13;
        }
        /* Segments must be strictly ascending and non-overlapping (WR-01).
         * top_seg_end is the rounded-up end of the previous mapped segment
         * (starts at USER_WINDOW_BASE). Rejecting vaddr < top_seg_end stops two
         * PT_LOADs from double-mapping a page (the second leaf would clobber the
         * first's PA + permissions, leaking the first PA in place and defeating
         * the USER_TEXT W^X intent) and bounds the total page count. */
        if (vaddr < top_seg_end) {
            klog_error("exec: %s segment %u overlaps a prior segment", path, i);
            free_mapped_pages();
            kfree(buf);
            return -14;
        }
        /* Page-aligned p_vaddr only this phase (the in-scope binary is aligned;
         * the unaligned general case is deferred per RESEARCH). */
        if ((vaddr & 0xFFFULL) != 0) {
            klog_error("exec: %s segment %u unaligned vaddr unsupported this phase", path, i);
            free_mapped_pages();
            kfree(buf);
            return -15;
        }

        user_prot_t prot = (ph[i].p_flags & PF_X) ? USER_TEXT : USER_DATA;
        uint64_t page_va = vaddr & ~0xFFFULL;
        uint64_t in_page = vaddr & 0xFFFULL;          /* 0 given the alignment check */
        uint64_t pages   = ((in_page + memsz) + 0xFFFULL) >> 12;

        /* Map each page, pre-zeroing it (so the BSS tail is zero without a
         * second pass), then copy the file bytes into the page's PHYSICAL
         * address. The PA from pmm_alloc_page IS identity-mapped (PA == its own
         * low kernel VA under the 0x40000000 RAM block), but the user VA
         * page_va (0x80000000) is NOT identity-mapped to itself - it maps to a
         * fresh PA elsewhere. So segment bytes go to the PA; vmm_map_user_page
         * then exposes that PA at the user VA; eret runs at the user VA. This
         * mirrors usermode_run_payload (usermode.c). Copying to page_va would
         * write into the wrong physical page. */
        uint64_t copied = 0;
        for (uint64_t p = 0; p < pages; p++) {
            /* Fail closed if tracking would overflow (CR-02): the 2MB ceiling
             * caps a valid run at 512 segment pages + 1 stack page, so reaching
             * the array bound means a check above was bypassed. Reject and free
             * rather than allocate a page we could not record and would leak. */
            if (mapped_count >= EXEC_MAX_USER_PAGES) {
                klog_error("exec: %s segment %u exceeds the page-tracking capacity", path, i);
                free_mapped_pages();
                kfree(buf);
                return -16;
            }
            uint64_t pa = pmm_alloc_page();
            if (pa == 0) {
                klog_error("exec: %s out of physical pages mapping segment %u", path, i);
                free_mapped_pages();
                kfree(buf);
                return -6;
            }
            memset((void *)pa, 0, 0x1000);
            vmm_map_user_page(page_va + p * 0x1000, pa, prot);
            mapped_pas[mapped_count++] = pa;

            /* Copy this page's slice of the file bytes (page-aligned in-scope,
             * so in_page == 0; the general in-page offset is folded in for the
             * first page). Bytes past filesz stay zero from the pre-zero. */
            uint64_t page_start_off = (p == 0) ? in_page : 0;
            if (copied < filesz) {
                uint64_t remain = filesz - copied;
                uint64_t space  = 0x1000 - page_start_off;
                uint64_t n = (remain < space) ? remain : space;
                memcpy((void *)(pa + page_start_off), buf + off + copied, n);
                copied += n;
            }
        }

        uint64_t seg_end = round_up_page(vaddr + memsz);
        if (seg_end > top_seg_end) {
            top_seg_end = seg_end;
        }
    }

    /* 6. EL0 stack one page ABOVE the highest segment (computed, never the
     * hardcoded 0x80001000 which would collide with a data segment). The stack
     * page must also fit inside the single 2MB L3 (CR-03): if the top segment
     * reached USER_L3_TOP the stack would land in a different 2MB band and alias
     * a segment's L3 leaf, so SP_EL0's first push would clobber code/data or
     * fault. Reject before mapping it. */
    uint64_t stack_va = top_seg_end;
    if (stack_va + 0x1000 > USER_L3_TOP) {
        klog_error("exec: %s EL0 stack would exceed the mapped 2MB user window", path);
        free_mapped_pages();
        kfree(buf);
        return -17;
    }
    uint64_t stack_pa = pmm_alloc_page();
    if (stack_pa == 0) {
        klog_error("exec: %s out of physical pages for EL0 stack", path);
        free_mapped_pages();
        kfree(buf);
        return -7;
    }
    memset((void *)stack_pa, 0, 0x1000);
    vmm_map_user_page(stack_va, stack_pa, USER_DATA);
    mapped_pas[mapped_count++] = stack_pa;   /* always fits: array is +1 over the cap */
    user_map_end = stack_va + 0x1000;

    /* 7. Enter EL0. The segments are copied into pmm pages, so the heap buffer
     * is no longer needed during the run - free it before the enter.
     *
     * The enter is bracketed by the PCB lifecycle: register a registry-only PCB
     * (visible in ps, reapable by pid) and point current_user_proc at it so the
     * syscall seam can read its kill_requested flag (the LOADED process, NOT
     * idle). user_proc_register never enqueues the PCB, so ready_head stays NULL
     * and the dormant scheduler is untouched. If registration fails, bail with a
     * negative return BEFORE entering EL0 (an EL0 run must always have a PCB so a
     * kill request has somewhere to land), freeing the mapped pages first.
     *
     * usermode_enter returns here after the EL0 program exits via the sys_exit
     * one-shot branch OR after the kill seam reaps it via usermode_return. On
     * return: clear current_user_proc, unregister + free the PCB, and return the
     * mapped pmm pages (segments + stack), closing the leak across exec calls. */
    /* Capture e_entry BEFORE kfree(buf): e aliases buf, so reading e->e_entry
     * after the free is a use-after-free (it read 0 once user_proc_register's
     * kmalloc reused the chunk, and the eret landed on a null entry -> EL0
     * instruction abort). Latent until this loader was actually invoked. */
    uint64_t entry = e->e_entry;
    klog_info("exec: loaded %s entry=%p", path, (void *)entry);
    kfree(buf);

    process_t *proc = user_proc_register(path);
    if (proc == NULL) {
        klog_error("exec: cannot register PCB for %s", path);
        free_mapped_pages();
        return -8;
    }
    current_user_proc = proc;

    usermode_enter(entry, stack_va + 0x1000);

    current_user_proc = NULL;
    process_unregister(proc);
    kfree(proc);
    free_mapped_pages();

    klog_info("exec: %s returned", path);
    return 0;
}

/* ============================================================================
 * End of exec.c
 * ============================================================================ */

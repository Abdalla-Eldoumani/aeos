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

/* The single EL0 user window backed by vmm_map_user_page: TTBR0 L1 index 2
 * (0x80000000) covers one 1GB region; every PT_LOAD must fall inside it. */
#define USER_WINDOW_BASE    0x80000000ULL
#define USER_WINDOW_TOP     0xC0000000ULL

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
 * exec calls. A static AArch64 test binary is a few pages; the cap bounds the
 * tracking array. Pages mapped beyond the cap (a pathologically large segment)
 * are not tracked for free, matching the pre-cap behavior - not a new leak. */
#define EXEC_MAX_TRACKED_PAGES 256
static uint64_t mapped_pas[EXEC_MAX_TRACKED_PAGES];
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

/* Record a PA so it is freed after the EL0 run. Over the cap, the page stays
 * mapped and is simply not tracked (no fault, no new leak vs. the prior code). */
static void track_pa(uint64_t pa)
{
    if (mapped_count < EXEC_MAX_TRACKED_PAGES) {
        mapped_pas[mapped_count++] = pa;
    }
}

/* Round a VA up to the next 4KB boundary. */
static inline uint64_t round_up_page(uint64_t va)
{
    return (va + 0xFFFULL) & ~0xFFFULL;
}

/**
 * Load, validate, map, and run a static ELF64 at EL0. See exec.h. The flow:
 * vfs read into a size-bounded buffer -> elf_validate -> per-PT_LOAD bounds +
 * map (copy segment bytes to the per-page PA, NOT the user VA) -> dynamic EL0
 * stack -> usermode_enter. Every reject path frees what it allocated and
 * returns negative without entering EL0; no path dereferences user memory.
 */
int elf_exec_file(const char *path)
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
        klog_error("exec: %s size %u out of bounds", path, (uint32_t)size);
        vfs_close(fd);
        return -2;
    }

    /* 3. Read the whole file into a heap buffer, then close the fd. */
    unsigned char *buf = (unsigned char *)kmalloc(size);
    if (buf == NULL) {
        klog_error("exec: out of memory loading %s (%u bytes)", path, (uint32_t)size);
        vfs_close(fd);
        return -3;
    }
    ssize_t got = vfs_read(fd, buf, size);
    vfs_close(fd);
    if (got < 0 || (uint64_t)got < size) {
        klog_error("exec: short read on %s (%d of %u)", path, (int)got, (uint32_t)size);
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
            kfree(buf);
            return -11;
        }
        /* memsz must cover filesz (the BSS tail is [filesz, memsz)). */
        if (memsz < filesz) {
            klog_error("exec: %s segment %u memsz < filesz", path, i);
            kfree(buf);
            return -12;
        }
        /* Window bounds (overflow-safe). T-06-05: a crafted segment must not
         * map below the window or past its top. */
        if (vaddr < USER_WINDOW_BASE || vaddr + memsz < vaddr ||
            vaddr + memsz > USER_WINDOW_TOP) {
            klog_error("exec: %s segment %u vaddr out of user window", path, i);
            kfree(buf);
            return -13;
        }
        /* Page-aligned p_vaddr only this phase (the in-scope binary is aligned;
         * the unaligned general case is deferred per RESEARCH). */
        if ((vaddr & 0xFFFULL) != 0) {
            klog_error("exec: %s segment %u unaligned vaddr unsupported this phase", path, i);
            kfree(buf);
            return -13;
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
            uint64_t pa = pmm_alloc_page();
            if (pa == 0) {
                klog_error("exec: %s out of physical pages mapping segment %u", path, i);
                kfree(buf);
                return -6;
            }
            memset((void *)pa, 0, 0x1000);
            vmm_map_user_page(page_va + p * 0x1000, pa, prot);
            track_pa(pa);

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
     * hardcoded 0x80001000 which would collide with a data segment). */
    uint64_t stack_va = top_seg_end;
    uint64_t stack_pa = pmm_alloc_page();
    if (stack_pa == 0) {
        klog_error("exec: %s out of physical pages for EL0 stack", path);
        kfree(buf);
        return -7;
    }
    memset((void *)stack_pa, 0, 0x1000);
    vmm_map_user_page(stack_va, stack_pa, USER_DATA);
    track_pa(stack_pa);
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
        for (unsigned k = 0; k < mapped_count; k++) {
            pmm_free_page(mapped_pas[k]);
        }
        mapped_count = 0;
        return -8;
    }
    current_user_proc = proc;

    usermode_enter(entry, stack_va + 0x1000);

    current_user_proc = NULL;
    process_unregister(proc);
    kfree(proc);
    for (unsigned k = 0; k < mapped_count; k++) {
        pmm_free_page(mapped_pas[k]);
    }
    mapped_count = 0;

    klog_info("exec: %s returned", path);
    return 0;
}

/* ============================================================================
 * End of exec.c
 * ============================================================================ */

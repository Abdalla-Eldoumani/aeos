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

uint64_t usermode_map_base(void)
{
    return user_map_base;
}

uint64_t usermode_map_end(void)
{
    return user_map_end;
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
    user_map_end = stack_va + 0x1000;

    /* 7. Enter EL0. The segments are copied into pmm pages, so the heap buffer
     * is no longer needed during the run - free it before the enter. Keep the
     * stack map -> log -> usermode_enter -> post-enter log as ONE linear block:
     * 06-04 wraps usermode_enter with the PCB-registry lifecycle here, so there
     * must be no helper and no early return between the last map and the enter.
     * usermode_enter returns to here after the EL0 program exits via the
     * sys_exit one-shot branch. */
    klog_info("exec: loaded %s entry=%p", path, (void *)e->e_entry);
    kfree(buf);
    usermode_enter(e->e_entry, stack_va + 0x1000);
    klog_info("exec: %s returned", path);
    return 0;
}

/* ============================================================================
 * End of exec.c
 * ============================================================================ */

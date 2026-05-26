# Section 02: Memory Management

## Overview

This section implements memory management for AEOS. It provides a buddy allocator for page-level physical memory allocation, a first-fit allocator for kernel heap management, and the MMU bringup that puts the kernel behind a page table. The MMU is enabled at boot through a single identity map, so kernel addresses are still physical (VA == PA) while translation, caching, and a per-segment-W^X window for loaded EL0 code are all live.

## Components

### Physical Memory Manager (pmm.c)
- **Location**: `src/mm/pmm.c`
- **Algorithm**: Buddy allocator
- **Purpose**: Manage physical page allocation
- **Features**:
  - O(log n) allocation and deallocation
  - Power-of-two sized blocks (4KB to 4MB)
  - Automatic buddy coalescing on free
  - Allocation tracking and statistics

### Kernel Heap (heap.c)
- **Location**: `src/mm/heap.c`
- **Algorithm**: First-fit with block merging
- **Purpose**: Dynamic memory allocation for kernel data structures
- **Features**:
  - kmalloc/kfree/kcalloc/krealloc
  - Automatic adjacent block merging (O(1) per kfree)
  - Header magic validation; kfree refuses a wrong-magic caller pointer
  - kcalloc rejects an `nmemb * size` multiplication overflow
  - Heap usage statistics

### Virtual Memory (vmm.c)
- **Location**: `src/mm/vmm.c`
- **Purpose**: Build the page tables and enable the MMU and caches
- **Features**:
  - One L1 identity map: RAM as a Normal write-back block, MMIO as a Device block
  - A TTBR1 high-half alias of RAM, verified at boot against the identity read
  - Per-core MMU enable for the SMP secondaries against the same shared tables
  - A single 2MB EL0 user window with per-segment W^X for loaded code

## Memory Layout

```
Physical Memory (256 MB total, QEMU virt -m 256M)
├── 0x40000000 - _kernel_end:       Kernel image (text + rodata + data + BSS, currently ~2.16 MB)
├── __heap_start - __heap_end:      Kernel heap (4 MB, currently 0x40229000 - 0x40629000)
├── __heap_end  - __stack_top:      Kernel stack (128 KB, grows downward from __stack_top)
└── __stack_top - 0x50000000:       PMM-managed pages (the rest of RAM)
```

The linker (`linker.ld`) lays sections in the order kernel image -> heap -> stack, so the heap and stack addresses shift if the kernel grows. `mm_init` reads `__heap_start` / `__heap_end` symbols and hands the post-stack range to the PMM.

`mm_init` starts the PMM at `__stack_top`, not `heap_end`. The region `[heap_end, __stack_top)` holds the live boot stack and the SEC-01 stack-guard sentinel at `__stack_limit` (which equals `heap_end`). The buddy allocator writes a free-list node at the start of its first managed block, so starting at `heap_end` would clobber the sentinel and the in-use stack on the first allocation. Reserving the stack from the PMM is what "PMM-managed pages (the rest of RAM)" above means.

## Virtual Memory (MMU)

`vmm_init` builds one level-1 page table and enables the MMU and caches from C in `kernel_main`, so every address after boot is translated rather than raw physical. The map is intentionally minimal; the honest posture below states exactly what it does and does not give you.

### The identity map

The L1 table has two live block entries, both 1GB:

- **MMIO** at `0x00000000` is mapped Device-nGnRnE and marked never-execute. This one block covers the GIC, the UART, the virtio transports, and the PL031 RTC. The PL031 driver and the other MMIO drivers read their registers through this block; no driver installs its own mapping.
- **RAM** at `0x40000000` is mapped Normal write-back, Inner-Shareable. The kernel executes from here, so the execute-never bits stay clear. All 256MB of RAM sits inside this single 1GB block, so the kernel image, heap, framebuffer, stack, and virtqueues are all covered by one entry.

The map is identity: the virtual address equals the physical address. `vmm_init` runs while the program counter is already inside the identity-mapped RAM block, so execution continues across the enable with no relink and no jump.

### The TTBR1 high-half alias

RAM is also mapped at the high-half base `0xFFFFFF8000000000` through TTBR1. `vmm_report` and the `mmu_ttbr1_alias` test read the first word of the kernel image through the alias and confirm it matches the identity read, which proves the high-half mapping is live. The kernel does not yet execute from the high half; the alias is a demonstrated mapping, not the sole map.

### Per-core enable for the SMP secondaries

The tables are built once, by the primary's `vmm_init`. Each secondary core calls `vmm_enable_secondary`, which programs that core's translation registers against the same shared tables and enables the MMU there; it does not rebuild the tables. The register-programming body is a single shared helper, so the load-bearing `TG1` granule field is written from one place and a per-core copy cannot reintroduce a translation fault. The tables are Inner-Shareable, so the page-table walk is coherent across cores.

### The EL0 user window and its 2MB ceiling

`vmm_map_user_page` carves 4KB pages for the EL0 program into one window at `0x80000000` (L1 index 2). Kernel indexes 0 (MMIO) and 1 (RAM) are never touched, so a fault in the user window cannot break the running kernel. The window is backed by a single static L3 table, which is 512 pages, so the mappable region is exactly the 2MB span `[0x80000000, 0x80200000)`, not the 1GB the L1 entry nominally covers. Two virtual addresses in different 2MB bands would collapse onto the same L3 leaf, so callers must reject any address reaching past `0x80200000` before mapping. The ELF loader does this with the `USER_L3_TOP` ceiling: it validates every segment and the EL0 stack against `0x80200000`, the region the mapper can actually honor, before mapping a single page. Lifting the ceiling would mean allocating an additional L3 table per 2MB band, which is deferred.

### MMU posture (do not overstate)

- **One RWX block for the kernel; no kernel-wide W^X.** The kernel's code, rodata, data, heap, and stack share one Normal read-write-execute 1GB block. There is no per-section W^X for the kernel. Fine-grained kernel permissions would need 2MB or 4KB tables and stay out of scope.
- **Per-segment W^X for loaded EL0 code.** `vmm_map_user_page` supports a `USER_TEXT` class that maps a page EL0 read-and-execute but not EL0-writable. The ELF loader maps executable segments `USER_TEXT`, so a loaded program cannot rewrite its own instructions. This narrows the user window only; the kernel block above is still RWX.
- **TTBR0 is reserved, not empty.** The per-process user mapping installed for an EL0 program lives in TTBR0, and the running kernel still needs TTBR0 for the identity map, so it is not empty.

## Buddy Allocator

### Concept
The buddy allocator manages memory in power-of-two sized blocks. Each block has a "buddy" at a specific address calculated by XORing with the block size. When both a block and its buddy are free, they can be merged into a larger block.

### Order System
- Order 0: 4KB (1 page)
- Order 1: 8KB (2 pages)
- Order 2: 16KB (4 pages)
- ...
- Order 10: 4MB (1024 pages)

### Free Lists
The allocator maintains an array of 11 free lists (one per order). Each list contains blocks of that size available for allocation.

## Heap Allocator

### Block Structure
Each heap block has a header containing:
- A magic field (`0xAEDA110C`) used to validate the header
- Size (including header)
- Free flag
- Next/previous pointers for linked list

### Block Magic
Every header carries the magic value `0xAEDA110C`. It is stamped at `heap_init` and re-stamped on every operation that touches a header: `split_block` stamps the new tail, `merge_free_blocks` re-stamps the surviving block after both the absorb-next and absorb-prev folds, and the `krealloc` in-place shrink re-stamps the shrunk head. An absorbed sub-header no longer reads a valid magic, so a pointer that lands mid-block of a merged allocation is detectable.

`kfree` uses the magic to tell a bad caller pointer from internal corruption. If the caller's header magic is wrong, `kfree` logs a `klog_error` and returns rather than halting, so a stale or mid-block free is refused without taking the kernel down. Internal traversals (`find_free_block`, `merge_free_blocks`) use the halting `heap_check_magic` validator instead, because a bad magic encountered while walking the free list means the list itself is corrupt and a `klog_fatal` at the fault site is the right answer.

### Allocation Strategy
**First-fit**: Scans the free list from the beginning and uses the first block large enough to satisfy the request.

### Block Splitting
When a large free block is allocated, if there's enough remaining space (at least 16 bytes + header), it's split into two blocks:
- One allocated block (requested size)
- One free block (remainder)

### Block Merging
When a block is freed, the allocator checks adjacent blocks. If neighbors are also free, they're merged into a single larger block to reduce fragmentation.

## API Reference

### Physical Memory Manager

```c
/* Initialize PMM with memory range */
void pmm_init(uint64_t mem_start, uint64_t mem_end, uint64_t kernel_end);

/* Allocate 2^order contiguous pages */
uint64_t pmm_alloc_pages(uint32_t order);

/* Free 2^order contiguous pages */
void pmm_free_pages(uint64_t addr, uint32_t order);

/* Reserve a memory region (exclude from allocation) */
void pmm_reserve_region(uint64_t start, uint64_t end);

/* Get memory statistics */
void pmm_get_stats(pmm_stats_t *stats);

/* Debug output */
void pmm_dump_state(void);
```

### Kernel Heap

```c
/* Allocate size bytes */
void *kmalloc(size_t size);

/* Allocate and zero-initialize */
void *kcalloc(size_t nmemb, size_t size);

/* Free allocated memory */
void kfree(void *ptr);

/* Resize allocation */
void *krealloc(void *ptr, size_t new_size);

/* Get heap statistics */
void heap_get_stats(heap_stats_t *stats);

/* Debug output */
void heap_dump_state(void);
```

## Memory Statistics

### PMM Stats (pmm_stats_t)
- `total_pages`: Total pages in system
- `free_pages`: Currently free pages
- `used_pages`: Currently allocated pages
- `reserved_pages`: Pages excluded from allocation

### Heap Stats (heap_stats_t)
- `total_size`: Total heap size in bytes
- `used_size`: Bytes allocated (including headers)
- `free_size`: Bytes available for allocation
- `num_blocks`: Number of blocks in heap
- `num_allocs`: Total allocation count
- `num_frees`: Total free count

## Usage Examples

### Allocating Physical Pages

```c
/* Allocate one page (4KB) */
uint64_t page = pmm_alloc_pages(0);

/* Allocate 16KB (order 2 = 4 pages) */
uint64_t large = pmm_alloc_pages(2);

/* Free the pages */
pmm_free_pages(page, 0);
pmm_free_pages(large, 2);
```

### Kernel Heap Allocation

```c
/* Allocate structure */
my_struct_t *s = (my_struct_t *)kmalloc(sizeof(my_struct_t));

/* Allocate array with zero-init */
int *array = (int *)kcalloc(100, sizeof(int));

/* Resize array */
array = (int *)krealloc(array, 200 * sizeof(int));

/* Free memory */
kfree(s);
kfree(array);
```

## Important Notes

### Page Size
- AEOS uses 4KB pages (PAGE_SIZE = 4096)
- All physical allocations are page-aligned

### Heap Size
- Fixed at 4MB (configurable in main.c)
- Cannot grow beyond initial size
- Monitor usage with heap_get_stats()

### Alignment
- PMM allocations: Page-aligned (4KB)
- Heap allocations: 8-byte aligned
- Stack: 16-byte aligned (ARM64 requirement)

### Memory Overhead
- PMM: Free block headers stored in-place (8 bytes per free block)
- Heap: Block headers (32 bytes each) reduce usable space

## Known Issues

### Buddy Allocator Fragmentation
When allocations of mixed sizes occur, free blocks may be unavailable at certain orders even though total free memory is sufficient.

**Example**:
- Allocate order 5 (128KB)
- Allocate order 3 (32KB)
- Free order 5
- Now we have 128KB free but it's not buddies with anything

**Mitigation**: Allocate similar-sized objects together when possible.

### Heap Fragmentation
First-fit can lead to small free blocks scattered throughout the heap.

**Mitigation**: Block merging helps, but allocation patterns matter. Consider using slab allocator for frequently allocated fixed-size objects (not implemented).

### No Memory Reclamation
Once memory is allocated from the PMM for the heap, it cannot be returned. The heap cannot shrink.

## Testing

### PMM Tests

```c
/* Test basic allocation */
uint64_t p1 = pmm_alloc_pages(0);
uint64_t p2 = pmm_alloc_pages(0);
assert(p1 != p2);  /* Different pages */
assert((p1 & 0xFFF) == 0);  /* Page-aligned */

/* Test buddy coalescing */
pmm_free_pages(p1, 0);
pmm_free_pages(p2, 0);
pmm_dump_state();  /* Check if buddies merged */
```

### Heap Tests

```c
/* Test allocation */
void *p = kmalloc(100);
assert(p != NULL);

/* Test double-free detection */
kfree(p);
kfree(p);  /* Should log error and not crash */

/* Test realloc */
void *p2 = krealloc(NULL, 50);  /* Should work like malloc */
p2 = krealloc(p2, 0);  /* Should work like free */
assert(p2 == NULL);
```

### Memory Leak Detection

```c
heap_stats_t stats_before, stats_after;

heap_get_stats(&stats_before);

/* Do some allocations and frees */
void *p = kmalloc(1000);
kfree(p);

heap_get_stats(&stats_after);

/* Check for leaks */
assert(stats_before.used_size == stats_after.used_size);
```
# Section 02: Memory Management

## Overview

This section implements physical and virtual memory management for AEOS. It provides a buddy allocator for page-level physical memory allocation and a first-fit allocator for kernel heap management.

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
  - Automatic adjacent block merging
  - Double-free detection
  - Heap usage statistics

## Memory Layout

```
Physical Memory (256 MB total)
├── 0x40000000 - 0x40010000: Kernel code/data (~64KB)
├── 0x40010000 - 0x4001a000: Kernel stack (128KB, grows down)
├── 0x4001a000 - 0x4041a000: Kernel heap (4MB)
└── 0x4041a000 - 0x50000000: Free physical pages (~251MB)
```

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
- Size (including header)
- Free flag
- Next/previous pointers for linked list

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
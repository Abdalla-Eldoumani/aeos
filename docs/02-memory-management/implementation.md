# Memory Management - Implementation Details

## pmm.c - Physical Memory Manager (Buddy Allocator)

### Data Structures

```c
typedef struct free_block {
    struct free_block *next;
} free_block_t;

static struct {
    free_block_t *free_lists[PMM_MAX_ORDER + 1];  /* Free lists for each order */
    uint64_t mem_start;
    uint64_t mem_end;
    size_t total_pages;
    size_t free_pages;
    bool initialized;
} pmm;
```

**Key Points**:
- Free blocks store metadata in-place (8-byte next pointer at start of block)
- 11 free lists (orders 0-10) for different block sizes
- No separate metadata structure - free blocks are self-describing

### Initialization

```c
void pmm_init(uint64_t mem_start, uint64_t mem_end, uint64_t kernel_end)
{
    /* Align boundaries to pages */
    mem_start = PAGE_ALIGN_UP(mem_start);
    mem_end = PAGE_ALIGN_DOWN(mem_end);
    kernel_end = PAGE_ALIGN_UP(kernel_end);

    /* Add all available memory to free lists */
    current = available_start;
    while (current < available_end) {
        /* Find largest block that fits */
        for (order = PMM_MAX_ORDER; order >= 0; order--) {
            block_size = PAGE_SIZE << order;

            if (remaining >= block_size &&
                (current & (block_size - 1)) == 0) {
                add_to_free_list(current, order);
                current += block_size;
                pmm.free_pages += (1 << order);
                break;
            }
        }
    }
}
```

**Algorithm**:
1. Align memory boundaries to 4KB pages
2. Iterate through available memory
3. For each position, find the largest properly-aligned block that fits
4. Add block to appropriate free list
5. Repeat until all memory is added

**Alignment Check**: `(current & (block_size - 1)) == 0` verifies address is aligned to block size.

### Allocation

```c
uint64_t pmm_alloc_pages(uint32_t order)
{
    /* Find smallest available block that fits */
    for (current_order = order; current_order <= PMM_MAX_ORDER; current_order++) {
        if (pmm.free_lists[current_order] != NULL) {
            /* Found a block, remove it from free list */
            addr = remove_from_free_list(current_order);

            /* Split block if it's larger than needed */
            while (current_order > order) {
                current_order--;
                /* Add buddy (second half) to free list */
                add_to_free_list(addr + (PAGE_SIZE << current_order), current_order);
            }

            pmm.free_pages -= (1 << order);
            return addr;
        }
    }

    /* No memory available */
    return 0;
}
```

**Splitting Algorithm**:
- If requesting order 2 but only order 4 is available:
  1. Remove order 4 block (64KB at address A)
  2. Split into two order 3 blocks (32KB each at A and A+32KB)
  3. Add second half (A+32KB) to order 3 list
  4. Split first half into two order 2 blocks (16KB each at A and A+16KB)
  5. Add second half (A+16KB) to order 2 list
  6. Return first order 2 block (A)

### Deallocation with Buddy Coalescing

```c
void pmm_free_pages(uint64_t addr, uint32_t order)
{
    /* Try to merge with buddy */
    while (order < PMM_MAX_ORDER) {
        buddy_addr = get_buddy_addr(addr, order);

        /* Check if buddy is free */
        if (!is_in_free_list(buddy_addr, order)) {
            break;  /* Buddy not free, stop merging */
        }

        /* Remove buddy from free list */
        remove_from_free_list(order);

        /* Merge with buddy (use lower address) */
        if (buddy_addr < addr) {
            addr = buddy_addr;
        }

        /* Move up to next order */
        order++;
    }

    /* Add merged block to free list */
    add_to_free_list(addr, order);
    pmm.free_pages += (1 << order);
}
```

**Buddy Address Calculation**:
```c
static uint64_t get_buddy_addr(uint64_t addr, uint32_t order)
{
    uint64_t block_size = PAGE_SIZE << order;
    return addr ^ block_size;
}
```

**Why XOR?**
- For order N block at address A, buddy is at A XOR (block_size)
- Example: Order 2 (16KB = 0x4000)
  - Block at 0x40000, buddy at 0x40000 ^ 0x4000 = 0x44000
  - Block at 0x44000, buddy at 0x44000 ^ 0x4000 = 0x40000

This works because blocks are always aligned to their size.

### Free List Management

```c
static void add_to_free_list(uint64_t addr, uint32_t order)
{
    free_block_t *block = (free_block_t *)addr;
    block->next = pmm.free_lists[order];
    pmm.free_lists[order] = block;
}

static uint64_t remove_from_free_list(uint32_t order)
{
    free_block_t *block = pmm.free_lists[order];
    if (block == NULL) {
        return 0;
    }

    pmm.free_lists[order] = block->next;
    return (uint64_t)block;
}
```

**In-Place Metadata**: The first 8 bytes of each free block store the `next` pointer. This means free blocks must be at least 8 bytes (smallest block is 4KB, so no problem).

## heap.c - Kernel Heap Allocator

### Data Structures

```c
typedef struct heap_block {
    size_t size;                /* Size of block (including header) */
    bool is_free;               /* True if block is free */
    struct heap_block *next;    /* Next block in list */
    struct heap_block *prev;    /* Previous block in list */
} heap_block_t;

#define BLOCK_HEADER_SIZE   sizeof(heap_block_t)  /* 32 bytes */
#define MIN_BLOCK_SIZE      (BLOCK_HEADER_SIZE + 16)
```

**Block Layout**:
```
+------------------+
| heap_block_t     | 32 bytes
| - size           |
| - is_free        |
| - next           |
| - prev           |
+------------------+
| User data        | (size - 32) bytes
+------------------+
```

### Initialization

```c
void heap_init(void *heap_start, size_t heap_size)
{
    /* Create initial free block spanning entire heap */
    initial_block = (heap_block_t *)heap_start;
    initial_block->size = heap_size;
    initial_block->is_free = true;
    initial_block->next = NULL;
    initial_block->prev = NULL;

    heap.first_block = initial_block;
}
```

Initially, the entire heap is one large free block.

### Allocation (kmalloc)

```c
void *kmalloc(size_t size)
{
    /* Add header size and align to 8 bytes */
    size = (size + BLOCK_HEADER_SIZE + 7) & ~7;

    /* Ensure minimum block size */
    if (size < MIN_BLOCK_SIZE) {
        size = MIN_BLOCK_SIZE;
    }

    /* Find free block using first-fit */
    block = find_free_block(size);
    if (block == NULL) {
        return NULL;
    }

    /* Split block if it's much larger than needed */
    if (block->size >= size + MIN_BLOCK_SIZE) {
        split_block(block, size);
    }

    /* Mark block as used */
    block->is_free = false;

    /* Return pointer after header */
    return (void *)((uint64_t)block + BLOCK_HEADER_SIZE);
}
```

**Size Calculation**:
- User requests 100 bytes
- Add header: 100 + 32 = 132
- Align to 8: (132 + 7) & ~7 = 136

**Why Alignment?** ARM64 benefits from aligned accesses. 8-byte alignment ensures efficient memory operations.

### First-Fit Search

```c
static heap_block_t *find_free_block(size_t size)
{
    heap_block_t *block = heap.first_block;

    while (block != NULL) {
        if (block->is_free && block->size >= size) {
            return block;
        }
        block = block->next;
    }

    return NULL;
}
```

Scans from start of heap until finding first suitable free block.

**Performance**: O(n) where n = number of blocks. Could be improved with segregated free lists.

### Block Splitting

```c
static void split_block(heap_block_t *block, size_t size)
{
    heap_block_t *new_block;
    size_t remaining_size = block->size - size;

    /* Create new block in remaining space */
    new_block = (heap_block_t *)((uint64_t)block + size);
    new_block->size = remaining_size;
    new_block->is_free = true;
    new_block->next = block->next;
    new_block->prev = block;

    /* Update current block */
    block->size = size;
    block->next = new_block;

    /* Update next block's prev pointer */
    if (new_block->next != NULL) {
        new_block->next->prev = new_block;
    }
}
```

**Example**:
```
Before: [100 bytes free]
After split(50):
        [50 bytes used] -> [50 bytes free]
```

### Deallocation (kfree)

```c
void kfree(void *ptr)
{
    if (ptr == NULL) {
        return;
    }

    /* Get block header */
    block = (heap_block_t *)((uint64_t)ptr - BLOCK_HEADER_SIZE);

    /* Check if already free (double-free detection) */
    if (block->is_free) {
        klog_error("kfree: Double free detected at %p", ptr);
        return;
    }

    /* Mark as free */
    block->is_free = true;

    /* Merge adjacent free blocks */
    merge_free_blocks();
}
```

**Double-Free Detection**: Checks `is_free` flag before freeing. This catches many use-after-free bugs.

### Block Merging

```c
static void merge_free_blocks(void)
{
    heap_block_t *block = heap.first_block;

    while (block != NULL && block->next != NULL) {
        if (block->is_free && block->next->is_free) {
            /* Merge with next block */
            block->size += block->next->size;
            block->next = block->next->next;

            if (block->next != NULL) {
                block->next->prev = block;
            }
        } else {
            block = block->next;
        }
    }
}
```

**Coalescing Strategy**: Single pass from start to end, merging each free block with its free successor.

**Example**:
```
Before: [F:32] -> [U:64] -> [F:32] -> [F:16]
After:  [F:32] -> [U:64] -> [F:48]
```

### Realloc Implementation

```c
void *krealloc(void *ptr, size_t new_size)
{
    /* Special cases */
    if (ptr == NULL) {
        return kmalloc(new_size);
    }
    if (new_size == 0) {
        kfree(ptr);
        return NULL;
    }

    /* Get current block */
    block = (heap_block_t *)((uint64_t)ptr - BLOCK_HEADER_SIZE);

    /* If block is large enough, just return it */
    if (block->size >= new_size + BLOCK_HEADER_SIZE) {
        return ptr;
    }

    /* Allocate new block */
    new_ptr = kmalloc(new_size);
    if (new_ptr == NULL) {
        return NULL;
    }

    /* Copy data from old to new */
    copy_size = block->size - BLOCK_HEADER_SIZE;
    if (new_size < copy_size) {
        copy_size = new_size;
    }
    memcpy(new_ptr, ptr, copy_size);

    /* Free old block */
    kfree(ptr);

    return new_ptr;
}
```

**Optimization**: If existing block is large enough, no reallocation needed.

## Memory Alignment

### Page Alignment Macros

```c
#define PAGE_SIZE           4096
#define PAGE_SHIFT          12
#define PAGE_ALIGN_DOWN(x)  ((x) & ~(PAGE_SIZE - 1))
#define PAGE_ALIGN_UP(x)    (((x) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1))
```

**Examples**:
- PAGE_ALIGN_DOWN(0x1234) = 0x1000
- PAGE_ALIGN_UP(0x1234) = 0x2000

### Heap Allocation Alignment

```c
size = (size + BLOCK_HEADER_SIZE + 7) & ~7;
```

**How It Works**:
- Add 7: Rounds up to next multiple of 8
- `& ~7`: Clears bottom 3 bits (same as `& 0xFFFFFFF8`)

**Examples**:
- (100 + 7) & ~7 = 107 & ~7 = 104
- (104 + 7) & ~7 = 111 & ~7 = 104

## Statistics Tracking

### PMM Statistics

```c
void pmm_get_stats(pmm_stats_t *stats)
{
    stats->total_pages = pmm.total_pages;
    stats->free_pages = pmm.free_pages;
    stats->used_pages = pmm.total_pages - pmm.free_pages;
}
```

Simple counters updated on each allocation/free.

### Heap Statistics

```c
void heap_get_stats(heap_stats_t *stats)
{
    /* Walk block list */
    block = heap.first_block;
    while (block != NULL) {
        num_blocks++;
        if (block->is_free) {
            free_size += block->size;
        } else {
            used_size += block->size;
        }
        block = block->next;
    }

    stats->total_size = heap.heap_size;
    stats->used_size = used_size;
    stats->free_size = free_size;
    stats->num_blocks = num_blocks;
}
```

**Performance**: O(n) - must walk entire heap to collect stats.

## Common Pitfalls

### Pointer Arithmetic with Headers
```c
/* Getting block from user pointer */
block = (heap_block_t *)(ptr - BLOCK_HEADER_SIZE);  /* Wrong - byte math */
block = (heap_block_t *)((uint64_t)ptr - BLOCK_HEADER_SIZE);  /* Correct */
```

Cast to uint64_t first to ensure byte-level arithmetic.

### Forgetting Alignment
```c
/* May cause unaligned access */
void *p = kmalloc(33);

/* Size becomes 48 (33 + 32 header + padding) */
/* Aligned to 8 bytes */
```

### Double Free Not Detected in PMM
The physical memory manager does NOT detect double-frees. Be careful:
```c
uint64_t p = pmm_alloc_pages(0);
pmm_free_pages(p, 0);
pmm_free_pages(p, 0);  /* CORRUPTS FREE LISTS */
```

The heap allocator does detect this, but PMM doesn't.
/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: src/drivers/virtio_gpu.c
 * Description: VirtIO GPU driver implementation
 * ============================================================================ */

#include <aeos/virtio_gpu.h>
#include <aeos/framebuffer.h>
#include <aeos/kprintf.h>
#include <aeos/string.h>
#include <aeos/heap.h>

/* Virtqueue configuration */
#define VIRTQ_SIZE 64  /* Queue size (must be power of 2) */

/* Virtqueue structure */
typedef struct {
    virtq_desc_t *desc;      /* Descriptor table */
    virtq_avail_t *avail;    /* Available ring */
    virtq_used_t *used;      /* Used ring */
    uint16_t last_used_idx;  /* Last processed used index */
    uint16_t num_free;       /* Number of free descriptors */
    uint16_t free_head;      /* First free descriptor */
} virtqueue_t;

/* Global virtio-gpu device */
static virtio_gpu_t gpu_dev;

/* Control virtqueue (queue 0) */
static virtqueue_t ctrl_vq;

/**
 * Initialize a virtqueue
 */
static int virtqueue_init(virtqueue_t *vq, volatile uint32_t *mmio, uint32_t queue_idx)
{
    uint32_t queue_size;
    size_t desc_size, avail_size, used_size;
    uint8_t *queue_mem;
    uint64_t desc_addr, avail_addr, used_addr;
    uint32_t i;

    /* Select queue */
    virtio_mmio_write32(mmio, VIRTIO_MMIO_QUEUE_SEL, queue_idx);

    /* Get maximum queue size */
    queue_size = virtio_mmio_read32(mmio, VIRTIO_MMIO_QUEUE_NUM_MAX);
    if (queue_size == 0) {
        klog_error("Queue %u not available", queue_idx);
        return -1;
    }

    if (queue_size > VIRTQ_SIZE) {
        queue_size = VIRTQ_SIZE;
    }

    /* Calculate sizes */
    desc_size = sizeof(virtq_desc_t) * queue_size;
    avail_size = sizeof(uint16_t) * (3 + queue_size);
    used_size = sizeof(uint16_t) * 3 + sizeof(virtq_used_elem_t) * queue_size;

    /* For legacy VirtIO, used ring must be page-aligned after avail ring */
    /* Calculate offset for used ring (page-aligned) */
    size_t avail_offset = desc_size;
    size_t used_offset = (desc_size + avail_size + 4095) & ~4095ULL;  /* Page-align used ring */

    /* Allocate memory (must be page-aligned for legacy v1) */
    size_t total_size = used_offset + used_size + 8192;  /* Extra for alignment */
    uint8_t *raw_mem = (uint8_t *)kmalloc(total_size);
    if (!raw_mem) {
        klog_error("Failed to allocate queue memory");
        return -1;
    }

    /* Align to 4KB page boundary (required for legacy VirtIO v1) */
    uint64_t addr = (uint64_t)raw_mem;
    uint64_t aligned_addr = (addr + 4095) & ~4095ULL;  /* Round up to next 4KB */
    queue_mem = (uint8_t *)aligned_addr;

    /* Clear queue memory */
    memset(queue_mem, 0, used_offset + used_size);

    /* Set up descriptor table */
    vq->desc = (virtq_desc_t *)queue_mem;
    vq->avail = (virtq_avail_t *)(queue_mem + avail_offset);
    vq->used = (virtq_used_t *)(queue_mem + used_offset);  /* Page-aligned for legacy */

    klog_debug("  Queue layout: desc=%p avail=%p used=%p",
              vq->desc, vq->avail, vq->used);

    /* Initialize free list */
    vq->num_free = queue_size;
    vq->free_head = 0;
    vq->last_used_idx = 0;

    for (i = 0; i < queue_size - 1; i++) {
        vq->desc[i].next = i + 1;
    }
    vq->desc[queue_size - 1].next = 0;

    /* Get physical addresses (we're using identity mapping) */
    desc_addr = (uint64_t)vq->desc;
    avail_addr = (uint64_t)vq->avail;
    used_addr = (uint64_t)vq->used;

    /* Check device version for correct initialization */
    uint32_t version = virtio_mmio_read32(mmio, VIRTIO_MMIO_VERSION);

    /* Configure queue in device */
    virtio_mmio_write32(mmio, VIRTIO_MMIO_QUEUE_NUM, queue_size);
    klog_debug("  Queue %u: size=%u, version=%u", queue_idx, queue_size, version);

    if (version == 1) {
        /* Legacy VirtIO v1: Use QUEUE_ALIGN and QUEUE_PFN */
        klog_debug("  Using legacy queue initialization (v1)");
        virtio_mmio_write32(mmio, VIRTIO_MMIO_QUEUE_ALIGN, 4096);  /* Page size */

        /* Calculate page frame number (descriptor table address / page size) */
        uint32_t pfn = (uint32_t)(desc_addr >> 12);  /* Divide by 4096 */
        virtio_mmio_write32(mmio, VIRTIO_MMIO_QUEUE_PFN, pfn);

        klog_debug("  Queue PFN=0x%x (desc_addr=0x%p)", pfn, (void*)desc_addr);
    } else {
        /* Modern VirtIO v2: Use separate descriptor/avail/used addresses */
        klog_debug("  Using modern queue initialization (v2)");
        klog_debug("  desc=0x%x avail=0x%x used=0x%x",
                  (uint32_t)desc_addr, (uint32_t)avail_addr, (uint32_t)used_addr);
        virtio_mmio_write32(mmio, VIRTIO_MMIO_QUEUE_DESC_LOW, (uint32_t)(desc_addr & 0xFFFFFFFF));
        virtio_mmio_write32(mmio, VIRTIO_MMIO_QUEUE_DESC_HIGH, (uint32_t)(desc_addr >> 32));
        virtio_mmio_write32(mmio, VIRTIO_MMIO_QUEUE_AVAIL_LOW, (uint32_t)(avail_addr & 0xFFFFFFFF));
        virtio_mmio_write32(mmio, VIRTIO_MMIO_QUEUE_AVAIL_HIGH, (uint32_t)(avail_addr >> 32));
        virtio_mmio_write32(mmio, VIRTIO_MMIO_QUEUE_USED_LOW, (uint32_t)(used_addr & 0xFFFFFFFF));
        virtio_mmio_write32(mmio, VIRTIO_MMIO_QUEUE_USED_HIGH, (uint32_t)(used_addr >> 32));

        /* Memory barrier before marking queue ready */
        __asm__ volatile("dmb sy" ::: "memory");

        /* Mark queue as ready */
        virtio_mmio_write32(mmio, VIRTIO_MMIO_QUEUE_READY, 1);

        /* Verify queue is ready (use (void) to suppress unused warning in release builds) */
        uint32_t ready = virtio_mmio_read32(mmio, VIRTIO_MMIO_QUEUE_READY);
        (void)ready;
        klog_debug("  Queue ready: %u", ready);
    }

    /* Verify queue is ready */
    uint32_t queue_ready = (version == 1) ? 1 : virtio_mmio_read32(mmio, VIRTIO_MMIO_QUEUE_READY);
    (void)queue_ready;  /* Used only in debug output */
    klog_debug("Virtqueue %u initialized (size=%u, ready=%u)", queue_idx, queue_size, queue_ready);

    /* Debug: Show queue configuration */
    klog_debug("  desc=0x%p avail=0x%p used=0x%p", vq->desc, vq->avail, vq->used);

    return 0;
}

/**
 * Submit a command to the control virtqueue
 * @param cmd Command buffer (read-only)
 * @param cmd_len Command length
 * @param resp Response buffer (write-only)
 * @param resp_len Response length
 * @return 0 on success, -1 on error
 */
static int virtio_gpu_submit_cmd(void *cmd, size_t cmd_len, void *resp, size_t resp_len)
{
    volatile uint32_t *mmio = gpu_dev.vdev.mmio_base;
    uint16_t cmd_desc, resp_desc;
    uint16_t old_avail_idx;

    if (!gpu_dev.initialized || ctrl_vq.num_free < 2) {
        return -1;
    }

    /* Allocate descriptor for command */
    cmd_desc = ctrl_vq.free_head;
    ctrl_vq.free_head = ctrl_vq.desc[cmd_desc].next;
    ctrl_vq.num_free--;

    /* Allocate descriptor for response */
    resp_desc = ctrl_vq.free_head;
    ctrl_vq.free_head = ctrl_vq.desc[resp_desc].next;
    ctrl_vq.num_free--;

    /* Set up command descriptor (device reads from this) */
    ctrl_vq.desc[cmd_desc].addr = (uint64_t)cmd;
    ctrl_vq.desc[cmd_desc].len = cmd_len;
    ctrl_vq.desc[cmd_desc].flags = VIRTQ_DESC_F_NEXT;
    ctrl_vq.desc[cmd_desc].next = resp_desc;

    /* Set up response descriptor (device writes to this) */
    ctrl_vq.desc[resp_desc].addr = (uint64_t)resp;
    ctrl_vq.desc[resp_desc].len = resp_len;
    ctrl_vq.desc[resp_desc].flags = VIRTQ_DESC_F_WRITE;
    ctrl_vq.desc[resp_desc].next = 0;

    /* Add to available ring */
    old_avail_idx = ctrl_vq.avail->idx;
    ctrl_vq.avail->ring[old_avail_idx % VIRTQ_SIZE] = cmd_desc;

    /* Memory barrier - ensure all memory writes are visible */
    __asm__ volatile("dmb ish" ::: "memory");
    __asm__ volatile("dsb ish" ::: "memory");

    ctrl_vq.avail->idx = old_avail_idx + 1;

    /* Another barrier after updating avail idx */
    __asm__ volatile("dmb ish" ::: "memory");
    __asm__ volatile("dsb ish" ::: "memory");

    /* Notify device (write to queue notify register) */
    virtio_mmio_write32(mmio, VIRTIO_MMIO_QUEUE_NOTIFY, 0);

    /* Wait for response (with timeout to prevent infinite hang) */
    uint32_t timeout = 1000000;  /* ~1 second timeout */
    while (ctrl_vq.used->idx == ctrl_vq.last_used_idx) {
        if (--timeout == 0) {
            /* Check interrupt status to see if device tried to signal */
            uint32_t isr = virtio_mmio_read32(mmio, VIRTIO_MMIO_INTERRUPT_STATUS);
            klog_error("GPU command timeout! used->idx=%u last_used=%u isr=0x%x",
                       ctrl_vq.used->idx, ctrl_vq.last_used_idx, isr);
            /* Return descriptors to free list */
            ctrl_vq.desc[resp_desc].next = ctrl_vq.free_head;
            ctrl_vq.free_head = cmd_desc;
            ctrl_vq.num_free += 2;
            return -1;
        }
        /* Memory barrier before reading used ring */
        __asm__ volatile("dmb ish" ::: "memory");
    }

    /* Process completion and free descriptors */
    ctrl_vq.last_used_idx++;

    /* Return descriptors to free list */
    ctrl_vq.desc[resp_desc].next = ctrl_vq.free_head;
    ctrl_vq.free_head = cmd_desc;
    ctrl_vq.num_free += 2;

    return 0;
}

/**
 * Initialize VirtIO device (generic)
 */
int virtio_init_device(volatile void *base, virtio_device_t *dev)
{
    volatile uint32_t *mmio = (volatile uint32_t *)base;

    /* Check magic number */
    uint32_t magic = virtio_mmio_read32(mmio, VIRTIO_MMIO_MAGIC);
    if (magic != 0x74726976) {  /* 'virt' in little-endian */
        klog_error("Invalid VirtIO magic: 0x%x", magic);
        return -1;
    }

    /* Check version (accept both legacy v1 and modern v2) */
    uint32_t version = virtio_mmio_read32(mmio, VIRTIO_MMIO_VERSION);
    if (version != 1 && version != 2) {
        klog_error("Unsupported VirtIO version: %u (expected 1 or 2)", version);
        return -1;
    }

    /* Read device ID */
    dev->device_id = virtio_mmio_read32(mmio, VIRTIO_MMIO_DEVICE_ID);
    dev->vendor_id = virtio_mmio_read32(mmio, VIRTIO_MMIO_VENDOR_ID);
    dev->mmio_base = mmio;
    dev->initialized = false;

    klog_debug("VirtIO device found:");
    klog_debug("  Type: %u", dev->device_id);
    klog_debug("  Vendor: 0x%x", dev->vendor_id);
    klog_debug("  Version: %u", version);

    return 0;
}

/**
 * Initialize VirtIO GPU driver
 */
int virtio_gpu_init(void)
{
    uint32_t i;
    uint64_t addr;

    klog_debug("Scanning for VirtIO GPU device...");

    /* Scan all VirtIO MMIO device slots */
    for (i = 0; i < VIRTIO_MMIO_COUNT; i++) {
        addr = VIRTIO_MMIO_BASE + (i * VIRTIO_MMIO_SIZE);

        /* Try to initialize device at this slot */
        if (virtio_init_device((void *)addr, &gpu_dev.vdev) != 0) {
            continue;  /* No device at this slot */
        }

        /* Check if this is a GPU device */
        if (gpu_dev.vdev.device_id == VIRTIO_ID_GPU) {
            klog_info("VirtIO GPU device found at slot %u (0x%x)", i, (uint32_t)addr);
            goto found_gpu;
        }

        klog_debug("VirtIO device at slot %u: type=%u (not GPU)", i, gpu_dev.vdev.device_id);
    }

    /* No GPU device found */
    klog_debug("VirtIO GPU not found in any device slot");
    return -1;

found_gpu:

    volatile uint32_t *mmio = gpu_dev.vdev.mmio_base;

    /* Reset device */
    virtio_mmio_write32(mmio, VIRTIO_MMIO_STATUS, 0);

    /* Set ACKNOWLEDGE status */
    virtio_mmio_write32(mmio, VIRTIO_MMIO_STATUS, VIRTIO_STATUS_ACKNOWLEDGE);

    /* Set DRIVER status */
    uint32_t status = virtio_mmio_read32(mmio, VIRTIO_MMIO_STATUS);
    virtio_mmio_write32(mmio, VIRTIO_MMIO_STATUS, status | VIRTIO_STATUS_DRIVER);

    /* Read device features using selector register */
    virtio_mmio_write32(mmio, VIRTIO_MMIO_DEVICE_FEATURES_SEL, 0);  /* Select low 32 bits */
    uint32_t features_lo = virtio_mmio_read32(mmio, VIRTIO_MMIO_DEVICE_FEATURES);
    virtio_mmio_write32(mmio, VIRTIO_MMIO_DEVICE_FEATURES_SEL, 1);  /* Select high 32 bits */
    uint32_t features_hi = virtio_mmio_read32(mmio, VIRTIO_MMIO_DEVICE_FEATURES);

    gpu_dev.vdev.features = ((uint64_t)features_hi << 32) | features_lo;
    klog_debug("Device features: 0x%x%x", features_hi, features_lo);

    /* Check device version */
    uint32_t dev_version = virtio_mmio_read32(mmio, VIRTIO_MMIO_VERSION);

    /* For version 1 (legacy), feature negotiation is optional */
    /* For version 2 (modern), we MUST negotiate VIRTIO_F_VERSION_1 */
    if (dev_version == 1) {
        /* Legacy device - must set guest page size first */
        klog_debug("Using legacy VirtIO (version 1)");
        virtio_mmio_write32(mmio, VIRTIO_MMIO_GUEST_PAGE_SIZE, 4096);
    } else {
        /* Modern device - write driver features */
        /* We must accept VIRTIO_F_VERSION_1 (bit 32) */
        klog_debug("Using modern VirtIO (version 2)");

        /* Write low 32 bits of features (accept all device features) */
        virtio_mmio_write32(mmio, VIRTIO_MMIO_DRIVER_FEATURES_SEL, 0);
        virtio_mmio_write32(mmio, VIRTIO_MMIO_DRIVER_FEATURES, features_lo);

        /* Write high 32 bits - MUST include VERSION_1 (bit 0 of high word = bit 32) */
        virtio_mmio_write32(mmio, VIRTIO_MMIO_DRIVER_FEATURES_SEL, 1);
        virtio_mmio_write32(mmio, VIRTIO_MMIO_DRIVER_FEATURES, features_hi | 1);  /* Bit 32 = VERSION_1 */
    }

    /* Set FEATURES_OK */
    status = virtio_mmio_read32(mmio, VIRTIO_MMIO_STATUS);
    virtio_mmio_write32(mmio, VIRTIO_MMIO_STATUS, status | VIRTIO_STATUS_FEATURES_OK);

    /* Verify FEATURES_OK */
    status = virtio_mmio_read32(mmio, VIRTIO_MMIO_STATUS);
    if (!(status & VIRTIO_STATUS_FEATURES_OK)) {
        klog_error("Device did not accept our features");
        return -1;
    }

    /* Initialize control virtqueue (queue 0) */
    if (virtqueue_init(&ctrl_vq, mmio, 0) != 0) {
        klog_error("Failed to initialize control virtqueue");
        return -1;
    }

    /* Set DRIVER_OK */
    status = virtio_mmio_read32(mmio, VIRTIO_MMIO_STATUS);
    virtio_mmio_write32(mmio, VIRTIO_MMIO_STATUS, status | VIRTIO_STATUS_DRIVER_OK);

    gpu_dev.initialized = true;
    gpu_dev.num_scanouts = 1;  /* Assume 1 display for now */
    gpu_dev.resource_id = 1;   /* Start resource IDs at 1 */
    gpu_dev.display_resource_id = 0;  /* Display not yet set up */

    /* Verify device status */
    status = virtio_mmio_read32(mmio, VIRTIO_MMIO_STATUS);
    klog_debug("Final device status: 0x%x", status);

    klog_info("VirtIO GPU initialized successfully!");

    return 0;
}

/**
 * Create 2D resource
 */
uint32_t virtio_gpu_create_resource(uint32_t width, uint32_t height, uint32_t format)
{
    virtio_gpu_resource_create_2d_t cmd;
    virtio_gpu_ctrl_hdr_t resp;
    uint32_t resource_id;

    if (!gpu_dev.initialized) {
        return 0;
    }

    resource_id = gpu_dev.resource_id++;

    /* Prepare command */
    memset(&cmd, 0, sizeof(cmd));
    cmd.hdr.type = VIRTIO_GPU_CMD_RESOURCE_CREATE_2D;
    cmd.resource_id = resource_id;
    cmd.format = format;
    cmd.width = width;
    cmd.height = height;

    klog_debug("GPU CMD: CREATE_2D (0x%x) res=%u %ux%u fmt=%u",
               cmd.hdr.type, resource_id, width, height, format);

    /* Submit command */
    memset(&resp, 0, sizeof(resp));
    if (virtio_gpu_submit_cmd(&cmd, sizeof(cmd), &resp, sizeof(resp)) != 0) {
        klog_error("Failed to create resource");
        return 0;
    }

    klog_debug("Created 2D resource %u (%ux%u, format=%u)", resource_id, width, height, format);
    return resource_id;
}

/**
 * Set scanout
 */
int virtio_gpu_set_scanout(uint32_t scanout_id, uint32_t resource_id,
                            uint32_t width, uint32_t height)
{
    virtio_gpu_set_scanout_t cmd;
    virtio_gpu_ctrl_hdr_t resp;

    if (!gpu_dev.initialized) {
        return -1;
    }

    /* Prepare command */
    memset(&cmd, 0, sizeof(cmd));
    cmd.hdr.type = VIRTIO_GPU_CMD_SET_SCANOUT;
    cmd.scanout_id = scanout_id;
    cmd.resource_id = resource_id;
    cmd.r.x = 0;
    cmd.r.y = 0;
    cmd.r.width = width;
    cmd.r.height = height;

    /* Submit command */
    memset(&resp, 0, sizeof(resp));
    if (virtio_gpu_submit_cmd(&cmd, sizeof(cmd), &resp, sizeof(resp)) != 0) {
        klog_error("Failed to set scanout");
        return -1;
    }

    klog_debug("Set scanout %u to resource %u (%ux%u)", scanout_id, resource_id, width, height);
    return 0;
}

/**
 * Transfer to host
 */
int virtio_gpu_transfer_to_host(uint32_t resource_id, uint32_t x, uint32_t y,
                                  uint32_t width, uint32_t height)
{
    virtio_gpu_transfer_to_host_2d_t cmd;
    virtio_gpu_ctrl_hdr_t resp;

    if (!gpu_dev.initialized) {
        return -1;
    }

    /* Prepare command */
    memset(&cmd, 0, sizeof(cmd));
    cmd.hdr.type = VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D;
    cmd.r.x = x;
    cmd.r.y = y;
    cmd.r.width = width;
    cmd.r.height = height;
    cmd.offset = 0;
    cmd.resource_id = resource_id;

    /* Submit command */
    memset(&resp, 0, sizeof(resp));
    if (virtio_gpu_submit_cmd(&cmd, sizeof(cmd), &resp, sizeof(resp)) != 0) {
        klog_error("Failed to transfer to host");
        return -1;
    }

    return 0;
}

/**
 * Flush resource
 */
int virtio_gpu_flush(uint32_t resource_id, uint32_t x, uint32_t y,
                      uint32_t width, uint32_t height)
{
    virtio_gpu_resource_flush_t cmd;
    virtio_gpu_ctrl_hdr_t resp;

    if (!gpu_dev.initialized) {
        return -1;
    }

    /* Prepare command */
    memset(&cmd, 0, sizeof(cmd));
    cmd.hdr.type = VIRTIO_GPU_CMD_RESOURCE_FLUSH;
    cmd.r.x = x;
    cmd.r.y = y;
    cmd.r.width = width;
    cmd.r.height = height;
    cmd.resource_id = resource_id;

    /* Submit command */
    memset(&resp, 0, sizeof(resp));
    if (virtio_gpu_submit_cmd(&cmd, sizeof(cmd), &resp, sizeof(resp)) != 0) {
        klog_error("Failed to flush resource");
        return -1;
    }

    return 0;
}

/**
 * Attach backing store (framebuffer memory) to resource
 */
static int virtio_gpu_attach_backing(uint32_t resource_id, void *addr, uint32_t size)
{
    struct {
        virtio_gpu_resource_attach_backing_t hdr;
        virtio_gpu_mem_entry_t mem;
    } __attribute__((packed)) cmd;
    virtio_gpu_ctrl_hdr_t resp;

    if (!gpu_dev.initialized) {
        return -1;
    }

    /* Prepare command */
    memset(&cmd, 0, sizeof(cmd));
    cmd.hdr.hdr.type = VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING;
    cmd.hdr.resource_id = resource_id;
    cmd.hdr.nr_entries = 1;
    cmd.mem.addr = (uint64_t)addr;
    cmd.mem.length = size;

    /* Submit command */
    memset(&resp, 0, sizeof(resp));
    if (virtio_gpu_submit_cmd(&cmd, sizeof(cmd), &resp, sizeof(resp)) != 0) {
        klog_error("Failed to attach backing");
        return -1;
    }

    klog_debug("Attached backing %p (size=%u) to resource %u", addr, size, resource_id);
    return 0;
}

/**
 * Update display - complete display setup and update
 */
int virtio_gpu_update_display(void)
{
    fb_info_t *fb;
    uint32_t resource_id;
    uint32_t format;

    if (!gpu_dev.initialized) {
        return -1;
    }

    /* Get framebuffer info */
    fb = fb_get_info();
    if (!fb || !fb->initialized) {
        klog_error("Framebuffer not initialized");
        return -1;
    }

    /* Check if display is already set up */
    if (gpu_dev.display_resource_id != 0) {
        /* Display already configured - just transfer and flush */
        resource_id = gpu_dev.display_resource_id;

        /* Transfer framebuffer data to host */
        if (virtio_gpu_transfer_to_host(resource_id, 0, 0, fb->width, fb->height) != 0) {
            /* Transfer failed - don't log error on every frame */
            return -1;
        }

        /* Flush to update display */
        if (virtio_gpu_flush(resource_id, 0, 0, fb->width, fb->height) != 0) {
            return -1;
        }

        return 0;
    }

    /* First-time setup */
    klog_info("Setting up VirtIO GPU display:");
    klog_info("  Framebuffer: %ux%u @ %p", fb->width, fb->height, fb->base);

    /* Determine pixel format (our framebuffer is XRGB8888) */
    format = VIRTIO_GPU_FORMAT_X8R8G8B8_UNORM;

    /* Step 1: Create 2D resource */
    klog_debug("Step 1: Creating 2D resource...");
    resource_id = virtio_gpu_create_resource(fb->width, fb->height, format);
    if (resource_id == 0) {
        klog_error("Failed to create display resource");
        return -1;
    }
    klog_debug("  Resource created: ID=%u", resource_id);

    /* Step 2: Attach our framebuffer as backing store */
    klog_debug("Step 2: Attaching backing store...");
    if (virtio_gpu_attach_backing(resource_id, fb->base, fb->pitch * fb->height) != 0) {
        klog_error("Failed to attach framebuffer backing");
        return -1;
    }
    klog_debug("  Backing attached");

    /* Step 3: Set scanout to connect resource to display 0 */
    klog_debug("Step 3: Setting scanout...");
    if (virtio_gpu_set_scanout(0, resource_id, fb->width, fb->height) != 0) {
        klog_error("Failed to set scanout");
        return -1;
    }
    klog_debug("  Scanout configured");

    /* Step 4: Transfer framebuffer data to host */
    klog_debug("Step 4: Transferring to host...");
    if (virtio_gpu_transfer_to_host(resource_id, 0, 0, fb->width, fb->height) != 0) {
        klog_error("Failed to transfer to host");
        return -1;
    }
    klog_debug("  Transfer complete");

    /* Step 5: Flush to update display */
    klog_debug("Step 5: Flushing display...");
    if (virtio_gpu_flush(resource_id, 0, 0, fb->width, fb->height) != 0) {
        klog_error("Failed to flush display");
        return -1;
    }
    klog_debug("  Flush complete");

    /* Save resource ID for future updates */
    gpu_dev.display_resource_id = resource_id;

    klog_info("VirtIO GPU display setup complete!");
    klog_info("Graphics should now appear in QEMU window");

    return 0;
}

/* ============================================================================
 * End of virtio_gpu.c
 * ============================================================================ */

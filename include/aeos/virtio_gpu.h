/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: include/aeos/virtio_gpu.h
 * Description: VirtIO GPU driver
 * ============================================================================ */

#ifndef AEOS_VIRTIO_GPU_H
#define AEOS_VIRTIO_GPU_H

#include <aeos/types.h>
#include <aeos/virtio.h>

/* VirtIO MMIO device region (QEMU virt board) */
#define VIRTIO_MMIO_BASE    0x0a000000  /* Base address for MMIO devices */
#define VIRTIO_MMIO_SIZE    0x200       /* Size of each device slot */
#define VIRTIO_MMIO_COUNT   32          /* Maximum number of devices */

/* VirtIO GPU feature bits */
#define VIRTIO_GPU_F_VIRGL          0
#define VIRTIO_GPU_F_EDID           1

/* VirtIO GPU commands */
#define VIRTIO_GPU_CMD_GET_DISPLAY_INFO         0x0100
#define VIRTIO_GPU_CMD_RESOURCE_CREATE_2D       0x0101
#define VIRTIO_GPU_CMD_RESOURCE_UNREF           0x0102
#define VIRTIO_GPU_CMD_SET_SCANOUT              0x0103
#define VIRTIO_GPU_CMD_RESOURCE_FLUSH           0x0104
#define VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D      0x0105
#define VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING  0x0106
#define VIRTIO_GPU_CMD_RESOURCE_DETACH_BACKING  0x0107
#define VIRTIO_GPU_CMD_GET_CAPSET_INFO          0x0108
#define VIRTIO_GPU_CMD_GET_CAPSET               0x0109
#define VIRTIO_GPU_CMD_GET_EDID                 0x010a

/* VirtIO GPU pixel formats */
#define VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM   1
#define VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM   2
#define VIRTIO_GPU_FORMAT_A8R8G8B8_UNORM   3
#define VIRTIO_GPU_FORMAT_X8R8G8B8_UNORM   4

/* VirtIO GPU command header */
typedef struct {
    uint32_t type;
    uint32_t flags;
    uint64_t fence_id;
    uint32_t ctx_id;
    uint32_t padding;
} __attribute__((packed)) virtio_gpu_ctrl_hdr_t;

/* VirtIO GPU rectangle */
typedef struct {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
} __attribute__((packed)) virtio_gpu_rect_t;

/* VirtIO GPU display info response */
typedef struct {
    virtio_gpu_ctrl_hdr_t hdr;
    struct {
        virtio_gpu_rect_t r;
        uint32_t enabled;
        uint32_t flags;
    } pmodes[16];
} __attribute__((packed)) virtio_gpu_resp_display_info_t;

/* VirtIO GPU 2D resource create */
typedef struct {
    virtio_gpu_ctrl_hdr_t hdr;
    uint32_t resource_id;
    uint32_t format;
    uint32_t width;
    uint32_t height;
} __attribute__((packed)) virtio_gpu_resource_create_2d_t;

/* VirtIO GPU set scanout */
typedef struct {
    virtio_gpu_ctrl_hdr_t hdr;
    virtio_gpu_rect_t r;
    uint32_t scanout_id;
    uint32_t resource_id;
} __attribute__((packed)) virtio_gpu_set_scanout_t;

/* VirtIO GPU transfer to host 2D */
typedef struct {
    virtio_gpu_ctrl_hdr_t hdr;
    virtio_gpu_rect_t r;
    uint64_t offset;
    uint32_t resource_id;
    uint32_t padding;
} __attribute__((packed)) virtio_gpu_transfer_to_host_2d_t;

/* VirtIO GPU resource flush */
typedef struct {
    virtio_gpu_ctrl_hdr_t hdr;
    virtio_gpu_rect_t r;
    uint32_t resource_id;
    uint32_t padding;
} __attribute__((packed)) virtio_gpu_resource_flush_t;

/* VirtIO GPU memory entry (for attach backing) */
typedef struct {
    uint64_t addr;
    uint32_t length;
    uint32_t padding;
} __attribute__((packed)) virtio_gpu_mem_entry_t;

/* VirtIO GPU attach backing */
typedef struct {
    virtio_gpu_ctrl_hdr_t hdr;
    uint32_t resource_id;
    uint32_t nr_entries;
    /* Followed by virtio_gpu_mem_entry_t entries */
} __attribute__((packed)) virtio_gpu_resource_attach_backing_t;

/* VirtIO GPU driver state */
typedef struct {
    virtio_device_t vdev;
    uint32_t num_scanouts;
    uint32_t resource_id;
    bool initialized;
} virtio_gpu_t;

/**
 * Initialize VirtIO GPU driver
 * @return 0 on success, -1 on error
 */
int virtio_gpu_init(void);

/**
 * Create 2D resource
 * @param width Width in pixels
 * @param height Height in pixels
 * @param format Pixel format
 * @return resource ID or 0 on error
 */
uint32_t virtio_gpu_create_resource(uint32_t width, uint32_t height, uint32_t format);

/**
 * Set scanout (connect resource to display)
 * @param scanout_id Display ID (usually 0)
 * @param resource_id Resource ID
 * @param width Width in pixels
 * @param height Height in pixels
 * @return 0 on success, -1 on error
 */
int virtio_gpu_set_scanout(uint32_t scanout_id, uint32_t resource_id,
                            uint32_t width, uint32_t height);

/**
 * Transfer framebuffer to host
 * @param resource_id Resource ID
 * @param x X offset
 * @param y Y offset
 * @param width Width in pixels
 * @param height Height in pixels
 * @return 0 on success, -1 on error
 */
int virtio_gpu_transfer_to_host(uint32_t resource_id, uint32_t x, uint32_t y,
                                  uint32_t width, uint32_t height);

/**
 * Flush resource (update display)
 * @param resource_id Resource ID
 * @param x X offset
 * @param y Y offset
 * @param width Width in pixels
 * @param height Height in pixels
 * @return 0 on success, -1 on error
 */
int virtio_gpu_flush(uint32_t resource_id, uint32_t x, uint32_t y,
                      uint32_t width, uint32_t height);

/**
 * Update display with current framebuffer
 * @return 0 on success, -1 on error
 */
int virtio_gpu_update_display(void);

#endif /* AEOS_VIRTIO_GPU_H */

/* ============================================================================
 * End of virtio_gpu.h
 * ============================================================================ */

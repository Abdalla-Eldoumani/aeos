/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: include/aeos/virtio.h
 * Description: VirtIO protocol definitions
 * ============================================================================ */

#ifndef AEOS_VIRTIO_H
#define AEOS_VIRTIO_H

#include <aeos/types.h>

/* VirtIO MMIO registers (QEMU virt board) */
#define VIRTIO_MMIO_MAGIC           0x000  /* 'virt' */
#define VIRTIO_MMIO_VERSION         0x004  /* Version (should be 2) */
#define VIRTIO_MMIO_DEVICE_ID       0x008  /* Device type */
#define VIRTIO_MMIO_VENDOR_ID       0x00c  /* Vendor ID */
#define VIRTIO_MMIO_DEVICE_FEATURES 0x010  /* Device features (read) */
#define VIRTIO_MMIO_DEVICE_FEATURES_SEL 0x014 /* Device features selector */
#define VIRTIO_MMIO_DRIVER_FEATURES 0x020  /* Driver features (write) */
#define VIRTIO_MMIO_DRIVER_FEATURES_SEL 0x024 /* Driver features selector */
#define VIRTIO_MMIO_GUEST_PAGE_SIZE 0x028  /* Guest page size (legacy v1 only) */
#define VIRTIO_MMIO_QUEUE_SEL       0x030  /* Queue selector */
#define VIRTIO_MMIO_QUEUE_NUM_MAX   0x034  /* Max queue size */
#define VIRTIO_MMIO_QUEUE_NUM       0x038  /* Queue size */
#define VIRTIO_MMIO_QUEUE_ALIGN     0x03c  /* Queue align (legacy v1 only) */
#define VIRTIO_MMIO_QUEUE_PFN       0x040  /* Queue PFN (legacy v1 only) */
#define VIRTIO_MMIO_QUEUE_READY     0x044  /* Queue ready (v2 only) */
#define VIRTIO_MMIO_QUEUE_NOTIFY    0x050  /* Queue notify */
#define VIRTIO_MMIO_INTERRUPT_STATUS 0x060 /* Interrupt status */
#define VIRTIO_MMIO_INTERRUPT_ACK   0x064  /* Interrupt acknowledge */
#define VIRTIO_MMIO_STATUS          0x070  /* Device status */
#define VIRTIO_MMIO_QUEUE_DESC_LOW  0x080  /* Queue descriptor low 32 bits */
#define VIRTIO_MMIO_QUEUE_DESC_HIGH 0x084  /* Queue descriptor high 32 bits */
#define VIRTIO_MMIO_QUEUE_AVAIL_LOW 0x090  /* Queue available low 32 bits */
#define VIRTIO_MMIO_QUEUE_AVAIL_HIGH 0x094 /* Queue available high 32 bits */
#define VIRTIO_MMIO_QUEUE_USED_LOW  0x0a0  /* Queue used low 32 bits */
#define VIRTIO_MMIO_QUEUE_USED_HIGH 0x0a4  /* Queue used high 32 bits */
#define VIRTIO_MMIO_CONFIG_GENERATION 0x0fc /* Configuration generation */

/* VirtIO device IDs */
#define VIRTIO_ID_NETWORK   1
#define VIRTIO_ID_BLOCK     2
#define VIRTIO_ID_CONSOLE   3
#define VIRTIO_ID_RNG       4
#define VIRTIO_ID_BALLOON   5
#define VIRTIO_ID_GPU       16

/* VirtIO status register bits */
#define VIRTIO_STATUS_ACKNOWLEDGE   1
#define VIRTIO_STATUS_DRIVER        2
#define VIRTIO_STATUS_DRIVER_OK     4
#define VIRTIO_STATUS_FEATURES_OK   8
#define VIRTIO_STATUS_FAILED        128

/* VirtIO feature bits */
#define VIRTIO_F_VERSION_1  (1ULL << 32)

/* Virtqueue descriptor flags */
#define VIRTQ_DESC_F_NEXT       1  /* Descriptor continues */
#define VIRTQ_DESC_F_WRITE      2  /* Buffer is write-only */
#define VIRTQ_DESC_F_INDIRECT   4  /* Buffer contains list of descriptors */

/* Virtqueue descriptor */
typedef struct {
    uint64_t addr;      /* Physical address */
    uint32_t len;       /* Length */
    uint16_t flags;     /* Flags */
    uint16_t next;      /* Next descriptor index */
} __attribute__((packed)) virtq_desc_t;

/* Virtqueue available ring */
typedef struct {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[];
} __attribute__((packed)) virtq_avail_t;

/* Virtqueue used element */
typedef struct {
    uint32_t id;        /* Descriptor index */
    uint32_t len;       /* Bytes written */
} __attribute__((packed)) virtq_used_elem_t;

/* Virtqueue used ring */
typedef struct {
    uint16_t flags;
    uint16_t idx;
    virtq_used_elem_t ring[];
} __attribute__((packed)) virtq_used_t;

/* VirtIO device structure */
typedef struct {
    volatile uint32_t *mmio_base;  /* MMIO register base */
    uint32_t device_id;            /* Device type */
    uint32_t vendor_id;            /* Vendor ID */
    uint64_t features;             /* Supported features */
    bool initialized;              /* Initialization status */
} virtio_device_t;

/**
 * Initialize VirtIO device
 * @param base MMIO base address
 * @param dev Output device structure
 * @return 0 on success, -1 on error
 */
int virtio_init_device(volatile void *base, virtio_device_t *dev);

/**
 * Write to VirtIO MMIO register
 */
static inline void virtio_mmio_write32(volatile uint32_t *base, uint32_t offset, uint32_t value)
{
    base[offset / 4] = value;
}

/**
 * Read from VirtIO MMIO register
 */
static inline uint32_t virtio_mmio_read32(volatile uint32_t *base, uint32_t offset)
{
    return base[offset / 4];
}

#endif /* AEOS_VIRTIO_H */

/* ============================================================================
 * End of virtio.h
 * ============================================================================ */

/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: src/drivers/virtio_net.c
 * Description: VirtIO network device driver - legacy v1, poll-driven.
 *
 * The device on QEMU virt is LEGACY v1 (VIRTIO_MMIO_VERSION == 1, no
 * VIRTIO_F_VERSION_1 offered), so this mirrors virtio_input.c's PFN virtqueue
 * setup, NOT virtio_gpu.c's v2 QUEUE_DESC/QUEUE_READY path (dead code here).
 * It negotiates VIRTIO_NET_F_MAC only, which forces the 10-byte virtio_net_hdr
 * and single-descriptor RX. RX = queue 0, TX = queue 1. The path is driven by
 * net_tx / net_rx_poll callers - there is NO IRQ (the TEST kernel_main has no
 * GIC/timer, and the round-trip scenario lives there), so no gic_enable_irq.
 * ============================================================================ */

#include <aeos/virtio_net.h>
#include <aeos/virtio.h>
#include <aeos/virtio_gpu.h>   /* VIRTIO_MMIO_BASE / VIRTIO_MMIO_SIZE / VIRTIO_MMIO_COUNT */
#include <aeos/spinlock.h>
#include <aeos/kprintf.h>
#include <aeos/heap.h>
#include <aeos/string.h>

/* Driver ring sizes. QUEUE_NUM_MAX is 1024 on this device; cap to a small
 * driver ring (the gpu/input drivers cap to 64). One reused TX buffer is
 * enough because net_lock serializes net_tx. */
#define RXQ_SIZE        32
#define TXQ_SIZE        32

/* Per-descriptor data buffer: the 10-byte header + up to NET_RX_BUF_SIZE of
 * frame. 2048 is comfortably above the 1514-byte max untagged Ethernet frame. */
#define NET_BUF_SIZE    2048

/* The legacy virtio_net_hdr is exactly 10 bytes with MAC-only negotiated. */
#define NET_HDR_LEN     10

/* A virtqueue (RX or TX) on the legacy PFN path. */
typedef struct {
    virtq_desc_t  *desc;
    virtq_avail_t *avail;
    virtq_used_t  *used;
    uint8_t       *bufs;        /* RXQ_SIZE/TXQ_SIZE contiguous NET_BUF_SIZE buffers */
    uint16_t       size;        /* ring size (RXQ_SIZE or TXQ_SIZE) */
    uint16_t       last_used;   /* our cursor into the used ring */
} net_virtqueue_t;

/* File-static driver state. */
static volatile uint32_t *net_mmio = 0;
static uint8_t  net_mac[6];
static bool     net_initialized = false;
static net_virtqueue_t rxq;
static net_virtqueue_t txq;

/* The Phase 7 spinlock_t over the shared net-path state: the RX/TX queue
 * indices (avail->idx, last_used) updated by net_tx and net_rx_poll. 08-04
 * updates the pending-ping state under this SAME lock so the queue cursors and
 * the ping result move atomically. This is the FEAT-05 criterion-4 discharge -
 * the real Phase 7 lock on the shared state, NOT an ad hoc disable-IRQ scheme.
 * Uncontended in production (only the primary drives the net path), but the
 * lock is present and correct on the weak memory model. */
static spinlock_t net_lock = SPINLOCK_INIT;

/* Format one byte as exactly two lowercase hex digits. kprintf's print_uint
 * ignores the width modifier, so "%02x" does NOT zero-pad here (a 0x00 byte
 * would print as a single "0"); build the MAC string by hand to keep it
 * unambiguous on serial (criterion 1). */
static void byte_to_hex2(uint8_t b, char *out)
{
    static const char digits[] = "0123456789abcdef";
    out[0] = digits[(b >> 4) & 0xf];
    out[1] = digits[b & 0xf];
}

/**
 * Set up one legacy PFN virtqueue. Mirrors input_virtqueue_init
 * (virtio_input.c:78-188): one 4KB-aligned kmalloc region holds the descriptor
 * table, the avail ring, the page-aligned used ring, and `ring_size`
 * NET_BUF_SIZE data buffers, registered via QUEUE_NUM / QUEUE_ALIGN / QUEUE_PFN
 * (the version-1 path - this device offers no v2).
 *
 * For RX (is_rx): every descriptor is VIRTQ_DESC_F_WRITE with its full
 * NET_BUF_SIZE buffer, and all descriptors are posted to the avail ring so the
 * device has somewhere to write inbound frames before the first notify
 * (Pitfall 3). For TX: descriptors are flags=0 (device reads) and NOT posted;
 * net_tx posts on demand.
 * @return 0 on success, -1 on allocation failure or a zero-size queue.
 */
static int net_virtqueue_init(net_virtqueue_t *vq, volatile uint32_t *mmio,
                              uint32_t queue_idx, bool is_rx, uint16_t ring_size)
{
    virtio_mmio_write32(mmio, VIRTIO_MMIO_QUEUE_SEL, queue_idx);

    uint32_t qmax = virtio_mmio_read32(mmio, VIRTIO_MMIO_QUEUE_NUM_MAX);
    if (qmax == 0) {
        return -1;
    }
    if (ring_size > qmax) {
        ring_size = (uint16_t)qmax;
    }

    size_t desc_size  = sizeof(virtq_desc_t) * ring_size;
    size_t avail_size = sizeof(uint16_t) * (3 + ring_size);
    size_t used_size  = sizeof(uint16_t) * 3 + sizeof(virtq_used_elem_t) * ring_size;

    size_t avail_offset = desc_size;
    size_t used_offset  = (desc_size + avail_size + 4095) & ~4095ULL;
    size_t buf_offset   = used_offset + used_size;
    size_t bufs_size    = (size_t)ring_size * NET_BUF_SIZE;

    /* +8192 of slack so the 4KB realignment below always fits inside the block. */
    size_t total = buf_offset + bufs_size + 8192;
    uint8_t *raw = (uint8_t *)kmalloc(total);
    if (!raw) {
        return -1;
    }

    uint64_t aligned = ((uint64_t)raw + 4095) & ~4095ULL;
    uint8_t *mem = (uint8_t *)aligned;
    memset(mem, 0, buf_offset + bufs_size);

    vq->desc  = (virtq_desc_t *)mem;
    vq->avail = (virtq_avail_t *)(mem + avail_offset);
    vq->used  = (virtq_used_t *)(mem + used_offset);
    vq->bufs  = mem + buf_offset;
    vq->size  = ring_size;
    vq->last_used = 0;

    /* Point each descriptor at its NET_BUF_SIZE slice. RX buffers are
     * write-only (device fills them); TX buffers are read by the device. */
    for (uint16_t i = 0; i < ring_size; i++) {
        vq->desc[i].addr  = (uint64_t)(vq->bufs + (size_t)i * NET_BUF_SIZE);
        vq->desc[i].len   = NET_BUF_SIZE;
        vq->desc[i].flags = is_rx ? VIRTQ_DESC_F_WRITE : 0;
        vq->desc[i].next  = 0;
    }
    vq->avail->flags = 0;
    vq->avail->idx   = 0;

    /* Legacy registration: QUEUE_NUM, then QUEUE_ALIGN + QUEUE_PFN. */
    virtio_mmio_write32(mmio, VIRTIO_MMIO_QUEUE_NUM, ring_size);
    virtio_mmio_write32(mmio, VIRTIO_MMIO_QUEUE_ALIGN, 4096);
    virtio_mmio_write32(mmio, VIRTIO_MMIO_QUEUE_PFN, (uint32_t)((uint64_t)vq->desc >> 12));

    klog_info("  net queue %u: size=%u desc=%p (RX=%u), PFN=0x%x",
              queue_idx, ring_size, vq->desc, (unsigned)is_rx,
              (uint32_t)((uint64_t)vq->desc >> 12));

    /* Post all RX descriptors so the device can receive immediately. TX stays
     * empty until net_tx posts. */
    if (is_rx) {
        __asm__ volatile("dmb ish" ::: "memory");
        for (uint16_t i = 0; i < ring_size; i++) {
            vq->avail->ring[i] = i;
        }
        __asm__ volatile("dmb ish" ::: "memory");
        vq->avail->idx = ring_size;
        __asm__ volatile("dmb ish" ::: "memory");
    }

    return 0;
}

/**
 * Run the legacy v1 status handshake + MAC-only feature negotiation on the
 * already-located net device, then read the MAC. Mirrors init_input_device
 * (virtio_input.c:193-279) with two divergences: driver features =
 * feat_lo & VIRTIO_NET_F_MAC (input echoes all of feat_lo), and there is no
 * v2 VERSION_1 branch (this device is version 1). Does NOT set DRIVER_OK -
 * that follows the queue setup in virtio_net_init.
 */
static void net_negotiate(volatile uint32_t *mmio)
{
    uint32_t status;

    /* Reset, then ACKNOWLEDGE + DRIVER. */
    virtio_mmio_write32(mmio, VIRTIO_MMIO_STATUS, 0);
    virtio_mmio_write32(mmio, VIRTIO_MMIO_STATUS, VIRTIO_STATUS_ACKNOWLEDGE);
    status = virtio_mmio_read32(mmio, VIRTIO_MMIO_STATUS);
    virtio_mmio_write32(mmio, VIRTIO_MMIO_STATUS, status | VIRTIO_STATUS_DRIVER);

    /* Legacy guest page size (version == 1). */
    virtio_mmio_write32(mmio, VIRTIO_MMIO_GUEST_PAGE_SIZE, 4096);

    /* Read device features (low 32), accept MAC ONLY. */
    virtio_mmio_write32(mmio, VIRTIO_MMIO_DEVICE_FEATURES_SEL, 0);
    uint32_t feat_lo = virtio_mmio_read32(mmio, VIRTIO_MMIO_DEVICE_FEATURES);
    virtio_mmio_write32(mmio, VIRTIO_MMIO_DRIVER_FEATURES_SEL, 0);
    virtio_mmio_write32(mmio, VIRTIO_MMIO_DRIVER_FEATURES, feat_lo & VIRTIO_NET_F_MAC);

    /* FEATURES_OK is harmless on legacy and has no hard gate; set it but do
     * not fail the probe if the read-back does not echo it. */
    status = virtio_mmio_read32(mmio, VIRTIO_MMIO_STATUS);
    virtio_mmio_write32(mmio, VIRTIO_MMIO_STATUS, status | VIRTIO_STATUS_FEATURES_OK);

    /* Read the 6-byte MAC from device config (offset 0x100). */
    volatile uint8_t *cfg = (volatile uint8_t *)((uint64_t)mmio + VIRTIO_MMIO_CONFIG);
    for (int i = 0; i < 6; i++) {
        net_mac[i] = cfg[i];
    }

    char macstr[18];
    for (int i = 0; i < 6; i++) {
        byte_to_hex2(net_mac[i], &macstr[i * 3]);
        macstr[i * 3 + 2] = (i < 5) ? ':' : '\0';
    }
    klog_info("virtio-net: MAC %s", macstr);
}

int virtio_net_init(void)
{
    uint32_t i;

    klog_debug("Scanning for VirtIO net device...");

    memset(net_mac, 0, sizeof(net_mac));
    memset(&rxq, 0, sizeof(rxq));
    memset(&txq, 0, sizeof(txq));
    net_mmio = 0;
    net_initialized = false;

    /* Scan all virtio-mmio slots for DEVICE_ID == VIRTIO_ID_NETWORK. The slot
     * shifts with the -device set, so match on device_id - never a hardcoded
     * slot (mirror virtio_gpu_init's scan, virtio_gpu.c:290-309). */
    for (i = 0; i < VIRTIO_MMIO_COUNT; i++) {
        uint64_t addr = VIRTIO_MMIO_BASE + ((uint64_t)i * VIRTIO_MMIO_SIZE);
        volatile uint32_t *mmio = (volatile uint32_t *)addr;
        if (virtio_mmio_read32(mmio, VIRTIO_MMIO_MAGIC) != 0x74726976) {
            continue;
        }
        if (virtio_mmio_read32(mmio, VIRTIO_MMIO_DEVICE_ID) == VIRTIO_ID_NETWORK) {
            net_mmio = mmio;
            klog_info("VirtIO net device found at slot %u", i);
            break;
        }
    }

    if (!net_mmio) {
        /* No net device: clean no-op so a boot without -device virtio-net-device
         * (e.g. text-mode make run) still reaches the WM loop. */
        klog_warn("virtio-net not found");
        return -1;
    }

    net_negotiate(net_mmio);

    /* Set up both virtqueues before DRIVER_OK: RX = queue 0 (buffers posted),
     * TX = queue 1 (posted on demand by net_tx). */
    if (net_virtqueue_init(&rxq, net_mmio, 0, true, RXQ_SIZE) != 0) {
        klog_warn("virtio-net: RX queue setup failed");
        return -1;
    }
    if (net_virtqueue_init(&txq, net_mmio, 1, false, TXQ_SIZE) != 0) {
        klog_warn("virtio-net: TX queue setup failed");
        return -1;
    }

    /* DRIVER_OK after both queues exist. */
    uint32_t status = virtio_mmio_read32(net_mmio, VIRTIO_MMIO_STATUS);
    virtio_mmio_write32(net_mmio, VIRTIO_MMIO_STATUS, status | VIRTIO_STATUS_DRIVER_OK);

    /* Notify queue 0 so the device starts consuming the posted RX buffers
     * (mirror virtio_input.c:266-270). */
    virtio_mmio_write32(net_mmio, VIRTIO_MMIO_QUEUE_SEL, 0);
    __asm__ volatile("dmb ish" ::: "memory");
    virtio_mmio_write32(net_mmio, VIRTIO_MMIO_QUEUE_NOTIFY, 0);

    net_initialized = true;
    return 0;
}

/* Transmit one frame. net_lock guards the short queue work (claim the
 * descriptor, write the buffer, bump avail->idx, notify). The bounded
 * completion wait is kept INSIDE the lock for simplicity - the single reused TX
 * buffer means a second net_tx must not start before this one drains, and the
 * wait is bounded so the held region is finite. A wrong queue setup times out
 * (returns -1) rather than hanging (T-08-05). */
int net_tx(const uint8_t *frame, uint32_t len)
{
    if (!net_initialized) {
        return -1;
    }
    /* Cap the frame to the TX buffer minus the 10-byte header so an oversized
     * caller length cannot overrun the 2048-byte buffer. */
    if (len > NET_BUF_SIZE - NET_HDR_LEN) {
        len = NET_BUF_SIZE - NET_HDR_LEN;
    }

    spin_lock(&net_lock);

    uint8_t *tx_buf = txq.bufs;          /* one reused buffer, serialized by the lock */
    memset(tx_buf, 0, NET_HDR_LEN);      /* zero the 10-byte virtio_net_hdr */
    memcpy(tx_buf + NET_HDR_LEN, frame, len);

    uint16_t d = 0;                      /* descriptor 0 (single reused buffer) */
    txq.desc[d].addr  = (uint64_t)tx_buf;
    txq.desc[d].len   = NET_HDR_LEN + len;
    txq.desc[d].flags = 0;               /* device reads */
    txq.desc[d].next  = 0;

    uint16_t ai = txq.avail->idx;
    txq.avail->ring[ai % txq.size] = d;
    __asm__ volatile("dmb ish; dsb ish" ::: "memory");
    txq.avail->idx = (uint16_t)(ai + 1);
    __asm__ volatile("dmb ish; dsb ish" ::: "memory");

    virtio_mmio_write32(net_mmio, VIRTIO_MMIO_QUEUE_NOTIFY, 1);  /* queue 1 = TX */

    /* Bounded wait for completion - never an unbounded loop (T-08-05). */
    uint32_t to = 1000000;
    while (txq.used->idx == txq.last_used && --to) {
        __asm__ volatile("dmb ish" ::: "memory");
    }
    int rc;
    if (txq.used->idx != txq.last_used) {
        txq.last_used++;
        rc = 0;
    } else {
        klog_warn("virtio-net: TX completion timeout");
        rc = -1;
    }

    spin_unlock(&net_lock);
    return rc;
}

/* Poll for one received frame. net_lock guards the short queue work (read the
 * used ring, pop one element, copy the frame, re-post the descriptor). Never
 * blocks - returns 0 when nothing is ready. The wlen >= NET_HDR_LEN guard and
 * the copy cap are the T-08-03 mitigation: a malformed/short inbound frame must
 * not be read past or overrun the caller buffer - reaching the WM loop
 * dominates, a dropped frame is fine, a fault is not. The device cannot split a
 * frame across descriptors because MRG_RXBUF was not negotiated. */
int net_rx_poll(uint8_t *out, uint32_t *len)
{
    if (!net_initialized) {
        return -1;
    }

    spin_lock(&net_lock);

    __asm__ volatile("dmb ish" ::: "memory");
    if (rxq.last_used == rxq.used->idx) {
        spin_unlock(&net_lock);
        return 0;                         /* nothing ready - NOT a fault */
    }

    uint16_t ui = (uint16_t)(rxq.last_used % rxq.size);
    uint32_t id   = rxq.used->ring[ui].id;
    uint32_t wlen = rxq.used->ring[ui].len;
    rxq.last_used++;

    if (id >= rxq.size) {
        /* A used.id outside the ring would index a bogus buffer; drop it and
         * advance (defends against a misbehaving device). */
        spin_unlock(&net_lock);
        return 0;
    }

    int rc;
    if (wlen < NET_HDR_LEN) {
        /* Short frame: cannot contain the 10-byte header. Drop it; re-post the
         * descriptor below. Never subtract past zero. */
        rc = 0;
    } else {
        uint32_t frame_len = wlen - NET_HDR_LEN;
        /* Cap to the actual frame capacity of one descriptor buffer
         * (NET_BUF_SIZE - NET_HDR_LEN = 2038), NOT NET_RX_BUF_SIZE (2048). The
         * source slice is only NET_BUF_SIZE bytes and the header consumes the
         * first NET_HDR_LEN; capping at 2048 would let a device-reported
         * oversized wlen read up to NET_HDR_LEN bytes past this descriptor's
         * slice (off the bufs allocation for the last id). 2038 keeps the read
         * inside the slice for every id, and stays within the caller buffer. */
        if (frame_len > NET_BUF_SIZE - NET_HDR_LEN) {
            frame_len = NET_BUF_SIZE - NET_HDR_LEN;
        }
        memcpy(out, rxq.bufs + (size_t)id * NET_BUF_SIZE + NET_HDR_LEN, frame_len);
        *len = frame_len;
        rc = 1;
    }

    /* Re-post the descriptor to the avail ring so the device can reuse it. */
    uint16_t ai = rxq.avail->idx;
    rxq.avail->ring[ai % rxq.size] = (uint16_t)id;
    __asm__ volatile("dmb ish" ::: "memory");
    rxq.avail->idx = (uint16_t)(ai + 1);
    __asm__ volatile("dmb ish" ::: "memory");
    virtio_mmio_write32(net_mmio, VIRTIO_MMIO_QUEUE_NOTIFY, 0);  /* queue 0 = RX */

    spin_unlock(&net_lock);
    return rc;
}

bool virtio_net_available(void)
{
    return net_initialized;
}

void virtio_net_get_mac(uint8_t out_mac[6])
{
    for (int i = 0; i < 6; i++) {
        out_mac[i] = net_mac[i];
    }
}

/* The net_lock seam for src/net/ (08-03). The stack updates its pending-ping
 * state (the awaited id+seq, got_reply, the cached gateway MAC) under this SAME
 * lock so that state and the queue cursors move atomically (criterion 4). The
 * lock is non-recursive: a holder must release before any net_tx/net_rx_poll
 * (those re-take it) or it self-deadlocks. */
void net_lock_acquire(void)
{
    spin_lock(&net_lock);
}

void net_lock_release(void)
{
    spin_unlock(&net_lock);
}

/* ============================================================================
 * End of virtio_net.c
 * ============================================================================ */

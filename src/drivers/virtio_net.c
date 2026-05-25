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
 * lock is present and correct on the weak memory model. The unused attribute
 * covers the Task-1 skeleton where the primitives are still stubs; Task 2 uses
 * it in net_tx/net_rx_poll. */
static spinlock_t net_lock __attribute__((unused)) = SPINLOCK_INIT;

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
 * Run the legacy v1 status handshake + MAC-only feature negotiation on the
 * already-located net device, then read the MAC. Mirrors init_input_device
 * (virtio_input.c:193-279) with two divergences: driver features =
 * feat_lo & VIRTIO_NET_F_MAC (input echoes all of feat_lo), and there is no
 * v2 VERSION_1 branch (this device is version 1). Does NOT set DRIVER_OK -
 * that follows the queue setup (Task 2).
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

    /* TODO(Task 2): set up the RX(0)/TX(1) legacy PFN virtqueues, post RX
     * buffers, set DRIVER_OK + QUEUE_NOTIFY(0), and implement net_tx/net_rx_poll.
     * Until then the device is probed and the MAC is read (criterion 1). */

    net_initialized = true;
    return 0;
}

int net_tx(const uint8_t *frame, uint32_t len)
{
    /* TODO(Task 2): the poll-driven TX path under net_lock. */
    (void)frame;
    (void)len;
    return -1;
}

int net_rx_poll(uint8_t *out, uint32_t *len)
{
    /* TODO(Task 2): the poll-driven RX path under net_lock. */
    (void)out;
    (void)len;
    return -1;
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

/* ============================================================================
 * End of virtio_net.c
 * ============================================================================ */

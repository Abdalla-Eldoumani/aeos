/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: include/aeos/virtio_net.h
 * Description: VirtIO network device driver interface (legacy v1, poll-driven)
 * ============================================================================ */

#ifndef AEOS_VIRTIO_NET_H
#define AEOS_VIRTIO_NET_H

#include <aeos/types.h>
#include <aeos/virtio.h>

/* Device-specific config space begins at this MMIO byte offset. virtio.h's
 * register map stops at CONFIG_GENERATION (0x0fc); the 6-byte MAC the device
 * exposes when VIRTIO_NET_F_MAC is negotiated is the first field here. */
#define VIRTIO_MMIO_CONFIG  0x100

/* virtio-net feature bits (low 32). The driver accepts ONLY MAC: that forces
 * the legacy 10-byte virtio_net_hdr and single-descriptor RX. Accepting
 * MRG_RXBUF would grow the header to 12 bytes and let a frame span
 * descriptors, which this poll path does not handle - so it is left unset. */
#define VIRTIO_NET_F_MAC        (1u << 5)
#define VIRTIO_NET_F_MRG_RXBUF  (1u << 15)
#define VIRTIO_NET_F_STATUS     (1u << 16)

/* The maximum Ethernet frame net_rx_poll hands back. RX descriptor buffers are
 * NET_RX_BUF_SIZE + the 10-byte header; the copy out is capped at this so a
 * near-buffer-sized frame cannot overrun a caller buffer sized against it.
 * Callers size their receive buffer to NET_RX_BUF_SIZE. */
#define NET_RX_BUF_SIZE     2048u

/* Legacy virtio_net_hdr: exactly 10 bytes when MRG_RXBUF is NOT negotiated
 * (virtio v1.2 spec 5.1.6). Prepended to every TX frame, present at the front
 * of every RX frame. All fields are zero for the plain (no-offload, no-GSO)
 * frames this driver sends. */
struct virtio_net_hdr {
    uint8_t  flags;
    uint8_t  gso_type;
    uint16_t hdr_len;
    uint16_t gso_size;
    uint16_t csum_start;
    uint16_t csum_offset;
} __attribute__((packed));

/**
 * Scan the virtio-mmio slots for a virtio-net device (DEVICE_ID == 1) and, on
 * a match, run the legacy v1 init: status handshake, MAC-only negotiation,
 * RX(queue 0) / TX(queue 1) PFN virtqueues, RX buffer post, and read+print the
 * 6-byte MAC. Poll-driven: no IRQ is registered.
 * @return 0 on success, -1 when no net device is present (clean no-op).
 */
int virtio_net_init(void);

/**
 * Transmit one Ethernet frame. Prepends the 10-byte header, posts a single TX
 * descriptor to queue 1, notifies, and bounded-waits for completion. Holds the
 * net lock across the short queue work.
 * @param frame the Ethernet frame (no virtio_net_hdr - the driver prepends it).
 * @param len   the frame length in bytes.
 * @return 0 on completion, -1 on timeout or when no device is present.
 */
int net_tx(const uint8_t *frame, uint32_t len);

/**
 * Poll the RX queue for one received Ethernet frame (poll-driven, never
 * blocks). On a frame it copies the bytes after the 10-byte header into out
 * (capped at NET_RX_BUF_SIZE), sets *len, and re-posts the descriptor. Holds
 * the net lock across the short queue work.
 * @param out a caller buffer of at least NET_RX_BUF_SIZE bytes.
 * @param len out: the frame length written to out.
 * @return 1 if a frame was returned, 0 if nothing is ready (or a short frame
 *         was dropped), -1 when no device is present.
 */
int net_rx_poll(uint8_t *out, uint32_t *len);

/**
 * @return true if a virtio-net device was found and initialized.
 */
bool virtio_net_available(void);

/**
 * Copy the 6-byte device MAC into out_mac. Zeroed if no device is present.
 */
void virtio_net_get_mac(uint8_t out_mac[6]);

#endif /* AEOS_VIRTIO_NET_H */

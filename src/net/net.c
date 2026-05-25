/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: src/net/net.c
 * Description: Minimal Ethernet/ARP/IPv4/ICMP stack (FEAT-05).
 *
 * The protocol half of the network path on top of the poll-driven net_tx /
 * net_rx_poll primitives in src/drivers/virtio_net.c. It owns the byte-exact
 * header layouts (see include/aeos/net.h), the one's-complement Internet
 * checksum, the bounds-safe RX dispatch, the pure ARP-reply builder, and the
 * ARP-resolve / ICMP-echo build+send helpers. No TCP/UDP/DHCP/routing.
 *
 * On-the-wire byte order is BIG-endian; this kernel is little-endian, so every
 * multi-byte field is written/read big-endian (the wbe16/rbe16 helpers below)
 * to avoid any unaligned packed-struct store. Everything is integer-only - no
 * FP/SIMD - so it survives -mgeneral-regs-only (CPACR_EL1 forbids Q-registers
 * at EL1). kprintf has no %l/%ll; print wide values with %u/%x casts.
 * ============================================================================ */

#include <aeos/net.h>
#include <aeos/virtio_net.h>
#include <aeos/kprintf.h>
#include <aeos/string.h>

/* Write/read a 16-bit value big-endian (network order) at a byte offset. Used
 * for every 2-byte wire field so a packed frame is never the target of an
 * unaligned 16-bit store. */
static inline void wbe16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xff);
}

static inline uint16_t rbe16(const uint8_t *p)
{
    return (uint16_t)((p[0] << 8) | p[1]);
}

/* 16-bit one's-complement Internet checksum. Probe-validated for BOTH the
 * 20-byte IPv4 header and the 8-byte ICMP message (08-RESEARCH Q3). Pure: no
 * I/O, no statics - so 08-04's RED gate can assert a known frame's checksum
 * directly. Zero the checksum field before calling, then store the result
 * big-endian into it. */
uint16_t inet_csum(const uint8_t *data, uint32_t len)
{
    uint32_t sum = 0;
    while (len > 1) {
        sum += (uint16_t)((data[0] << 8) | data[1]);
        data += 2;
        len -= 2;
    }
    if (len) {
        sum += (uint16_t)(data[0] << 8);   /* odd trailing byte, low half zero */
    }
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    return (uint16_t)(~sum);
}

/* Build an opcode-2 ARP reply for a valid inbound request. PURE - no net_tx,
 * no logging - so 08-04's test_net_arp_reply can feed a synthetic request and
 * assert the exact reply bytes (the non-vacuous criterion-2 proof; slirp never
 * ARPs the guest, so the builder unit is the direct proof).
 *
 * Returns WITHOUT writing (0) unless the input is a well-formed ARP request for
 * our_ip: req_len >= 42, ethertype 0x0806, opcode 1, target IP == our_ip. Every
 * one of those checks gates the field reads below it, so a short or non-matching
 * frame is never indexed past. */
int arp_build_reply(const uint8_t *req, uint32_t req_len,
                    const uint8_t our_mac[6], const uint8_t our_ip[4],
                    uint8_t *out)
{
    /* Length first: an Ethernet+ARP frame is exactly 42 bytes; a shorter one
     * cannot hold the fields read below, so reject it before any index. */
    if (req_len < ARP_FRAME_LEN) {
        return 0;
    }
    /* Ethertype must be ARP. */
    if (rbe16(req + ETH_OFF_ETHERTYPE) != ETHERTYPE_ARP) {
        return 0;
    }
    const uint8_t *arp = req + ETH_HDR_LEN;
    /* Opcode must be a request. */
    if (rbe16(arp + ARP_OFF_OPCODE) != ARP_OP_REQUEST) {
        return 0;
    }
    /* Target IP must be ours, else this request is not for us. */
    if (memcmp(arp + ARP_OFF_TARGET_IP, our_ip, IP_ADDR_LEN) != 0) {
        return 0;
    }

    const uint8_t *req_sender_mac = arp + ARP_OFF_SENDER_MAC;
    const uint8_t *req_sender_ip  = arp + ARP_OFF_SENDER_IP;

    /* Ethernet header: dst = the requester's MAC, src = our MAC, type = ARP. */
    memcpy(out + ETH_OFF_DST, req_sender_mac, MAC_ADDR_LEN);
    memcpy(out + ETH_OFF_SRC, our_mac, MAC_ADDR_LEN);
    wbe16(out + ETH_OFF_ETHERTYPE, ETHERTYPE_ARP);

    /* ARP packet: opcode 2 (reply); sender = us, target = the requester. */
    uint8_t *oarp = out + ETH_HDR_LEN;
    wbe16(oarp + ARP_OFF_HTYPE, ARP_HTYPE_ETHERNET);
    wbe16(oarp + ARP_OFF_PTYPE, ETHERTYPE_IPV4);
    oarp[ARP_OFF_HLEN] = ARP_HLEN_ETHERNET;
    oarp[ARP_OFF_PLEN] = ARP_PLEN_IPV4;
    wbe16(oarp + ARP_OFF_OPCODE, ARP_OP_REPLY);
    memcpy(oarp + ARP_OFF_SENDER_MAC, our_mac, MAC_ADDR_LEN);
    memcpy(oarp + ARP_OFF_SENDER_IP, our_ip, IP_ADDR_LEN);
    memcpy(oarp + ARP_OFF_TARGET_MAC, req_sender_mac, MAC_ADDR_LEN);
    memcpy(oarp + ARP_OFF_TARGET_IP, req_sender_ip, IP_ADDR_LEN);

    return ARP_FRAME_LEN;
}

/* ============================================================================
 * End of net.c
 * ============================================================================ */

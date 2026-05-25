/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: include/aeos/net.h
 * Description: Minimal Ethernet/ARP/IPv4/ICMP stack interface (FEAT-05)
 *
 * The protocol half of the network path: device-independent framing on top of
 * the poll-driven net_tx/net_rx_poll primitives in src/drivers/virtio_net.c.
 * It owns the byte-exact header layouts, the integer-only byte-swap helpers,
 * the one's-complement Internet checksum, the bounds-safe RX dispatch, and the
 * ARP-resolve / ICMP-echo build+send helpers. No TCP/UDP/DHCP/routing.
 *
 * On-the-wire byte order is BIG-endian; the kernel is little-endian, so every
 * multi-byte field is swapped through htons/htonl or accessed byte-at-a-time.
 * Everything here is integer-only - no FP/SIMD - so it survives the build's
 * -mgeneral-regs-only constraint (CPACR_EL1 does not permit Q-register use).
 * ============================================================================ */

#ifndef AEOS_NET_H
#define AEOS_NET_H

#include <aeos/types.h>

/* --------------------------------------------------------------------------
 * Byte-swap helpers (integer-only; safe under -mgeneral-regs-only).
 * Network order is big-endian. On this little-endian kernel htons==ntohs and
 * htonl==ntohl (the swap is its own inverse). Probe-validated (08-RESEARCH Q3).
 * -------------------------------------------------------------------------- */
static inline uint16_t htons(uint16_t v) { return (uint16_t)((v << 8) | (v >> 8)); }
static inline uint16_t ntohs(uint16_t v) { return htons(v); }
static inline uint32_t htonl(uint32_t v) {
    return ((v & 0xFFu) << 24) | ((v & 0xFF00u) << 8) |
           ((v >> 8) & 0xFF00u) | ((v >> 24) & 0xFFu);
}
static inline uint32_t ntohl(uint32_t v) { return htonl(v); }

/* --------------------------------------------------------------------------
 * Ethernet header (14 bytes). off 0 dst MAC[6], off 6 src MAC[6],
 * off 12 ethertype[2 BE]. (08-RESEARCH Q3, lines 271-277.)
 * -------------------------------------------------------------------------- */
#define ETH_HDR_LEN        14
#define ETH_OFF_DST        0
#define ETH_OFF_SRC        6
#define ETH_OFF_ETHERTYPE  12
#define ETHERTYPE_ARP      0x0806
#define ETHERTYPE_IPV4     0x0800

/* --------------------------------------------------------------------------
 * ARP packet (28 bytes, after the 14-byte Ethernet header). Offsets are
 * relative to the start of the ARP packet (i.e. ETH_HDR_LEN + ARP_OFF_*).
 * (08-RESEARCH Q3, lines 279-292.)
 * -------------------------------------------------------------------------- */
#define ARP_LEN            28
#define ARP_OFF_HTYPE      0   /* [2 BE] = 1 (Ethernet) */
#define ARP_OFF_PTYPE      2   /* [2 BE] = 0x0800 (IPv4) */
#define ARP_OFF_HLEN       4   /* [1] = 6 */
#define ARP_OFF_PLEN       5   /* [1] = 4 */
#define ARP_OFF_OPCODE     6   /* [2 BE] 1=request 2=reply */
#define ARP_OFF_SENDER_MAC 8   /* [6] */
#define ARP_OFF_SENDER_IP  14  /* [4] */
#define ARP_OFF_TARGET_MAC 18  /* [6] (zero in a request) */
#define ARP_OFF_TARGET_IP  24  /* [4] */
#define ARP_OP_REQUEST     1
#define ARP_OP_REPLY       2
#define ARP_HTYPE_ETHERNET 1
#define ARP_HLEN_ETHERNET  6
#define ARP_PLEN_IPV4      4

/* The full Ethernet+ARP frame: 14 + 28 = 42 bytes. (Probe sent 42, got a reply.) */
#define ARP_FRAME_LEN      (ETH_HDR_LEN + ARP_LEN)

/* --------------------------------------------------------------------------
 * IPv4 header (20 bytes). Offsets relative to the IPv4 header start
 * (ETH_HDR_LEN + IP_OFF_*). (08-RESEARCH Q3, lines 298-311.)
 * -------------------------------------------------------------------------- */
#define IP_HDR_MIN_LEN     20
#define IP_OFF_VER_IHL     0   /* [1] = 0x45 (v4, IHL 5 = 20 bytes) */
#define IP_OFF_DSCP_ECN    1   /* [1] = 0 */
#define IP_OFF_TOTAL_LEN   2   /* [2 BE] = 20 + payload */
#define IP_OFF_ID          4   /* [2 BE] = 0 */
#define IP_OFF_FLAGS_FRAG  6   /* [2 BE] = 0 */
#define IP_OFF_TTL         8   /* [1] = 64 */
#define IP_OFF_PROTO       9   /* [1] = 1 (ICMP) */
#define IP_OFF_CHECKSUM    10  /* [2 BE] over the 20-byte header only */
#define IP_OFF_SRC         12  /* [4] = 10.0.2.15 */
#define IP_OFF_DST         16  /* [4] = 10.0.2.2 */
#define IP_VER_IHL_DEFAULT 0x45
#define IP_TTL_DEFAULT     64
#define IP_PROTO_ICMP      1

/* --------------------------------------------------------------------------
 * ICMP echo (8-byte header + optional payload). Offsets relative to the ICMP
 * message start. (08-RESEARCH Q3, lines 314-323.)
 * -------------------------------------------------------------------------- */
#define ICMP_HDR_LEN       8
#define ICMP_OFF_TYPE      0   /* [1] 8=echo request, 0=echo reply */
#define ICMP_OFF_CODE      1   /* [1] = 0 */
#define ICMP_OFF_CHECKSUM  2   /* [2 BE] over the ICMP header + payload */
#define ICMP_OFF_ID        4   /* [2 BE] identifier */
#define ICMP_OFF_SEQ       6   /* [2 BE] sequence */
#define ICMP_TYPE_ECHO_REQUEST 8
#define ICMP_TYPE_ECHO_REPLY   0

/* --------------------------------------------------------------------------
 * Static config (slirp defaults, VERIFIED 08-RESEARCH). 4-byte arrays in wire
 * (network) byte order so they drop straight into the packet via memcpy - no
 * htonl needed at the call site. Our IP 10.0.2.15, gateway 10.0.2.2.
 * -------------------------------------------------------------------------- */
#define OUR_IP_0 10
#define OUR_IP_1 0
#define OUR_IP_2 2
#define OUR_IP_3 15
#define GW_IP_0  10
#define GW_IP_1  0
#define GW_IP_2  2
#define GW_IP_3  2
#define IP_ADDR_LEN  4
#define MAC_ADDR_LEN 6

/**
 * 16-bit one's-complement Internet checksum over data[0..len). Pure (no I/O).
 * Used for BOTH the 20-byte IPv4 header and the ICMP message. Zero the
 * checksum field first, compute over the span, store the result big-endian.
 * Probe-validated (08-RESEARCH Q3, lines 328-338).
 * @return the folded one's-complement of the 16-bit big-endian sum.
 */
uint16_t inet_csum(const uint8_t *data, uint32_t len);

/**
 * Build an ARP reply (opcode 2) for a valid inbound ARP request. PURE: no
 * net_tx, no logging - so a headless unit test can feed a synthetic request
 * and assert the exact reply bytes (the non-vacuous criterion-2 proof, since
 * slirp never ARPs the guest).
 *
 * Writes a 42-byte reply into out only when the request is well-formed AND its
 * target IP equals our_ip: Ethernet dst = the requester's sender MAC, src =
 * our_mac, ethertype 0x0806; ARP opcode 2, sender = our_mac/our_ip, target =
 * the requester's sender MAC/IP.
 *
 * @param req      the inbound Ethernet+ARP frame.
 * @param req_len  its length in bytes.
 * @param our_mac  our 6-byte MAC.
 * @param our_ip   our 4-byte IP (wire order).
 * @param out      a caller buffer of at least ARP_FRAME_LEN bytes.
 * @return ARP_FRAME_LEN (42) on a written reply; 0 WITHOUT writing if req_len
 *         < 42, the ethertype is not ARP, the opcode is not a request, or the
 *         target IP != our_ip.
 */
int arp_build_reply(const uint8_t *req, uint32_t req_len,
                    const uint8_t our_mac[6], const uint8_t our_ip[4],
                    uint8_t *out);

/**
 * Parse one inbound Ethernet frame and act on it. BOUNDS-CHECKS at every layer
 * (len >= 14 before the ethertype; ARP len >= 42; IPv4 IHL and total-length
 * validated against len before indexing the ICMP payload) and DROPS any
 * malformed/short/inconsistent frame - it never reads past len or faults
 * (reaching the WM loop dominates; T-08-07). Answers an ARP request for our IP,
 * answers an ICMP echo request to our IP, and matches an ICMP echo reply
 * against the pending ping (under net_lock).
 * @param frame the received Ethernet frame (no virtio_net_hdr).
 * @param len   its length in bytes.
 */
void net_rx_dispatch(const uint8_t *frame, uint32_t len);

/**
 * Broadcast an ARP request for target_ip and bounded-poll for the reply.
 * Bounded in BOTH builds (a uptime-ms deadline in production, a spin-count
 * under TEST_BUILD) - never an infinite wait.
 * @param target_ip the 4-byte target IP (wire order).
 * @param out_mac    out: the resolved 6-byte MAC on success.
 * @return 0 + out_mac on success, -1 on the bounded-wait expiry or no device.
 */
int arp_resolve(const uint8_t target_ip[4], uint8_t out_mac[6]);

/**
 * Build Ethernet+IPv4+ICMP(type 8 echo request) with correct IPv4 and ICMP
 * checksums and net_tx it.
 * @param dst_ip  the 4-byte destination IP (wire order).
 * @param dst_mac the 6-byte next-hop MAC.
 * @param id      the ICMP identifier.
 * @param seq     the ICMP sequence number.
 * @return 0 on a successful net_tx, -1 otherwise.
 */
int icmp_send_echo(const uint8_t dst_ip[4], const uint8_t dst_mac[6],
                   uint16_t id, uint16_t seq);

/**
 * The bounded ping driver. Resolves the next hop, sends one ICMP echo with a
 * fresh id+seq (set under net_lock), and bounded-polls for the matching reply.
 * Never infinite. The shell command / boot demo / tests (08-04) call this.
 * @param dst_ip the 4-byte destination IP (wire order).
 * @return 0 on a matched echo reply; -1 on timeout, ARP failure, or no device.
 */
int net_ping(const uint8_t dst_ip[4]);

#endif /* AEOS_NET_H */

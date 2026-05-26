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
#ifndef TEST_BUILD
#include <aeos/timer.h>
#endif

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

/* Our static identity in wire (network) byte order - drops straight into a
 * frame via memcpy, no swap at the call site. */
static const uint8_t our_ip[IP_ADDR_LEN] = { OUR_IP_0, OUR_IP_1, OUR_IP_2, OUR_IP_3 };

/* Shared net-path state, ALWAYS read/written under the driver's net_lock (the
 * SAME lock net_tx/net_rx_poll use over the queue cursors - reached through
 * net_lock_acquire/release; criterion 4). This is NOT a second lock. The RX
 * dispatch updates these on a matching reply; arp_resolve/net_ping read them. */
static volatile bool    arp_pending  = false;       /* a resolve is waiting */
static uint8_t          arp_want_ip[IP_ADDR_LEN];    /* the IP it awaits */
static volatile bool    arp_got      = false;       /* set when matched */
static uint8_t          arp_got_mac[MAC_ADDR_LEN];   /* the resolved MAC */

static volatile bool    ping_pending = false;       /* a ping is waiting */
static volatile uint16_t ping_id     = 0;           /* the awaited ICMP id */
static volatile uint16_t ping_seq    = 0;           /* the awaited ICMP seq */
static volatile bool    ping_got     = false;       /* set on a matched reply */

/* The bounded-wait budget. Production uses a timer_get_uptime_ms() deadline
 * (CNTVCT-backed; advances even when ticks are starved). The TEST kernel has no
 * timer, so it uses a spin-count bound (the probe used ~50M spins, well under
 * the 30s make-test timeout). Either way the wait is FINITE - on expiry the
 * caller returns -1, never an infinite loop (T-08-08). */
#define NET_WAIT_MS     2000u            /* production deadline */
#define NET_WAIT_SPINS  50000000u        /* TEST_BUILD spin bound */

/* Opaque bound token + the two helpers below keep the #ifdef out of the flow
 * bodies: net_wait_start() captures a budget, net_wait_expired(tok) tests it. */
#ifdef TEST_BUILD
typedef uint32_t net_wait_t;
static inline net_wait_t net_wait_start(void) { return NET_WAIT_SPINS; }
static inline bool net_wait_expired(net_wait_t *t) { if (*t == 0) return true; (*t)--; return false; }
#else
typedef uint64_t net_wait_t;
static inline net_wait_t net_wait_start(void) { return timer_get_uptime_ms() + NET_WAIT_MS; }
static inline bool net_wait_expired(net_wait_t *t) { return timer_get_uptime_ms() >= *t; }
#endif

/* Handle an inbound ARP packet. The caller has already verified len >= 42 and
 * the ethertype is ARP, so the ARP fields below are in-bounds. Opcode 1 to our
 * IP -> a unicast reply (net_tx WITHOUT net_lock held); opcode 2 whose sender
 * IP is the IP a resolve awaits -> cache its MAC under net_lock. */
static void net_handle_arp(const uint8_t *frame, uint32_t len)
{
    const uint8_t *arp = frame + ETH_HDR_LEN;
    uint16_t op = rbe16(arp + ARP_OFF_OPCODE);

    if (op == ARP_OP_REQUEST) {
        /* Answer a request for our IP. arp_build_reply is pure and returns 0
         * without writing if the target IP is not ours, so a request for some
         * other address produces no reply. net_tx is called with net_lock NOT
         * held (it self-locks). */
        uint8_t our_mac[MAC_ADDR_LEN];
        virtio_net_get_mac(our_mac);
        uint8_t reply[ARP_FRAME_LEN];
        int rlen = arp_build_reply(frame, len, our_mac, our_ip, reply);
        if (rlen == ARP_FRAME_LEN) {
            net_tx(reply, (uint32_t)rlen);
        }
        return;
    }

    if (op == ARP_OP_REPLY) {
        /* Cache the sender MAC only if a resolve is awaiting this sender IP
         * (anti-spoof, T-08-09). The compare + set is under net_lock so it is
         * consistent with the queue cursors; no net_tx is called here, so the
         * lock is never held across a primitive. */
        const uint8_t *sender_ip  = arp + ARP_OFF_SENDER_IP;
        const uint8_t *sender_mac = arp + ARP_OFF_SENDER_MAC;
        net_lock_acquire();
        if (arp_pending && !arp_got &&
            memcmp(sender_ip, arp_want_ip, IP_ADDR_LEN) == 0) {
            memcpy(arp_got_mac, sender_mac, MAC_ADDR_LEN);
            arp_got = true;
        }
        net_lock_release();
        return;
    }
}

/* Handle an inbound IPv4 frame. The caller has verified len >= 14 and the
 * ethertype is IPv4. EVERY field read below is gated by a length guard that
 * PRECEDES it: 14+20 for the IPv4 header, IHL*4 >= 20 and 14+ihl_bytes <= len
 * and 14+total_length <= len before the ICMP payload, and 14+ihl_bytes+8 <= len
 * before the ICMP fields. Any inconsistency DROPS (returns) - never indexes
 * past len (T-08-07). */
static void net_handle_ipv4(const uint8_t *frame, uint32_t len)
{
    /* Need the fixed 20-byte IPv4 header before reading ver/IHL, proto, etc. */
    if (len < ETH_HDR_LEN + IP_HDR_MIN_LEN) {
        return;
    }
    const uint8_t *ip = frame + ETH_HDR_LEN;

    /* IHL is the low nibble of byte 0, in 32-bit words. Validate it spans at
     * least the 20-byte minimum AND fits inside the frame BEFORE using it to
     * locate the payload. */
    uint32_t ihl_bytes = (uint32_t)(ip[IP_OFF_VER_IHL] & 0x0f) * 4;
    if (ihl_bytes < IP_HDR_MIN_LEN) {
        return;
    }
    if (ETH_HDR_LEN + ihl_bytes > len) {
        return;
    }

    /* total_length covers the IPv4 header + payload. Reject a frame claiming
     * more than we received before trusting it to bound the ICMP message. */
    uint32_t total_length = rbe16(ip + IP_OFF_TOTAL_LEN);
    if (ETH_HDR_LEN + total_length > len) {
        return;
    }

    /* Only ICMP is in scope. */
    if (ip[IP_OFF_PROTO] != IP_PROTO_ICMP) {
        return;
    }

    /* The ICMP header starts after the (validated) IPv4 header. Require the
     * 8-byte ICMP header to fit BEFORE reading the type/id/seq. */
    if (ETH_HDR_LEN + ihl_bytes + ICMP_HDR_LEN > len) {
        return;
    }
    const uint8_t *icmp = ip + ihl_bytes;
    uint8_t type = icmp[ICMP_OFF_TYPE];

    if (type == ICMP_TYPE_ECHO_REQUEST) {
        /* Echo this back as a type-0 reply: swap the Ethernet src/dst MAC and
         * the IPv4 src/dst IP, set type 0, recompute the ICMP checksum (over
         * the ICMP span we received, bounded by total_length) then the IPv4
         * header checksum, and net_tx it (net_lock NOT held - net_tx self-
         * locks). The reply is bounded by total_length, already <= len. */
        uint32_t icmp_len = total_length - ihl_bytes;     /* ICMP hdr + payload */
        uint32_t reply_len = ETH_HDR_LEN + ihl_bytes + icmp_len;

        uint8_t reply[NET_RX_BUF_SIZE];
        if (reply_len > sizeof(reply)) {
            return;                                        /* will not fit - drop */
        }
        memcpy(reply, frame, reply_len);

        /* Swap Ethernet src/dst. */
        memcpy(reply + ETH_OFF_DST, frame + ETH_OFF_SRC, MAC_ADDR_LEN);
        memcpy(reply + ETH_OFF_SRC, frame + ETH_OFF_DST, MAC_ADDR_LEN);
        /* Swap IPv4 src/dst. */
        uint8_t *rip = reply + ETH_HDR_LEN;
        memcpy(rip + IP_OFF_SRC, ip + IP_OFF_DST, IP_ADDR_LEN);
        memcpy(rip + IP_OFF_DST, ip + IP_OFF_SRC, IP_ADDR_LEN);
        /* Type 0, recompute the ICMP checksum over its span. inet_csum returns a
         * host-order value; wbe16 writes it big-endian (network order) - so NO
         * htons (a second swap would corrupt the on-wire checksum and the peer
         * would drop the frame). */
        uint8_t *ricmp = rip + ihl_bytes;
        ricmp[ICMP_OFF_TYPE] = ICMP_TYPE_ECHO_REPLY;
        wbe16(ricmp + ICMP_OFF_CHECKSUM, 0);
        wbe16(ricmp + ICMP_OFF_CHECKSUM, inet_csum(ricmp, icmp_len));
        /* Recompute the IPv4 header checksum over its 20+ bytes (wbe16 writes it
         * big-endian; no htons). */
        wbe16(rip + IP_OFF_CHECKSUM, 0);
        wbe16(rip + IP_OFF_CHECKSUM, inet_csum(rip, ihl_bytes));

        net_tx(reply, reply_len);
        return;
    }

    if (type == ICMP_TYPE_ECHO_REPLY) {
        /* Match against the pending ping (id AND seq, T-08-09) under net_lock -
         * the brief read/compare/set, consistent with the queue cursors. No
         * net_tx here, so the lock is never held across a primitive. */
        uint16_t id  = rbe16(icmp + ICMP_OFF_ID);
        uint16_t seq = rbe16(icmp + ICMP_OFF_SEQ);
        net_lock_acquire();
        if (ping_pending && !ping_got && id == ping_id && seq == ping_seq) {
            ping_got = true;
        }
        net_lock_release();
        return;
    }
}

/* Parse one inbound Ethernet frame. The FIRST guard (len >= 14) precedes the
 * ethertype read; the per-protocol handlers guard their own fields. A malformed
 * or short frame is DROPPED here, never indexed past (T-08-07). */
void net_rx_dispatch(const uint8_t *frame, uint32_t len)
{
    if (len < ETH_HDR_LEN) {
        return;                          /* too short to hold an ethertype */
    }

    uint16_t ethertype = rbe16(frame + ETH_OFF_ETHERTYPE);

    if (ethertype == ETHERTYPE_ARP) {
        if (len < ARP_FRAME_LEN) {
            return;                      /* too short for a 28-byte ARP packet */
        }
        net_handle_arp(frame, len);
        return;
    }

    if (ethertype == ETHERTYPE_IPV4) {
        net_handle_ipv4(frame, len);     /* guards its own layers */
        return;
    }

    /* Any other ethertype is out of scope - drop. */
}

int arp_resolve(const uint8_t target_ip[4], uint8_t out_mac[6])
{
    if (!virtio_net_available()) {
        return -1;
    }

    uint8_t our_mac[MAC_ADDR_LEN];
    virtio_net_get_mac(our_mac);

    /* Arm the pending-ARP state under net_lock, then RELEASE before any net_tx
     * or net_rx_poll (those re-take net_lock - holding it across one would
     * self-deadlock the non-recursive lock). */
    net_lock_acquire();
    memcpy(arp_want_ip, target_ip, IP_ADDR_LEN);
    arp_got = false;
    arp_pending = true;
    net_lock_release();

    /* Build the broadcast opcode-1 request: dst ff..ff, target IP = target_ip,
     * target MAC zero, sender = our MAC/IP. */
    uint8_t req[ARP_FRAME_LEN];
    memset(req, 0, sizeof(req));
    memset(req + ETH_OFF_DST, 0xff, MAC_ADDR_LEN);
    memcpy(req + ETH_OFF_SRC, our_mac, MAC_ADDR_LEN);
    wbe16(req + ETH_OFF_ETHERTYPE, ETHERTYPE_ARP);
    uint8_t *arp = req + ETH_HDR_LEN;
    wbe16(arp + ARP_OFF_HTYPE, ARP_HTYPE_ETHERNET);
    wbe16(arp + ARP_OFF_PTYPE, ETHERTYPE_IPV4);
    arp[ARP_OFF_HLEN] = ARP_HLEN_ETHERNET;
    arp[ARP_OFF_PLEN] = ARP_PLEN_IPV4;
    wbe16(arp + ARP_OFF_OPCODE, ARP_OP_REQUEST);
    memcpy(arp + ARP_OFF_SENDER_MAC, our_mac, MAC_ADDR_LEN);
    memcpy(arp + ARP_OFF_SENDER_IP, our_ip, IP_ADDR_LEN);
    /* target MAC stays zero; target IP: */
    memcpy(arp + ARP_OFF_TARGET_IP, target_ip, IP_ADDR_LEN);

    net_tx(req, ARP_FRAME_LEN);          /* net_lock NOT held here */

    /* Bounded poll for the reply. net_rx_poll self-locks; net_rx_dispatch
     * caches the MAC + sets arp_got under net_lock on the matching opcode-2. */
    net_wait_t budget = net_wait_start();
    uint8_t rxbuf[NET_RX_BUF_SIZE];
    int result = -1;
    while (!net_wait_expired(&budget)) {
        net_lock_acquire();
        bool done = arp_got;
        if (done) {
            memcpy(out_mac, arp_got_mac, MAC_ADDR_LEN);
        }
        net_lock_release();
        if (done) {
            result = 0;
            break;
        }
        uint32_t rlen = 0;
        if (net_rx_poll(rxbuf, &rlen) == 1) {
            net_rx_dispatch(rxbuf, rlen);
        }
    }

    net_lock_acquire();
    arp_pending = false;
    net_lock_release();
    return result;
}

int icmp_send_echo(const uint8_t dst_ip[4], const uint8_t dst_mac[6],
                   uint16_t id, uint16_t seq)
{
    if (!virtio_net_available()) {
        return -1;
    }

    uint8_t our_mac[MAC_ADDR_LEN];
    virtio_net_get_mac(our_mac);

    uint32_t frame_len = ETH_HDR_LEN + IP_HDR_MIN_LEN + ICMP_HDR_LEN;  /* 42, no payload */
    uint8_t frame[ETH_HDR_LEN + IP_HDR_MIN_LEN + ICMP_HDR_LEN];
    memset(frame, 0, sizeof(frame));

    /* Ethernet: dst = next-hop MAC, src = our MAC, type IPv4. */
    memcpy(frame + ETH_OFF_DST, dst_mac, MAC_ADDR_LEN);
    memcpy(frame + ETH_OFF_SRC, our_mac, MAC_ADDR_LEN);
    wbe16(frame + ETH_OFF_ETHERTYPE, ETHERTYPE_IPV4);

    /* IPv4: v4/IHL 5, total length 20+8, TTL 64, proto ICMP, src/dst, csum. */
    uint8_t *ip = frame + ETH_HDR_LEN;
    ip[IP_OFF_VER_IHL] = IP_VER_IHL_DEFAULT;
    wbe16(ip + IP_OFF_TOTAL_LEN, (uint16_t)(IP_HDR_MIN_LEN + ICMP_HDR_LEN));
    ip[IP_OFF_TTL]   = IP_TTL_DEFAULT;
    ip[IP_OFF_PROTO] = IP_PROTO_ICMP;
    memcpy(ip + IP_OFF_SRC, our_ip, IP_ADDR_LEN);
    memcpy(ip + IP_OFF_DST, dst_ip, IP_ADDR_LEN);
    /* inet_csum returns a host-order value; wbe16 writes it big-endian (network
     * order). NO htons - a second swap corrupts the on-wire checksum and slirp
     * silently drops the frame (no echo reply). */
    wbe16(ip + IP_OFF_CHECKSUM, 0);
    wbe16(ip + IP_OFF_CHECKSUM, inet_csum(ip, IP_HDR_MIN_LEN));

    /* ICMP echo request: type 8, code 0, id, seq, csum over the 8-byte header. */
    uint8_t *icmp = ip + IP_HDR_MIN_LEN;
    icmp[ICMP_OFF_TYPE] = ICMP_TYPE_ECHO_REQUEST;
    icmp[ICMP_OFF_CODE] = 0;
    wbe16(icmp + ICMP_OFF_ID, id);
    wbe16(icmp + ICMP_OFF_SEQ, seq);
    wbe16(icmp + ICMP_OFF_CHECKSUM, 0);
    wbe16(icmp + ICMP_OFF_CHECKSUM, inet_csum(icmp, ICMP_HDR_LEN));

    return net_tx(frame, frame_len);
}

int net_ping(const uint8_t dst_ip[4])
{
    if (!virtio_net_available()) {
        klog_warn("ping: no network device");
        return -1;
    }

    /* Resolve the next hop (the gateway is the next hop for any 10.0.2.x; only
     * 10.0.2.2 is in scope). On ARP timeout this is a real failure, not a hang. */
    uint8_t gw_ip[IP_ADDR_LEN] = { GW_IP_0, GW_IP_1, GW_IP_2, GW_IP_3 };
    uint8_t next_hop_mac[MAC_ADDR_LEN];
    if (arp_resolve(gw_ip, next_hop_mac) != 0) {
        klog_warn("ping: ARP resolve failed");
        return -1;
    }

    /* Arm the pending-ping state under net_lock, then RELEASE before the send
     * and the poll loop (icmp_send_echo and net_rx_poll re-take net_lock). */
    uint16_t id  = 0x1234;
    uint16_t seq = 1;
    net_lock_acquire();
    ping_id  = id;
    ping_seq = seq;
    ping_got = false;
    ping_pending = true;
    net_lock_release();

    if (icmp_send_echo(dst_ip, next_hop_mac, id, seq) != 0) {
        net_lock_acquire();
        ping_pending = false;
        net_lock_release();
        klog_warn("ping: send failed");
        return -1;
    }

    /* Bounded poll for the matching type-0 reply. net_rx_dispatch sets ping_got
     * under net_lock on a matched id+seq; we only read the flag under the lock. */
    net_wait_t budget = net_wait_start();
    uint8_t rxbuf[NET_RX_BUF_SIZE];
    int result = -1;
    while (!net_wait_expired(&budget)) {
        net_lock_acquire();
        bool done = ping_got;
        net_lock_release();
        if (done) {
            result = 0;
            break;
        }
        uint32_t rlen = 0;
        if (net_rx_poll(rxbuf, &rlen) == 1) {
            net_rx_dispatch(rxbuf, rlen);
        }
    }

    net_lock_acquire();
    ping_pending = false;
    net_lock_release();
    return result;
}

/* ============================================================================
 * End of net.c
 * ============================================================================ */

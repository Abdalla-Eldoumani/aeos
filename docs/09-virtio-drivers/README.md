# Section 09: VirtIO Drivers

## Overview

This section implements VirtIO device drivers for AEOS, enabling graphics display and input device support in QEMU. The drivers use the VirtIO MMIO transport layer. The active path on QEMU virt is the legacy (v1) protocol, which is what is stable there without modern feature negotiation; a modern (v2) branch exists in the queue setup but is not the exercised path. VirtIO provides a standardized interface for virtual hardware that is efficient and well documented.

## Components

### VirtIO GPU Driver (virtio_gpu.c)
- **Location**: `src/drivers/virtio_gpu.c`
- **Purpose**: Display output via VirtIO GPU device
- **Features**:
  - Device detection and initialization
  - 2D resource management
  - Framebuffer attachment
  - Scanout configuration
  - Display updates (transfer + flush)
  - Legacy (v1) protocol on QEMU virt; allocates a host resource, attaches the framebuffer, flushes on update

### VirtIO Input Driver (virtio_input.c)
- **Location**: `src/drivers/virtio_input.c`
- **Purpose**: Keyboard and pointer input
- **Features**:
  - Automatic device detection
  - Pointer movement (both `EV_REL` relative deltas and `EV_ABS` absolute, scaled from 0-32767 to the framebuffer) and button handling. `run-ramfb` attaches `virtio-tablet-device` (absolute), so the guest cursor tracks the host pointer with no drift (BUG-01); the relative `EV_REL` path remains for a `virtio-mouse-device`
  - Keyboard scancode translation through a 128-entry table (see below)
  - Pushes every decoded event into the `kernel/event.c` queue via `event_push`
  - No per-click console logging: the polling loop is silent on the normal path (the per-event mouse-button log line was removed in the BUG-10 cleanup)

### Keyboard scancode translation

`virtio_input.c` carries a `scancode_to_keycode[128]` table that maps two
families of codes to AEOS keycodes:

- **Legacy AT-style positions**, e.g. position 72 maps to `KEY_UP`.
- **The Linux input-event-codes navigation cluster (102-111)**: `KEY_HOME`,
  `KEY_UP`, `KEY_PAGE_UP`, `KEY_LEFT`, `KEY_RIGHT`, `KEY_END`, `KEY_DOWN`,
  `KEY_PAGE_DOWN`, `KEY_INSERT`, `KEY_DELETE`, which is what QEMU's
  virtio-keyboard emits directly.

This second family is what makes the terminal scrollback Page Up / Page Down
and the editor arrow navigation work. The keyboard handler bounds `ev->code`
against the table size before the lookup and drops any code that maps to
`KEY_NONE`.

The drivers stay isolated: they never call into `wm` or `desktop`. Decoded
events go up the stack through `event_push`, and the window manager pulls them
off the queue on its own loop.

### VirtIO Net Driver (virtio_net.c)
- **Location**: `src/drivers/virtio_net.c`
- **Purpose**: Network frame transmit/receive over the QEMU `virtio-net-device` (slirp backend)
- **Features**:
  - Scans for `device_id == 1` (VIRTIO_ID_NETWORK); never hardcodes a slot (the slot shifts with the `-device` set)
  - Legacy v1 init: the device reports version 1 and offers no `VIRTIO_F_VERSION_1`, so it uses the `QUEUE_ALIGN`/`QUEUE_PFN` path, not the v2 path
  - Negotiates `VIRTIO_NET_F_MAC` only. That forces the 10-byte `virtio_net_hdr` and single-descriptor receive (accepting MRG_RXBUF would make the header 12 bytes and let a frame span descriptors)
  - Two virtqueues: RX = queue 0 (write-only ~2KB buffers, posted before the device is notified), TX = queue 1 (device reads, posted on demand)
  - Reads the 6-byte MAC from device config offset 0x100 and prints it at probe: `virtio-net: MAC 52:54:00:12:34:56`
  - Poll-driven `net_tx` / `net_rx_poll` - there is no interrupt path. The `TEST=1` kernel has no GIC or timer, so an IRQ-driven driver could not work in the test build; the poll path runs identically in both builds
  - A file-static spinlock guards the RX/TX queue indices (the shared state), reusing the Phase 7 `spinlock.h` primitive
  - No-op when absent: if no net device is found the probe returns -1 and the kernel boots on (logs `virtio-net not found`)

The Ethernet/ARP/IPv4/ICMP stack that sits on top of `net_tx`/`net_rx_poll` lives
in `src/net/` (see "The network stack" below); this driver only moves bytes. The
driver exposes `net_lock_acquire`/`net_lock_release` so the stack can update its
pending-ping state under the SAME lock the queue cursors use.

### The network stack (src/net/net.c)

- **Location**: `src/net/net.c`, interface in `include/aeos/net.h`
- **Purpose**: the minimal Ethernet/ARP/IPv4/ICMP framing on top of the driver's
  `net_tx`/`net_rx_poll`. No TCP/UDP/DHCP/routing.
- **Byte order**: big-endian on the wire, little-endian kernel. Every multi-byte
  field is written/read big-endian byte-at-a-time (the `wbe16`/`rbe16` helpers,
  or the `htons`/`htonl` helpers in `net.h`) - never a packed-struct store.
- **Integer-only**: no FP/SIMD anywhere (the build is `-mgeneral-regs-only`).
- **Static config** (slirp defaults): our IP `10.0.2.15`, gateway `10.0.2.2`.
- **`inet_csum`**: the one's-complement Internet checksum, used for both the
  20-byte IPv4 header and the ICMP message. Pure (no I/O).
- **`arp_build_reply`**: a pure builder - given a valid inbound ARP request for
  our IP it writes a 42-byte opcode-2 reply; it returns 0 without writing if the
  input is too short, not a request, or for some other IP.
- **`net_rx_dispatch`**: the bounds-safe parser. It bounds-checks at every layer
  (`len >= 14` before the ethertype; `len >= 42` for ARP; for IPv4 the IHL and
  total-length are validated against `len` before the ICMP payload is indexed)
  and DROPS any malformed/short/inconsistent frame - it never reads past `len`
  or faults. It answers an ARP request for our IP, answers an ICMP echo request
  with a type-0 reply (both checksums recomputed), and matches an ICMP echo
  reply against the pending ping.
- **`arp_resolve` / `net_ping`**: both bounded - a `timer_get_uptime_ms()`
  deadline in production, a spin-count under `TEST_BUILD` (no timer there). They
  return -1 on the bounded-wait expiry, never an infinite loop.
- **Lock discipline**: the pending-ping state is guarded by the driver's
  `net_lock` (criterion 4), not a second lock. `net_lock` is non-recursive, so
  no flow holds it across a `net_tx`/`net_rx_poll` call (those re-take it and
  would self-deadlock). `net_rx_dispatch` locks only the brief reply-match; the
  ARP/echo sends and the ping poll loop run with the lock released.

Not yet wired into the boot path or the shell (that is a later plan); the code
links into both kernels but is not called on boot.

### virtio_net_hdr

With `VIRTIO_NET_F_MAC` the only accepted feature, the header is the legacy 10-byte
layout, prepended to every transmitted frame and present at the front of every
received frame:

```c
struct virtio_net_hdr {   /* 10 bytes */
    uint8_t  flags;       /* 0 for plain frames */
    uint8_t  gso_type;    /* 0 (no GSO) */
    uint16_t hdr_len;     /* 0 */
    uint16_t gso_size;    /* 0 */
    uint16_t csum_start;  /* 0 (no checksum offload) */
    uint16_t csum_offset; /* 0 */
} __attribute__((packed));
```

On transmit the driver zeroes the 10 bytes and copies the frame after them in the
same buffer. On receive it skips 10 bytes (the frame is at `buf + 10`, length
`used.len - 10`). `net_rx_poll` checks `used.len >= 10` before subtracting and caps
the copy at `NET_RX_BUF_SIZE` so a short or oversized inbound frame is dropped or
truncated, never read or written past the buffer.

### Framebuffer Driver (framebuffer.c)
- **Location**: `src/drivers/framebuffer.c`
- **Purpose**: Graphics primitives
- **Features**:
  - Pixel drawing
  - Rectangle filling and outlining
  - Line drawing (Bresenham's algorithm)
  - Two bitmap fonts: 8x8 (`fb_putchar`/`fb_puts`) and 8x16 (`fb_putchar_large`/`fb_puts_large`)
  - All primitives accept signed coordinates and clip to the framebuffer internally

### PL031 RTC Driver (pl031.c)

Not a VirtIO device, but a plain MMIO driver that lives alongside the others in
`src/drivers/`.

- **Location**: `src/drivers/pl031.c`, interface in `include/aeos/pl031.h`
- **Purpose**: read the wall-clock time for the taskbar clock (FEAT-06)
- **Device**: the PrimeCell PL031 RTC at base `0x09010000` (DTB-confirmed
  `compatible = "arm,pl031"`). Its data register RTC_DR (offset 0x00) holds the
  current time as wall-clock seconds since the Unix epoch, fed from the QEMU
  host clock - the only register read. `run-ramfb` runs QEMU with
  `-rtc base=localtime`, so RTC_DR carries the host's LOCAL time (the host
  timezone, which handles DST), not UTC. There is no IP/GPS geolocation on a
  bare-metal kernel; the host clock is the automatic local-time source.
  QEMU keeps the RTC running by default, so there is no enable write.
- **MMIO access**: a single `volatile uint32_t` read, the same pattern as
  `uart.c`. The base sits inside the kernel's identity-mapped Device-nGnRnE MMIO
  block (`vmm.c` maps `0x00000000-0x3FFFFFFF` as one Device block), so there is
  no separate mapping. It is a stateless global read; under `-smp` any core reads
  the same value, so no lock is taken.
- **H:M:S formatter**: `pl031_format_hms` does an integer-only breakdown
  (`secs % 86400` into hours/minutes/seconds, wrapping at midnight) and
  formats `"HH:MM:SS"` via `snprintf("%02u:%02u:%02u", ...)`. Integer-only so it
  is safe under `-mgeneral-regs-only`. It is timezone-agnostic - it formats
  whatever epoch seconds it is given, which is local time under
  `-rtc base=localtime`. No date (the date is out of scope).
- **Boot proof**: `pl031_init` runs on the boot path (after the virtio-net
  probe, before the EL0 demos) and logs `PL031: <secs> seconds, HH:MM:SS local`
  on serial so the raw value is visible. A register read has no failure path that
  hangs, so it log-and-continues like the other device probes.
- **Taskbar**: `desktop_draw_taskbar` reads the RTC each frame via
  `pl031_format_hms(pl031_now_seconds(), ...)`, replacing the old uptime counter,
  so the clock shows the real time of day. The 30 FPS WM redraw refreshes it.

## VirtIO MMIO Transport

VirtIO devices on QEMU's ARM virt board are exposed via MMIO (Memory-Mapped I/O). The MMIO region provides registers for device discovery, configuration, and virtqueue management.

### MMIO Address Space

| Slot | Base Address | Device Type |
|------|--------------|-------------|
| 0-31 | 0x0a000000 + (slot * 0x200) | Various |

Typical device assignments:
- Slot 31: VirtIO GPU
- Slot 30: VirtIO Keyboard
- Slot 29: VirtIO Mouse

### MMIO Registers

| Offset | Name | Description |
|--------|------|-------------|
| 0x000 | MAGIC | Magic value (0x74726976 = "virt") |
| 0x004 | VERSION | Device version (1 = legacy, 2 = modern) |
| 0x008 | DEVICE_ID | Device type (16 = GPU, 18 = Input) |
| 0x00C | VENDOR_ID | Vendor identifier |
| 0x010 | DEVICE_FEATURES | Device feature bits (low 32) |
| 0x020 | DRIVER_FEATURES | Driver accepted features (low 32) |
| 0x028 | GUEST_PAGE_SIZE | Page size (legacy only) |
| 0x030 | QUEUE_SEL | Queue selector |
| 0x034 | QUEUE_NUM_MAX | Maximum queue size |
| 0x038 | QUEUE_NUM | Configured queue size |
| 0x040 | QUEUE_PFN | Queue page frame number (legacy) |
| 0x044 | QUEUE_READY | Queue ready (modern) |
| 0x050 | QUEUE_NOTIFY | Queue notification |
| 0x060 | INTERRUPT_STATUS | Interrupt status |
| 0x064 | INTERRUPT_ACK | Interrupt acknowledgment |
| 0x070 | STATUS | Device status |

## Virtqueue Architecture

Virtqueues are the communication mechanism between driver and device. Each queue consists of three parts:

### Descriptor Table
Array of buffer descriptors:
```c
typedef struct {
    uint64_t addr;   /* Buffer physical address */
    uint32_t len;    /* Buffer length */
    uint16_t flags;  /* NEXT, WRITE, INDIRECT */
    uint16_t next;   /* Next descriptor index */
} virtq_desc_t;
```

### Available Ring
Driver-to-device buffer notifications:
```c
typedef struct {
    uint16_t flags;
    uint16_t idx;       /* Next write index */
    uint16_t ring[];    /* Descriptor indices */
} virtq_avail_t;
```

### Used Ring
Device-to-driver completion notifications:
```c
typedef struct {
    uint16_t flags;
    uint16_t idx;              /* Next write index */
    virtq_used_elem_t ring[];  /* Completed descriptors */
} virtq_used_t;
```

## Device IDs

| ID | Device Type |
|----|-------------|
| 1  | Network |
| 16 | GPU |
| 18 | Input |

## GPU Commands

| Command | Description |
|---------|-------------|
| `RESOURCE_CREATE_2D` | Create a 2D resource |
| `RESOURCE_ATTACH_BACKING` | Attach memory to resource |
| `SET_SCANOUT` | Connect resource to display |
| `TRANSFER_TO_HOST_2D` | Copy data to host |
| `RESOURCE_FLUSH` | Update display |

## Input Event Types

| Type | Name | Description |
|------|------|-------------|
| 0 | EV_SYN | Synchronization |
| 1 | EV_KEY | Key/button press |
| 2 | EV_REL | Relative movement |
| 3 | EV_ABS | Absolute position |

## API Reference

### VirtIO GPU

```c
/* Initialize VirtIO GPU driver */
int virtio_gpu_init(void);

/* Create 2D resource */
uint32_t virtio_gpu_create_resource(uint32_t width, uint32_t height, uint32_t format);

/* Set display scanout */
int virtio_gpu_set_scanout(uint32_t scanout_id, uint32_t resource_id,
                           uint32_t width, uint32_t height);

/* Transfer data to host */
int virtio_gpu_transfer_to_host(uint32_t resource_id, uint32_t x, uint32_t y,
                                uint32_t width, uint32_t height);

/* Flush display region */
int virtio_gpu_flush(uint32_t resource_id, uint32_t x, uint32_t y,
                     uint32_t width, uint32_t height);

/* Complete display update (setup + transfer + flush) */
int virtio_gpu_update_display(void);
```

### VirtIO Input

```c
/* Initialize VirtIO input devices */
int virtio_input_init(void);

/* Poll input devices for events */
void virtio_input_poll(void);

/* Check device availability */
bool virtio_keyboard_available(void);
bool virtio_mouse_available(void);
```

### VirtIO Net

```c
/* Probe the net device: scan, legacy MAC-only init, RX/TX queues, print MAC.
 * Returns 0 on success, -1 when no net device is present (clean no-op). */
int virtio_net_init(void);

/* Transmit one Ethernet frame (the driver prepends the 10-byte header).
 * Returns 0 on completion, -1 on timeout or when no device is present. */
int net_tx(const uint8_t *frame, uint32_t len);

/* Poll for one received frame; never blocks. Writes the frame (header skipped)
 * into out, capped at NET_RX_BUF_SIZE. Returns 1 on a frame, 0 if nothing is
 * ready, -1 when no device is present. */
int net_rx_poll(uint8_t *out, uint32_t *len);

/* Whether a net device was found and initialized. */
bool virtio_net_available(void);

/* Copy the 6-byte device MAC into out_mac. */
void virtio_net_get_mac(uint8_t out_mac[6]);

/* Acquire/release the driver's net_lock - the SAME lock net_tx/net_rx_poll use.
 * The src/net/ stack guards its pending-ping state through this (criterion 4).
 * Non-recursive: do not call net_tx/net_rx_poll while holding it. */
void net_lock_acquire(void);
void net_lock_release(void);
```

### Network stack (src/net/net.c)

```c
/* One's-complement Internet checksum over data[0..len). Pure. Used for both the
 * IPv4 header and the ICMP message. */
uint16_t inet_csum(const uint8_t *data, uint32_t len);

/* Pure ARP-reply builder: writes a 42-byte opcode-2 reply into out for a valid
 * inbound request for our_ip; returns 42, or 0 without writing on a mismatch. */
int arp_build_reply(const uint8_t *req, uint32_t req_len,
                    const uint8_t our_mac[6], const uint8_t our_ip[4],
                    uint8_t *out);

/* Bounds-safe parse of one inbound Ethernet frame: answer ARP for our IP,
 * answer an ICMP echo request, match an echo reply. Drops malformed frames. */
void net_rx_dispatch(const uint8_t *frame, uint32_t len);

/* Broadcast an ARP request and bounded-poll for the reply (0 + out_mac, or -1). */
int arp_resolve(const uint8_t target_ip[4], uint8_t out_mac[6]);

/* Build + send an ICMP echo request (type 8) with correct checksums. */
int icmp_send_echo(const uint8_t dst_ip[4], const uint8_t dst_mac[6],
                   uint16_t id, uint16_t seq);

/* Bounded ping driver: resolve, send, poll for the matching reply (0 or -1). */
int net_ping(const uint8_t dst_ip[4]);
```

Checksums go on the wire big-endian: `inet_csum` returns a host-order value and
the `wbe16` store writes it big-endian, so the store is `wbe16(p, inet_csum(...))`
with no `htons` (a second swap corrupts the on-wire checksum and the peer drops
the frame).

The interactive surface is the `ping <ip>` shell command (a dotted-quad parse,
no DNS; default target the slirp gateway 10.0.2.2), bounded by `net_ping` so it
never hangs the prompt.

After the probe, `kernel_main` runs a one-shot boot-path `ping 10.0.2.2` demo
(mirroring the `/hello` EL0 demo's log-and-continue discipline): on a reply it
logs `ping 10.0.2.2: reply` (the criterion-3 serial proof alongside the MAC),
and on a timeout or an absent device it warns once and continues - boot always
reaches the window manager loop. It is gated on the device being present and
runs exactly once, so a normal boot is not spammy.

### Framebuffer

```c
/* Initialize framebuffer */
int fb_init(uint32_t width, uint32_t height, uint32_t bpp);

/* Get framebuffer info */
fb_info_t *fb_get_info(void);

/* Drawing primitives */
void fb_putpixel(int32_t x, int32_t y, uint32_t color);
uint32_t fb_getpixel(int32_t x, int32_t y);
void fb_fill_rect(int32_t x, int32_t y, uint32_t w, uint32_t h, uint32_t color);
void fb_draw_rect(int32_t x, int32_t y, uint32_t w, uint32_t h, uint32_t color);
void fb_draw_line(int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint32_t color);
void fb_putchar(int32_t x, int32_t y, char c, uint32_t fg, uint32_t bg);          /* 8x8 */
void fb_puts(int32_t x, int32_t y, const char *s, uint32_t fg, uint32_t bg);      /* 8x8 */
void fb_putchar_large(int32_t x, int32_t y, char c, uint32_t fg, uint32_t bg);    /* 8x16 */
void fb_puts_large(int32_t x, int32_t y, const char *s, uint32_t fg, uint32_t bg);/* 8x16 */
void fb_clear(uint32_t color);
```

## Device Detection Order

**Critical**: QEMU creates VirtIO input devices in a specific order:
1. Mouse device (lower slot number)
2. Keyboard device (higher slot number)

The driver scans slots from low to high and assigns:
- First input device found = mouse
- Second input device found = keyboard

## Pixel Format

The framebuffer uses XRGB8888 format:
```
Byte 3 | Byte 2 | Byte 1 | Byte 0
  X    |   R    |   G    |   B
```

Color values are 32-bit: `0xAARRGGBB` (alpha ignored)

## Memory Requirements

| Component | Size |
|-----------|------|
| Framebuffer | ~1.2 MB (640x480x4) |
| GPU virtqueue | ~8 KB |
| Input virtqueues | ~16 KB (2 devices) |
| Event buffers | ~4 KB |

## Usage Example

### Initialize Graphics
```c
/* In kernel main */
fb_init(640, 480, 32);        /* Initialize framebuffer */
virtio_gpu_init();            /* Initialize GPU driver */

/* Draw something */
fb_clear(0xFF000000);         /* Black background */
fb_fill_rect(100, 100, 200, 150, 0xFF0000FF);  /* Blue rectangle */

/* Update display */
virtio_gpu_update_display();
```

### Poll Input
```c
/* In main loop */
while (1) {
    virtio_input_poll();  /* Check for events */

    event_t event;
    while (event_pop(&event)) {
        /* Process event */
    }
}
```

## Known Limitations

- GPU: Only 2D resources supported (no 3D)
- GPU: Single scanout (one display)
- Input: No tablet absolute mode scrolling
- Input: No multi-touch support
- Framebuffer: No hardware acceleration
- Fonts: only 8x8 and 8x16 bitmap fonts (no scalable / antialiased text)

## Debugging

### Check Device Detection
```c
/* Add to virtio_gpu_init() */
klog_debug("VirtIO device at slot %u: type=%u", slot, device_id);
```

### Verify Queue Setup
```c
/* After queue init */
klog_debug("Queue: desc=%p avail=%p used=%p", vq->desc, vq->avail, vq->used);
klog_debug("Queue PFN=0x%x", pfn);
```

### Monitor Input Events
```c
/* In process_input_events() */
klog_debug("Event: type=%u code=%u value=%d", ev->type, ev->code, ev->value);
```

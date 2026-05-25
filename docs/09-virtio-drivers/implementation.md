# VirtIO Drivers - Implementation Details

## VirtIO Device Initialization

### Status Flags

```c
#define VIRTIO_STATUS_ACKNOWLEDGE  1   /* OS found device */
#define VIRTIO_STATUS_DRIVER       2   /* OS knows how to drive device */
#define VIRTIO_STATUS_FEATURES_OK  8   /* Feature negotiation complete */
#define VIRTIO_STATUS_DRIVER_OK    4   /* Driver ready */
#define VIRTIO_STATUS_FAILED       128 /* Something went wrong */
```

### Initialization Sequence

```c
int virtio_gpu_init(void)
{
    /* 1. Reset device */
    virtio_mmio_write32(mmio, VIRTIO_MMIO_STATUS, 0);

    /* 2. Set ACKNOWLEDGE */
    virtio_mmio_write32(mmio, VIRTIO_MMIO_STATUS, VIRTIO_STATUS_ACKNOWLEDGE);

    /* 3. Set DRIVER */
    status = virtio_mmio_read32(mmio, VIRTIO_MMIO_STATUS);
    virtio_mmio_write32(mmio, VIRTIO_MMIO_STATUS, status | VIRTIO_STATUS_DRIVER);

    /* 4. For legacy, set guest page size */
    if (version == 1) {
        virtio_mmio_write32(mmio, VIRTIO_MMIO_GUEST_PAGE_SIZE, 4096);
    }

    /* 5. Read and negotiate features */
    virtio_mmio_write32(mmio, VIRTIO_MMIO_DEVICE_FEATURES_SEL, 0);
    features_lo = virtio_mmio_read32(mmio, VIRTIO_MMIO_DEVICE_FEATURES);

    /* 6. Set FEATURES_OK */
    status = virtio_mmio_read32(mmio, VIRTIO_MMIO_STATUS);
    virtio_mmio_write32(mmio, VIRTIO_MMIO_STATUS, status | VIRTIO_STATUS_FEATURES_OK);

    /* 7. Verify FEATURES_OK */
    status = virtio_mmio_read32(mmio, VIRTIO_MMIO_STATUS);
    if (!(status & VIRTIO_STATUS_FEATURES_OK)) {
        return -1;  /* Device rejected features */
    }

    /* 8. Initialize virtqueues */
    virtqueue_init(&ctrl_vq, mmio, 0);

    /* 9. Set DRIVER_OK */
    status = virtio_mmio_read32(mmio, VIRTIO_MMIO_STATUS);
    virtio_mmio_write32(mmio, VIRTIO_MMIO_STATUS, status | VIRTIO_STATUS_DRIVER_OK);

    return 0;
}
```

## Virtqueue Setup

### Memory Layout

For legacy VirtIO (v1), the queue must be laid out as:
```
+------------------+ <-- Page aligned
| Descriptor Table |    (16 bytes * queue_size)
+------------------+
| Available Ring   |    (6 + 2 * queue_size bytes)
+------------------+ <-- Page aligned
| Used Ring        |    (6 + 8 * queue_size bytes)
+------------------+
```

### Queue Initialization

```c
static int virtqueue_init(virtqueue_t *vq, volatile uint32_t *mmio, uint32_t queue_idx)
{
    /* Select queue */
    virtio_mmio_write32(mmio, VIRTIO_MMIO_QUEUE_SEL, queue_idx);

    /* Get maximum queue size */
    queue_size = virtio_mmio_read32(mmio, VIRTIO_MMIO_QUEUE_NUM_MAX);

    /* Calculate sizes */
    desc_size = sizeof(virtq_desc_t) * queue_size;
    avail_size = sizeof(uint16_t) * (3 + queue_size);
    used_size = sizeof(uint16_t) * 3 + sizeof(virtq_used_elem_t) * queue_size;

    /* Page-align used ring for legacy */
    size_t used_offset = (desc_size + avail_size + 4095) & ~4095ULL;

    /* Allocate page-aligned memory */
    uint8_t *raw_mem = kmalloc(total_size);
    queue_mem = (uint8_t *)((uint64_t)raw_mem + 4095) & ~4095ULL;

    /* Set up structures */
    vq->desc = (virtq_desc_t *)queue_mem;
    vq->avail = (virtq_avail_t *)(queue_mem + desc_size);
    vq->used = (virtq_used_t *)(queue_mem + used_offset);

    /* Configure queue */
    virtio_mmio_write32(mmio, VIRTIO_MMIO_QUEUE_NUM, queue_size);

    if (version == 1) {
        /* Legacy: Use page frame number */
        virtio_mmio_write32(mmio, VIRTIO_MMIO_QUEUE_ALIGN, 4096);
        virtio_mmio_write32(mmio, VIRTIO_MMIO_QUEUE_PFN, desc_addr >> 12);
    } else {
        /* Modern: Use separate addresses */
        virtio_mmio_write32(mmio, VIRTIO_MMIO_QUEUE_DESC_LOW, desc_addr);
        virtio_mmio_write32(mmio, VIRTIO_MMIO_QUEUE_AVAIL_LOW, avail_addr);
        virtio_mmio_write32(mmio, VIRTIO_MMIO_QUEUE_USED_LOW, used_addr);
        virtio_mmio_write32(mmio, VIRTIO_MMIO_QUEUE_READY, 1);
    }

    return 0;
}
```

## GPU Command Submission

### Descriptor Chain

GPU commands use chained descriptors:
1. Command buffer (device reads)
2. Response buffer (device writes)

```c
static int virtio_gpu_submit_cmd(void *cmd, size_t cmd_len, void *resp, size_t resp_len)
{
    /* Allocate two descriptors */
    cmd_desc = ctrl_vq.free_head;
    ctrl_vq.free_head = ctrl_vq.desc[cmd_desc].next;

    resp_desc = ctrl_vq.free_head;
    ctrl_vq.free_head = ctrl_vq.desc[resp_desc].next;

    /* Set up command descriptor (device reads from this) */
    ctrl_vq.desc[cmd_desc].addr = (uint64_t)cmd;
    ctrl_vq.desc[cmd_desc].len = cmd_len;
    ctrl_vq.desc[cmd_desc].flags = VIRTQ_DESC_F_NEXT;
    ctrl_vq.desc[cmd_desc].next = resp_desc;

    /* Set up response descriptor (device writes to this) */
    ctrl_vq.desc[resp_desc].addr = (uint64_t)resp;
    ctrl_vq.desc[resp_desc].len = resp_len;
    ctrl_vq.desc[resp_desc].flags = VIRTQ_DESC_F_WRITE;

    /* Add to available ring */
    ctrl_vq.avail->ring[ctrl_vq.avail->idx % VIRTQ_SIZE] = cmd_desc;

    /* Memory barrier before updating index */
    __asm__ volatile("dmb ish" ::: "memory");

    ctrl_vq.avail->idx++;

    /* Another barrier after updating index */
    __asm__ volatile("dmb ish" ::: "memory");

    /* Notify device */
    virtio_mmio_write32(mmio, VIRTIO_MMIO_QUEUE_NOTIFY, 0);

    /* Wait for response */
    while (ctrl_vq.used->idx == ctrl_vq.last_used_idx) {
        __asm__ volatile("dmb ish" ::: "memory");
    }

    ctrl_vq.last_used_idx++;

    return 0;
}
```

### GPU Command Structures

```c
/* Create 2D resource */
typedef struct {
    virtio_gpu_ctrl_hdr_t hdr;  /* type = RESOURCE_CREATE_2D */
    uint32_t resource_id;
    uint32_t format;             /* VIRTIO_GPU_FORMAT_X8R8G8B8_UNORM */
    uint32_t width;
    uint32_t height;
} virtio_gpu_resource_create_2d_t;

/* Attach backing */
typedef struct {
    virtio_gpu_ctrl_hdr_t hdr;  /* type = RESOURCE_ATTACH_BACKING */
    uint32_t resource_id;
    uint32_t nr_entries;
} virtio_gpu_resource_attach_backing_t;

/* Memory entry for backing */
typedef struct {
    uint64_t addr;
    uint32_t length;
    uint32_t padding;
} virtio_gpu_mem_entry_t;
```

### Display Update Sequence

```c
int virtio_gpu_update_display(void)
{
    /* Check if already configured */
    if (gpu_dev.display_resource_id != 0) {
        /* Just transfer and flush */
        virtio_gpu_transfer_to_host(resource_id, 0, 0, width, height);
        virtio_gpu_flush(resource_id, 0, 0, width, height);
        return 0;
    }

    /* First-time setup */
    /* 1. Create 2D resource */
    resource_id = virtio_gpu_create_resource(width, height, format);

    /* 2. Attach framebuffer memory */
    virtio_gpu_attach_backing(resource_id, fb->base, fb->pitch * fb->height);

    /* 3. Set scanout */
    virtio_gpu_set_scanout(0, resource_id, width, height);

    /* 4. Transfer to host */
    virtio_gpu_transfer_to_host(resource_id, 0, 0, width, height);

    /* 5. Flush display */
    virtio_gpu_flush(resource_id, 0, 0, width, height);

    /* Save for future updates */
    gpu_dev.display_resource_id = resource_id;

    return 0;
}
```

## Input Driver Implementation

### Device Detection

```c
int virtio_input_init(void)
{
    /* Scan all VirtIO MMIO slots */
    for (i = 0; i < VIRTIO_MMIO_COUNT; i++) {
        addr = VIRTIO_MMIO_BASE + (i * VIRTIO_MMIO_SIZE);
        mmio = (volatile uint32_t *)addr;

        /* Check magic */
        if (virtio_mmio_read32(mmio, VIRTIO_MMIO_MAGIC) != 0x74726976) {
            continue;
        }

        device_id = virtio_mmio_read32(mmio, VIRTIO_MMIO_DEVICE_ID);

        if (device_id == VIRTIO_ID_INPUT) {
            /* QEMU creates mouse first, then keyboard */
            if (!found_mouse) {
                mouse_slot = i;
                found_mouse = true;
            } else if (!found_keyboard) {
                keyboard_slot = i;
                found_keyboard = true;
            }
        }
    }

    /* Initialize devices at found slots */
    if (mouse_slot >= 0) {
        init_input_device(mouse_addr, &mouse_dev, &mouse_eventq);
    }
    if (keyboard_slot >= 0) {
        init_input_device(keyboard_addr, &keyboard_dev, &keyboard_eventq);
    }

    return 0;
}
```

### Input Queue Setup

Input devices use a single queue where the driver provides buffers for the device to fill with events.

```c
static int input_virtqueue_init(input_virtqueue_t *vq, volatile uint32_t *mmio, uint32_t queue_idx)
{
    /* Allocate queue memory + event buffers in same region */
    size_t event_buf_size = sizeof(virtio_input_event_t) * queue_size;
    size_t total_size = used_offset + used_size + event_buf_size + 8192;
    raw_mem = kmalloc(total_size);

    /* Event buffers placed after used ring */
    local_event_buf = (virtio_input_event_t *)(queue_mem + used_offset + used_size);
    vq->events = local_event_buf;

    /* Initialize descriptors pointing to event buffers */
    for (i = 0; i < queue_size; i++) {
        vq->desc[i].addr = (uint64_t)&local_event_buf[i];
        vq->desc[i].len = sizeof(virtio_input_event_t);
        vq->desc[i].flags = VIRTQ_DESC_F_WRITE;  /* Device writes */
    }

    /* Register queue with device */
    virtio_mmio_write32(mmio, VIRTIO_MMIO_QUEUE_NUM, queue_size);
    virtio_mmio_write32(mmio, VIRTIO_MMIO_QUEUE_PFN, desc_addr >> 12);

    /* Populate available ring AFTER queue registration */
    __asm__ volatile("dmb sy" ::: "memory");
    for (i = 0; i < queue_size; i++) {
        vq->avail->ring[i] = i;
    }
    vq->avail->idx = queue_size;

    /* Notify device */
    virtio_mmio_write32(mmio, VIRTIO_MMIO_QUEUE_NOTIFY, 0);

    return 0;
}
```

### Event Processing

```c
static void process_input_events(virtio_input_t *dev, input_virtqueue_t *vq, bool is_mouse)
{
    mmio = dev->vdev.mmio_base;

    /* Acknowledge interrupts */
    isr = virtio_mmio_read32(mmio, VIRTIO_MMIO_INTERRUPT_STATUS);
    if (isr) {
        virtio_mmio_write32(mmio, VIRTIO_MMIO_INTERRUPT_ACK, isr);
    }

    /* Memory barrier before reading used ring */
    __asm__ volatile("dmb ish" ::: "memory");

    /* Process all used descriptors */
    while (vq->last_used_idx != vq->used->idx) {
        desc_idx = vq->used->ring[vq->last_used_idx % INPUT_VIRTQ_SIZE].id;
        ev = &vq->events[desc_idx];

        if (is_mouse) {
            switch (ev->type) {
                case EV_REL:
                    /* Relative movement */
                    if (ev->code == REL_X) {
                        event_generate_mouse_move((int32_t)ev->value, 0);
                    } else if (ev->code == REL_Y) {
                        event_generate_mouse_move(0, (int32_t)ev->value);
                    }
                    break;

                case EV_KEY:
                    /* Mouse button */
                    if (ev->code == BTN_LEFT) {
                        event_generate_mouse_button(MOUSE_BUTTON_LEFT, ev->value != 0);
                    }
                    break;
            }
        } else {
            /* Keyboard event. Bound the code against the table before lookup. */
            if (ev->type == EV_KEY && ev->code < sizeof(scancode_to_keycode)) {
                kc = scancode_to_keycode[ev->code];
                if (kc != KEY_NONE) {
                    if (ev->value == 1) {
                        event_generate_key(kc, true);   /* Press */
                    } else if (ev->value == 0) {
                        event_generate_key(kc, false);  /* Release */
                    }
                }
            }
        }

        /* Re-add descriptor to available ring */
        vq->avail->ring[vq->avail->idx % INPUT_VIRTQ_SIZE] = desc_idx;
        __asm__ volatile("dmb ish" ::: "memory");
        vq->avail->idx++;

        vq->last_used_idx++;
    }

    /* Notify device */
    virtio_mmio_write32(mmio, VIRTIO_MMIO_QUEUE_NOTIFY, 0);
}
```

The polling loop drains the device event queue and hands each decoded event to
the `kernel/event.c` queue: `event_generate_mouse_move` and
`event_generate_mouse_button` for the mouse, `event_generate_key` for the
keyboard. Those helpers build an `event_t` and call `event_push`, which is the
only way input reaches the rest of the kernel. The loop logs nothing on the
normal path; the per-event mouse-button log was removed in the BUG-10 cleanup.

The keyboard `scancode_to_keycode` table is sized 128 and maps two families:
the legacy AT-style positions (for example position 72 to `KEY_UP`) and the
Linux input-event-codes navigation cluster, codes 102 through 111
(`KEY_HOME`, `KEY_UP`, `KEY_PAGE_UP`, `KEY_LEFT`, `KEY_RIGHT`, `KEY_END`,
`KEY_DOWN`, `KEY_PAGE_DOWN`, `KEY_INSERT`, `KEY_DELETE`). The second family is
what QEMU's virtio-keyboard emits for the navigation keys, so it backs the
terminal scrollback paging and the editor arrow movement. Codes at or past the
table size, and codes that map to `KEY_NONE`, are dropped.

## Framebuffer Implementation

### Pixel Operations

```c
void fb_putpixel(int32_t x, int32_t y, uint32_t color)
{
    if (x < 0 || x >= fb.width || y < 0 || y >= fb.height) {
        return;
    }

    uint32_t *pixel = (uint32_t *)((uint8_t *)fb.base + y * fb.pitch + x * 4);
    *pixel = color;
}

uint32_t fb_getpixel(int32_t x, int32_t y)
{
    if (x < 0 || x >= fb.width || y < 0 || y >= fb.height) {
        return 0;
    }

    uint32_t *pixel = (uint32_t *)((uint8_t *)fb.base + y * fb.pitch + x * 4);
    return *pixel;
}
```

### Rectangle Drawing

```c
void fb_fill_rect(int32_t x, int32_t y, uint32_t w, uint32_t h, uint32_t color)
{
    /* Clip to framebuffer bounds */
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > fb.width) { w = fb.width - x; }
    if (y + h > fb.height) { h = fb.height - y; }

    /* Draw row by row */
    for (uint32_t row = 0; row < h; row++) {
        uint32_t *pixel = (uint32_t *)((uint8_t *)fb.base + (y + row) * fb.pitch + x * 4);
        for (uint32_t col = 0; col < w; col++) {
            pixel[col] = color;
        }
    }
}
```

### Signed-Coordinate Clipping

Every framebuffer primitive takes signed (`int32_t`) coordinates and clips
internally: `fb_putpixel`, `fb_getpixel`, `fb_fill_rect`, `fb_draw_rect`,
`fb_draw_line`, `fb_putchar`, and `fb_puts` (and their 8x16 large variants).
A coordinate that goes negative is rejected (point primitives) or has the
negative span trimmed against the origin (rect primitives) before any pixel is
written. Because the parameters are signed, window-relative math that produces a
negative coordinate clips to the edge instead of wrapping into a huge unsigned
value and scribbling across the framebuffer. `fb_putpixel` was made signed in
the BUG-18 fix; the rect and line primitives clip the same way.

### Character Rendering

```c
/* 8x8 font - bit 0 is leftmost pixel */
static const uint8_t font8x8[128][8] = {
    /* ASCII 32-126 character bitmaps */
};

void fb_putchar(int32_t x, int32_t y, char c, uint32_t fg, uint32_t bg)
{
    if (c < 32 || c > 126) c = '?';

    const uint8_t *glyph = font8x8[(int)c];

    for (int j = 0; j < 8; j++) {
        uint8_t row = glyph[j];
        for (int i = 0; i < 8; i++) {
            /* Bit 0 is leftmost pixel */
            if (row & (1 << i)) {
                fb_putpixel(x + i, y + j, fg);
            } else {
                fb_putpixel(x + i, y + j, bg);
            }
        }
    }
}
```

**Important**: The font bitmap uses bit 0 as the leftmost pixel, not bit 7. This is critical for correct text rendering.

### Line Drawing

```c
void fb_draw_line(int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint32_t color)
{
    /* Bresenham's line algorithm */
    int dx = abs(x2 - x1);
    int dy = abs(y2 - y1);
    int sx = (x1 < x2) ? 1 : -1;
    int sy = (y1 < y2) ? 1 : -1;
    int err = dx - dy;

    while (1) {
        fb_putpixel(x1, y1, color);

        if (x1 == x2 && y1 == y2) break;

        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x1 += sx; }
        if (e2 < dx) { err += dx; y1 += sy; }
    }
}
```

## Network Stack Implementation

`src/net/net.c` is the protocol half on top of the driver's `net_tx`/`net_rx_poll`.
It is greenfield and minimal: ARP, IPv4, and ICMP echo, no TCP/UDP/DHCP/routing.

### Wire byte order

Network order is big-endian; the kernel is little-endian. Every multi-byte field
goes through byte-at-a-time helpers so a packed frame is never the target of an
unaligned 16-bit store:

```c
static inline void wbe16(uint8_t *p, uint16_t v) { p[0] = v >> 8; p[1] = v & 0xff; }
static inline uint16_t rbe16(const uint8_t *p) { return (p[0] << 8) | p[1]; }
```

The `htons`/`htonl` helpers in `net.h` are integer-only for the same reason (the
build is `-mgeneral-regs-only`; FP/SIMD traps at EL1).

### The bounds-safe parser (the dominant non-negotiable)

`net_rx_dispatch` parses attacker-influenceable bytes. A fault breaks the boot, so
the rule is: bounds-check BEFORE indexing at every layer, and DROP on any
inconsistency. The IPv4 path is the subtle one - the IHL and total-length must be
validated against `len` before they are used to locate the ICMP payload:

```c
if (len < ETH_HDR_LEN + IP_HDR_MIN_LEN) return;          /* 14 + 20 */
uint32_t ihl_bytes = (ip[IP_OFF_VER_IHL] & 0x0f) * 4;
if (ihl_bytes < IP_HDR_MIN_LEN) return;                  /* a sane header */
if (ETH_HDR_LEN + ihl_bytes > len) return;               /* header fits */
uint32_t total_length = rbe16(ip + IP_OFF_TOTAL_LEN);
if (ETH_HDR_LEN + total_length > len) return;            /* claimed <= received */
if (ip[IP_OFF_PROTO] != IP_PROTO_ICMP) return;
if (ETH_HDR_LEN + ihl_bytes + ICMP_HDR_LEN > len) return; /* ICMP header fits */
/* only now is icmp = ip + ihl_bytes safe to index */
```

Single-descriptor RX (MRG_RXBUF not negotiated) means the device cannot split a
frame, so `len` is the whole frame.

### The checksum

```c
uint16_t inet_csum(const uint8_t *data, uint32_t len) {
    uint32_t sum = 0;
    while (len > 1) { sum += (data[0] << 8) | data[1]; data += 2; len -= 2; }
    if (len) sum += data[0] << 8;
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)(~sum);
}
```

Zero the checksum field, compute over the span, store the result big-endian. The
same function covers the 20-byte IPv4 header and the 8-byte ICMP message.

### Lock discipline

The pending-ping state (the awaited id+seq, the got-reply flag, the cached gateway
MAC) is guarded by the driver's `net_lock` via `net_lock_acquire`/`net_lock_release`
- the SAME lock the queue cursors use, not a second lock. `net_lock` is
non-recursive (it spins forever on a held lock), so no flow holds it across a
`net_tx`/`net_rx_poll` call. `net_rx_dispatch` locks only the brief reply match;
the ARP/echo sends and the ping poll loop run with the lock released. Wrapping the
whole dispatch in one critical section would deadlock the moment the echo-request
branch calls `net_tx` (which re-takes the lock).

### Bounded waits

`arp_resolve` and `net_ping` poll with a bound, never an infinite loop. Production
uses a `timer_get_uptime_ms()` deadline (CNTVCT-backed; advances even with starved
ticks); the TEST build has no timer, so it uses a spin-count bound. On expiry both
return -1.

## Memory Barriers

Memory barriers are critical for VirtIO:

```c
/* Data Memory Barrier - ensures all previous memory operations complete */
__asm__ volatile("dmb sy" ::: "memory");  /* System-wide */
__asm__ volatile("dmb ish" ::: "memory"); /* Inner-shareable (sufficient for MMIO) */

/* Data Synchronization Barrier - ensures barrier instruction completes */
__asm__ volatile("dsb ish" ::: "memory");
```

Use barriers:
1. Before updating available ring index
2. After updating available ring index
3. Before reading used ring

## Debugging Tips

### Verify Queue Configuration
```c
klog_debug("Queue: desc=%p avail=%p used=%p", vq->desc, vq->avail, vq->used);
klog_debug("  avail->idx=%u used->idx=%u last_used=%u",
           vq->avail->idx, vq->used->idx, vq->last_used_idx);
```

### Check Interrupt Status
```c
uint32_t isr = virtio_mmio_read32(mmio, VIRTIO_MMIO_INTERRUPT_STATUS);
klog_debug("ISR=0x%x", isr);
```

### Trace Events
```c
klog_debug("Event: type=%u code=%u value=%d", ev->type, ev->code, ev->value);
```

### Common Issues

1. **No events received**: Check that available ring was populated AFTER queue registration
2. **Display not updating**: Ensure transfer_to_host + flush are both called
3. **Wrong device order**: QEMU creates mouse before keyboard
4. **Text mirrored**: Font bit order is bit 0 = leftmost, not bit 7

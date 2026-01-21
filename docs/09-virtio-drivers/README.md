# Section 09: VirtIO Drivers

## Overview

This section implements VirtIO device drivers for AEOS, enabling graphics display and input device support in QEMU. The drivers use the VirtIO MMIO transport layer with support for both legacy (v1) and modern (v2) protocols. VirtIO provides a standardized interface for virtual hardware that is efficient and well-documented.

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
  - Support for legacy and modern VirtIO

### VirtIO Input Driver (virtio_input.c)
- **Location**: `src/drivers/virtio_input.c`
- **Purpose**: Keyboard and mouse input
- **Features**:
  - Automatic device detection
  - Mouse movement (relative and absolute)
  - Mouse button handling
  - Keyboard scancode translation
  - Event generation for GUI

### Framebuffer Driver (framebuffer.c)
- **Location**: `src/drivers/framebuffer.c`
- **Purpose**: Graphics primitives
- **Features**:
  - Pixel drawing
  - Rectangle filling and outlining
  - Line drawing (Bresenham's algorithm)
  - 8x8 font character rendering
  - String output

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
void fb_putchar(int32_t x, int32_t y, char c, uint32_t fg, uint32_t bg);
void fb_puts(int32_t x, int32_t y, const char *s, uint32_t fg, uint32_t bg);
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
- Font: Fixed 8x8 bitmap font only

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

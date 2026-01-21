/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: src/drivers/virtio_input.c
 * Description: VirtIO Input device driver (keyboard and mouse)
 * ============================================================================ */

#include <aeos/virtio_input.h>
#include <aeos/virtio.h>
#include <aeos/event.h>
#include <aeos/framebuffer.h>
#include <aeos/kprintf.h>
#include <aeos/heap.h>
#include <aeos/string.h>

/* Virtqueue configuration */
#define INPUT_VIRTQ_SIZE 64

/* Virtqueue structure */
typedef struct {
    virtq_desc_t *desc;
    virtq_avail_t *avail;
    virtq_used_t *used;
    virtio_input_event_t *events;  /* Event buffer array */
    uint16_t last_used_idx;
    uint16_t num_free;
    uint16_t free_head;
} input_virtqueue_t;

/* Input devices */
static virtio_input_t keyboard_dev;
static virtio_input_t mouse_dev;
static input_virtqueue_t keyboard_eventq;
static input_virtqueue_t mouse_eventq;

/* Event buffers are now allocated inside virtqueue_init and stored in input_virtqueue_t */

/* Keyboard scancode to keycode mapping (simplified) */
static const uint8_t scancode_to_keycode[] = {
    0, KEY_ESCAPE, KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6,  /* 0-7 */
    KEY_7, KEY_8, KEY_9, KEY_0, KEY_MINUS, KEY_EQUAL, KEY_BACKSPACE, KEY_TAB,  /* 8-15 */
    KEY_Q, KEY_W, KEY_E, KEY_R, KEY_T, KEY_Y, KEY_U, KEY_I,  /* 16-23 */
    KEY_O, KEY_P, KEY_LEFTBRACE, KEY_RIGHTBRACE, KEY_ENTER, KEY_LEFTCTRL, KEY_A, KEY_S,  /* 24-31 */
    KEY_D, KEY_F, KEY_G, KEY_H, KEY_J, KEY_K, KEY_L, KEY_SEMICOLON,  /* 32-39 */
    KEY_APOSTROPHE, KEY_GRAVE, KEY_LEFTSHIFT, KEY_BACKSLASH, KEY_Z, KEY_X, KEY_C, KEY_V,  /* 40-47 */
    KEY_B, KEY_N, KEY_M, KEY_COMMA, KEY_DOT, KEY_SLASH, KEY_RIGHTSHIFT, 0,  /* 48-55 */
    KEY_LEFTALT, KEY_SPACE, KEY_CAPSLOCK, KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5,  /* 56-63 */
    KEY_F6, KEY_F7, KEY_F8, KEY_F9, KEY_F10, 0, 0, 0,  /* 64-71 */
    KEY_UP, 0, 0, KEY_LEFT, 0, KEY_RIGHT, 0, 0,  /* 72-79 */
    KEY_DOWN, 0, 0, 0, 0, 0, 0, KEY_F11,  /* 80-87 */
    KEY_F12  /* 88 */
};

/**
 * Initialize a virtqueue for input device
 */
static int input_virtqueue_init(input_virtqueue_t *vq, volatile uint32_t *mmio,
                                 uint32_t queue_idx)
{
    uint32_t queue_size;
    size_t desc_size, avail_size, used_size;
    uint8_t *queue_mem;
    uint64_t desc_addr;
    uint32_t i;

    /* Select queue */
    virtio_mmio_write32(mmio, VIRTIO_MMIO_QUEUE_SEL, queue_idx);

    /* Get maximum queue size */
    queue_size = virtio_mmio_read32(mmio, VIRTIO_MMIO_QUEUE_NUM_MAX);
    if (queue_size == 0) {
        return -1;
    }

    if (queue_size > INPUT_VIRTQ_SIZE) {
        queue_size = INPUT_VIRTQ_SIZE;
    }

    /* Calculate sizes */
    desc_size = sizeof(virtq_desc_t) * queue_size;
    avail_size = sizeof(uint16_t) * (3 + queue_size);
    used_size = sizeof(uint16_t) * 3 + sizeof(virtq_used_elem_t) * queue_size;

    /* Calculate used ring offset (page-aligned for legacy) */
    size_t avail_offset = desc_size;
    size_t used_offset = (desc_size + avail_size + 4095) & ~4095ULL;

    /* Calculate total size including event buffers */
    size_t event_buf_size = sizeof(virtio_input_event_t) * queue_size;
    size_t total_size = used_offset + used_size + event_buf_size + 8192;
    uint8_t *raw_mem = (uint8_t *)kmalloc(total_size);
    if (!raw_mem) {
        return -1;
    }

    /* Align to 4KB page boundary */
    uint64_t addr = (uint64_t)raw_mem;
    uint64_t aligned_addr = (addr + 4095) & ~4095ULL;
    queue_mem = (uint8_t *)aligned_addr;

    memset(queue_mem, 0, used_offset + used_size + event_buf_size);

    /* Place event buffers right after used ring */
    virtio_input_event_t *local_event_buf = (virtio_input_event_t *)(queue_mem + used_offset + used_size);

    /* Set up structures */
    vq->desc = (virtq_desc_t *)queue_mem;
    vq->avail = (virtq_avail_t *)(queue_mem + avail_offset);
    vq->used = (virtq_used_t *)(queue_mem + used_offset);
    vq->events = local_event_buf;  /* Store pointer for later access */
    vq->num_free = queue_size;
    vq->free_head = 0;
    vq->last_used_idx = 0;

    /* Initialize descriptors with local event buffers (but don't add to avail yet) */
    for (i = 0; i < queue_size; i++) {
        vq->desc[i].addr = (uint64_t)&local_event_buf[i];
        vq->desc[i].len = sizeof(virtio_input_event_t);
        vq->desc[i].flags = VIRTQ_DESC_F_WRITE;  /* Device writes to buffer */
        vq->desc[i].next = 0;  /* Not chained */
    }

    /* Initialize avail ring flags (no interrupt suppression) */
    vq->avail->flags = 0;
    vq->avail->idx = 0;

    /* Configure queue */
    virtio_mmio_write32(mmio, VIRTIO_MMIO_QUEUE_NUM, queue_size);

    uint32_t version = virtio_mmio_read32(mmio, VIRTIO_MMIO_VERSION);
    desc_addr = (uint64_t)vq->desc;

    klog_info("VirtIO input queue setup: version=%u, queue_size=%u", version, queue_size);
    klog_info("  desc=%p, avail=%p, used=%p", vq->desc, vq->avail, vq->used);
    klog_info("  events=%p", local_event_buf);

    if (version == 1) {
        /* Legacy VirtIO */
        klog_info("  Using legacy VirtIO, PFN=0x%x", (uint32_t)(desc_addr >> 12));
        virtio_mmio_write32(mmio, VIRTIO_MMIO_QUEUE_ALIGN, 4096);
        virtio_mmio_write32(mmio, VIRTIO_MMIO_QUEUE_PFN, (uint32_t)(desc_addr >> 12));
    } else {
        /* Modern VirtIO */
        klog_info("  Using modern VirtIO v%u", version);
        virtio_mmio_write32(mmio, VIRTIO_MMIO_QUEUE_DESC_LOW, (uint32_t)(desc_addr & 0xFFFFFFFF));
        virtio_mmio_write32(mmio, VIRTIO_MMIO_QUEUE_DESC_HIGH, (uint32_t)(desc_addr >> 32));
        virtio_mmio_write32(mmio, VIRTIO_MMIO_QUEUE_AVAIL_LOW, (uint32_t)((uint64_t)vq->avail & 0xFFFFFFFF));
        virtio_mmio_write32(mmio, VIRTIO_MMIO_QUEUE_AVAIL_HIGH, (uint32_t)((uint64_t)vq->avail >> 32));
        virtio_mmio_write32(mmio, VIRTIO_MMIO_QUEUE_USED_LOW, (uint32_t)((uint64_t)vq->used & 0xFFFFFFFF));
        virtio_mmio_write32(mmio, VIRTIO_MMIO_QUEUE_USED_HIGH, (uint32_t)((uint64_t)vq->used >> 32));
        __asm__ volatile("dmb sy" ::: "memory");
        virtio_mmio_write32(mmio, VIRTIO_MMIO_QUEUE_READY, 1);
    }

    /* Now populate the available ring AFTER queue is registered */
    __asm__ volatile("dmb sy" ::: "memory");
    for (i = 0; i < queue_size; i++) {
        vq->avail->ring[i] = i;
    }
    __asm__ volatile("dmb sy" ::: "memory");
    vq->avail->idx = queue_size;
    __asm__ volatile("dmb sy" ::: "memory");

    klog_info("  Queue populated: avail->idx=%u", vq->avail->idx);

    return 0;
}

/**
 * Initialize a single VirtIO input device
 */
static int init_input_device(uint64_t addr, virtio_input_t *dev,
                              input_virtqueue_t *eventq)
{
    volatile uint32_t *mmio = (volatile uint32_t *)addr;
    uint32_t status;

    /* Check magic and version */
    if (virtio_mmio_read32(mmio, VIRTIO_MMIO_MAGIC) != 0x74726976) {
        return -1;
    }

    uint32_t version = virtio_mmio_read32(mmio, VIRTIO_MMIO_VERSION);
    if (version != 1 && version != 2) {
        return -1;
    }

    /* Check device type */
    uint32_t device_id = virtio_mmio_read32(mmio, VIRTIO_MMIO_DEVICE_ID);
    if (device_id != VIRTIO_ID_INPUT) {
        return -1;
    }

    /* Reset device */
    virtio_mmio_write32(mmio, VIRTIO_MMIO_STATUS, 0);

    /* Set ACKNOWLEDGE */
    virtio_mmio_write32(mmio, VIRTIO_MMIO_STATUS, VIRTIO_STATUS_ACKNOWLEDGE);

    /* Set DRIVER */
    status = virtio_mmio_read32(mmio, VIRTIO_MMIO_STATUS);
    virtio_mmio_write32(mmio, VIRTIO_MMIO_STATUS, status | VIRTIO_STATUS_DRIVER);

    /* For legacy devices, set guest page size */
    if (version == 1) {
        virtio_mmio_write32(mmio, VIRTIO_MMIO_GUEST_PAGE_SIZE, 4096);
    }

    /* Read and accept features */
    virtio_mmio_write32(mmio, VIRTIO_MMIO_DEVICE_FEATURES_SEL, 0);
    uint32_t features_lo = virtio_mmio_read32(mmio, VIRTIO_MMIO_DEVICE_FEATURES);
    virtio_mmio_write32(mmio, VIRTIO_MMIO_DEVICE_FEATURES_SEL, 1);
    uint32_t features_hi = virtio_mmio_read32(mmio, VIRTIO_MMIO_DEVICE_FEATURES);

    if (version == 2) {
        virtio_mmio_write32(mmio, VIRTIO_MMIO_DRIVER_FEATURES_SEL, 0);
        virtio_mmio_write32(mmio, VIRTIO_MMIO_DRIVER_FEATURES, features_lo);
        virtio_mmio_write32(mmio, VIRTIO_MMIO_DRIVER_FEATURES_SEL, 1);
        virtio_mmio_write32(mmio, VIRTIO_MMIO_DRIVER_FEATURES, features_hi | 1);
    }

    /* Set FEATURES_OK */
    status = virtio_mmio_read32(mmio, VIRTIO_MMIO_STATUS);
    virtio_mmio_write32(mmio, VIRTIO_MMIO_STATUS, status | VIRTIO_STATUS_FEATURES_OK);

    /* Verify FEATURES_OK */
    status = virtio_mmio_read32(mmio, VIRTIO_MMIO_STATUS);
    if (!(status & VIRTIO_STATUS_FEATURES_OK)) {
        return -1;
    }

    /* Initialize event virtqueue (queue 0) */
    if (input_virtqueue_init(eventq, mmio, 0) != 0) {
        return -1;
    }

    /* Set DRIVER_OK */
    status = virtio_mmio_read32(mmio, VIRTIO_MMIO_STATUS);
    virtio_mmio_write32(mmio, VIRTIO_MMIO_STATUS, status | VIRTIO_STATUS_DRIVER_OK);

    /* Verify device accepted our configuration */
    status = virtio_mmio_read32(mmio, VIRTIO_MMIO_STATUS);
    klog_info("  Device status after DRIVER_OK: 0x%x", status);

    /* Select queue 0 and notify device to start receiving events */
    virtio_mmio_write32(mmio, VIRTIO_MMIO_QUEUE_SEL, 0);
    __asm__ volatile("dmb sy" ::: "memory");
    virtio_mmio_write32(mmio, VIRTIO_MMIO_QUEUE_NOTIFY, 0);
    klog_info("  Queue 0 notified");

    /* Store device info */
    dev->vdev.mmio_base = mmio;
    dev->vdev.device_id = device_id;
    dev->vdev.initialized = true;
    dev->initialized = true;

    return 0;
}

/**
 * Initialize VirtIO input devices
 */
int virtio_input_init(void)
{
    uint32_t i;
    uint64_t addr;
    bool found_keyboard = false;
    bool found_mouse = false;
    int keyboard_slot = -1;
    int mouse_slot = -1;

    klog_info("Scanning for VirtIO input devices...");

    memset(&keyboard_dev, 0, sizeof(keyboard_dev));
    memset(&mouse_dev, 0, sizeof(mouse_dev));

    /* Scan all VirtIO MMIO device slots */
    for (i = 0; i < VIRTIO_MMIO_COUNT; i++) {
        volatile uint32_t *mmio;
        uint32_t device_id;

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
                klog_debug("Found VirtIO input device at slot %u (mouse)", i);
            } else if (!found_keyboard) {
                keyboard_slot = i;
                found_keyboard = true;
                klog_debug("Found VirtIO input device at slot %u (keyboard)", i);
            }
        }
    }

    /* Initialize keyboard */
    if (keyboard_slot >= 0) {
        addr = VIRTIO_MMIO_BASE + (keyboard_slot * VIRTIO_MMIO_SIZE);
        if (init_input_device(addr, &keyboard_dev, &keyboard_eventq) == 0) {
            keyboard_dev.is_keyboard = true;
            klog_info("VirtIO keyboard initialized at slot %d", keyboard_slot);
        }
    }

    /* Initialize mouse */
    if (mouse_slot >= 0) {
        addr = VIRTIO_MMIO_BASE + (mouse_slot * VIRTIO_MMIO_SIZE);
        if (init_input_device(addr, &mouse_dev, &mouse_eventq) == 0) {
            mouse_dev.is_mouse = true;
            klog_info("VirtIO mouse initialized at slot %d", mouse_slot);
        }
    }

    if (!keyboard_dev.initialized && !mouse_dev.initialized) {
        klog_warn("No VirtIO input devices found");
        return -1;
    }

    return 0;
}

/**
 * Process events from an input device
 */
static void process_input_events(virtio_input_t *dev, input_virtqueue_t *vq, bool is_mouse)
{
    volatile uint32_t *mmio;
    uint32_t isr;

    if (!dev->initialized) {
        return;
    }

    mmio = dev->vdev.mmio_base;

    /* Check and acknowledge interrupt status */
    isr = virtio_mmio_read32(mmio, VIRTIO_MMIO_INTERRUPT_STATUS);
    if (isr) {
        virtio_mmio_write32(mmio, VIRTIO_MMIO_INTERRUPT_ACK, isr);
    }

    /* Memory barrier before reading used ring */
    __asm__ volatile("dmb ish" ::: "memory");

    /* Process all used descriptors */
    while (vq->last_used_idx != vq->used->idx) {
        uint32_t desc_idx = vq->used->ring[vq->last_used_idx % INPUT_VIRTQ_SIZE].id;
        virtio_input_event_t *ev = &vq->events[desc_idx];

        /* Debug: only log button clicks */
        if (is_mouse && ev->type == EV_KEY) {
            klog_info("CLICK: code=%u val=%d", ev->code, (int32_t)ev->value);
        }

        /* Process the event */
        if (is_mouse) {
            /* Mouse/tablet event */
            switch (ev->type) {
                case EV_REL:
                    /* Relative mouse movement */
                    if (ev->code == REL_X) {
                        event_generate_mouse_move((int32_t)ev->value, 0);
                    } else if (ev->code == REL_Y) {
                        event_generate_mouse_move(0, (int32_t)ev->value);
                    }
                    break;
                case EV_ABS:
                    /* Absolute tablet positioning - scale from 0-32767 to screen */
                    if (ev->code == ABS_X) {
                        int32_t x = (ev->value * FB_WIDTH) / 32768;
                        event_set_mouse_position(x, -1);  /* -1 = don't change Y */
                    } else if (ev->code == ABS_Y) {
                        int32_t y = (ev->value * FB_HEIGHT) / 32768;
                        event_set_mouse_position(-1, y);  /* -1 = don't change X */
                    }
                    break;
                case EV_KEY:
                    if (ev->code == BTN_LEFT) {
                        event_generate_mouse_button(MOUSE_BUTTON_LEFT, ev->value != 0);
                    } else if (ev->code == BTN_RIGHT) {
                        event_generate_mouse_button(MOUSE_BUTTON_RIGHT, ev->value != 0);
                    } else if (ev->code == BTN_MIDDLE) {
                        event_generate_mouse_button(MOUSE_BUTTON_MIDDLE, ev->value != 0);
                    }
                    break;
                default:
                    break;
            }
        } else {
            /* Keyboard event */
            if (ev->type == EV_KEY && ev->code < sizeof(scancode_to_keycode)) {
                keycode_t kc = scancode_to_keycode[ev->code];
                if (kc != KEY_NONE) {
                    /* value: 0 = release, 1 = press, 2 = repeat */
                    if (ev->value == 1) {
                        event_generate_key(kc, true);
                    } else if (ev->value == 0) {
                        event_generate_key(kc, false);
                    }
                    /* Ignore repeat (value == 2) for now */
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

/* Debug counter for logging */
static uint32_t poll_count = 0;
static uint16_t last_mouse_used_idx = 0;

/**
 * Poll VirtIO input devices
 */
void virtio_input_poll(void)
{
    poll_count++;

    /* Removed verbose polling logs */
    (void)poll_count;
    (void)last_mouse_used_idx;

    if (keyboard_dev.initialized) {
        process_input_events(&keyboard_dev, &keyboard_eventq, false);
    }

    if (mouse_dev.initialized) {
        process_input_events(&mouse_dev, &mouse_eventq, true);
    }
}

/**
 * Check if VirtIO keyboard is available
 */
bool virtio_keyboard_available(void)
{
    return keyboard_dev.initialized;
}

/**
 * Check if VirtIO mouse is available
 */
bool virtio_mouse_available(void)
{
    return mouse_dev.initialized;
}

/* ============================================================================
 * End of virtio_input.c
 * ============================================================================ */

/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: include/aeos/virtio_input.h
 * Description: VirtIO Input device driver interface
 * ============================================================================ */

#ifndef AEOS_VIRTIO_INPUT_H
#define AEOS_VIRTIO_INPUT_H

#include <aeos/types.h>
#include <aeos/virtio_gpu.h>  /* For common VirtIO definitions */

/* VirtIO Input device type */
#define VIRTIO_ID_INPUT     18

/* VirtIO Input config selectors */
#define VIRTIO_INPUT_CFG_UNSET      0x00
#define VIRTIO_INPUT_CFG_ID_NAME    0x01
#define VIRTIO_INPUT_CFG_ID_SERIAL  0x02
#define VIRTIO_INPUT_CFG_ID_DEVIDS  0x03
#define VIRTIO_INPUT_CFG_PROP_BITS  0x10
#define VIRTIO_INPUT_CFG_EV_BITS    0x11
#define VIRTIO_INPUT_CFG_ABS_INFO   0x12

/* Linux input event types (from linux/input-event-codes.h) */
#define EV_SYN          0x00
#define EV_KEY          0x01
#define EV_REL          0x02
#define EV_ABS          0x03

/* Relative axis codes */
#define REL_X           0x00
#define REL_Y           0x01
#define REL_WHEEL       0x08

/* Absolute axis codes (for tablet) */
#define ABS_X           0x00
#define ABS_Y           0x01

/* Key/button codes for mouse */
#define BTN_LEFT        0x110
#define BTN_RIGHT       0x111
#define BTN_MIDDLE      0x112

/* VirtIO Input event structure */
typedef struct {
    uint16_t type;      /* Event type (EV_KEY, EV_REL, etc.) */
    uint16_t code;      /* Event code */
    uint32_t value;     /* Event value */
} __attribute__((packed)) virtio_input_event_t;

/* VirtIO Input device state */
typedef struct {
    virtio_device_t vdev;
    bool is_keyboard;
    bool is_mouse;
    bool initialized;
} virtio_input_t;

/**
 * Initialize VirtIO input devices
 * Scans for keyboard and mouse devices
 * @return 0 on success (at least one device found), -1 on error
 */
int virtio_input_init(void);

/**
 * Poll VirtIO input devices
 * Generates events for the event queue
 */
void virtio_input_poll(void);

/**
 * Check if VirtIO keyboard is available
 */
bool virtio_keyboard_available(void);

/**
 * Check if VirtIO mouse is available
 */
bool virtio_mouse_available(void);

#endif /* AEOS_VIRTIO_INPUT_H */

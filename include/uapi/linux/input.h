#ifndef LEONOS_UAPI_LINUX_INPUT_H
#define LEONOS_UAPI_LINUX_INPUT_H

#include <stdint.h>
#include <linux/ioctl.h>

/* Linux evdev event layout for the supported x86_64 ABI. */
struct input_event {
    int64_t time_sec;
    int64_t time_usec;
    uint16_t type;
    uint16_t code;
    int32_t value;
};

struct input_id {
    uint16_t bustype;
    uint16_t vendor;
    uint16_t product;
    uint16_t version;
};

struct input_absinfo {
    int32_t value;
    int32_t minimum;
    int32_t maximum;
    int32_t fuzz;
    int32_t flat;
    int32_t resolution;
};

#define EV_SYN 0x00
#define EV_KEY 0x01
#define EV_REL 0x02
#define EV_ABS 0x03
#define EV_MSC 0x04
#define EV_MAX 0x1f
#define EV_CNT (EV_MAX + 1)

#define SYN_REPORT 0

/* The keyboard stream uses Linux input key codes, not the historical
 * LeonOS GUI scan-code constants. */
#define KEY_ESC 1
#define KEY_1 2
#define KEY_0 11
#define KEY_BACKSPACE 14
#define KEY_TAB 15
#define KEY_ENTER 28
#define KEY_LEFTCTRL 29
#define KEY_LEFTSHIFT 42
#define KEY_LEFTALT 56
#define KEY_SPACE 57
#define KEY_F1 59
#define KEY_F12 88
#define KEY_RIGHTCTRL 97
#define KEY_RIGHTALT 100
#define KEY_HOME 102
#define KEY_UP 103
#define KEY_PAGEUP 104
#define KEY_LEFT 105
#define KEY_RIGHT 106
#define KEY_END 107
#define KEY_DOWN 108
#define KEY_PAGEDOWN 109
#define KEY_INSERT 110
#define KEY_DELETE 111
#define KEY_LEFTMETA 125
#define KEY_RIGHTMETA 126
#define KEY_COMPOSE 127
#define KEY_MAX 0x2ff
#define KEY_CNT (KEY_MAX + 1)

#define BTN_LEFT 0x110
#define BTN_RIGHT 0x111
#define BTN_MIDDLE 0x112

#define BUS_USB 0x03
#define BUS_I8042 0x11

#define REL_X 0x00
#define REL_Y 0x01
#define REL_WHEEL 0x08

#define ABS_X 0x00
#define ABS_Y 0x01

#define EVIOCGVERSION _IOR('E', 0x01, int)
#define EVIOCGID _IOR('E', 0x02, struct input_id)
#define EVIOCGNAME(len) _IOC(_IOC_READ, 'E', 0x06, (len))
#define EVIOCGPHYS(len) _IOC(_IOC_READ, 'E', 0x07, (len))
#define EVIOCGBIT(ev, len) _IOC(_IOC_READ, 'E', 0x20 + (ev), (len))
#define EVIOCGKEY(len) _IOC(_IOC_READ, 'E', 0x18, (len))
#define EVIOCGABS(axis) _IOR('E', 0x40 + (axis), struct input_absinfo)
#define EVIOCGRAB _IOW('E', 0x90, int)

#endif

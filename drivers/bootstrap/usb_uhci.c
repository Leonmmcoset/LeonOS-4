#include <ntclks/console.h>
#include <ntclks/framebuffer.h>
#include <ntclks/input.h>
#include <ntclks/pty.h>
#include <ntclks/pci.h>
#include <ntclks/time.h>
#include <ntclks/usb.h>

#include "../../kernel/ntclks/arch/x86_64/port.h"

#define UHCI_PCI_CLASS 0x0cu
#define UHCI_PCI_SUBCLASS 0x03u
#define UHCI_PCI_PROGIF 0x00u

#define USB_UHCI_MAX_CONTROLLERS 4u
#define USB_UHCI_MAX_HID 8u
#define USB_UHCI_MAX_PORTS 2u
#define USB_UHCI_MAX_CONFIG 512u
#define USB_UHCI_MAX_CONTROL_TDS 20u
#define USB_UHCI_MAX_HID_TDS 8u
#define USB_UHCI_QH_COUNT (USB_UHCI_MAX_HID + 2u)
#define USB_UHCI_FRAME_COUNT 1024u
#define USB_UHCI_MAX_HUB_PORTS 8u
#define USB_UHCI_MAX_HUB_DEPTH 1u

#define UHCI_REG_CMD 0x00u
#define UHCI_REG_STATUS 0x02u
#define UHCI_REG_INTR 0x04u
#define UHCI_REG_FRNUM 0x06u
#define UHCI_REG_FLBASEADD 0x08u
#define UHCI_REG_SOFMOD 0x0cu
#define UHCI_REG_PORTSC1 0x10u

#define UHCI_CMD_RUN 0x0001u
#define UHCI_CMD_HCRESET 0x0002u
#define UHCI_CMD_CONFIGURE 0x0040u
#define UHCI_CMD_MAX_PACKET_64 0x0080u

#define UHCI_PORT_CONNECT 0x0001u
#define UHCI_PORT_CONNECT_CHANGE 0x0002u
#define UHCI_PORT_ENABLE 0x0004u
#define UHCI_PORT_ENABLE_CHANGE 0x0008u
#define UHCI_PORT_LOW_SPEED 0x0100u
#define UHCI_PORT_RESET 0x0200u

#define UHCI_LINK_TERMINATE 0x00000001u
#define UHCI_LINK_QH 0x00000002u

#define UHCI_TD_ACTIVE (1u << 23)
#define UHCI_TD_INTERRUPT (1u << 24)
#define UHCI_TD_LOW_SPEED (1u << 26)
#define UHCI_TD_ERROR_COUNT (3u << 27)
#define UHCI_TD_FATAL ((1u << 17) | (1u << 18) | (1u << 20) | \
                      (1u << 21) | (1u << 22))

#define UHCI_PID_OUT 0xe1u
#define UHCI_PID_IN 0x69u
#define UHCI_PID_SETUP 0x2du

#define USB_REQ_GET_STATUS 0x00u
#define USB_REQ_CLEAR_FEATURE 0x01u
#define USB_REQ_SET_FEATURE 0x03u
#define USB_REQ_GET_DESCRIPTOR 0x06u
#define USB_REQ_SET_ADDRESS 0x05u
#define USB_REQ_SET_CONFIGURATION 0x09u
#define USB_REQ_SET_IDLE 0x0au
#define USB_REQ_SET_PROTOCOL 0x0bu
#define USB_DESC_DEVICE 0x01u
#define USB_DESC_CONFIGURATION 0x02u
#define USB_DESC_HUB 0x29u

#define USB_CLASS_HID 0x03u
#define USB_CLASS_HUB 0x09u
#define USB_HID_SUBCLASS_BOOT 0x01u
#define USB_HID_PROTOCOL_KEYBOARD 0x01u
#define USB_HID_PROTOCOL_MOUSE 0x02u
#define USB_EP_INTERRUPT 0x03u

#define USB_HUB_PORT_CONNECTION 0x0001u
#define USB_HUB_PORT_ENABLE 0x0002u
#define USB_HUB_PORT_LOW_SPEED 0x0100u
#define USB_HUB_PORT_POWER 8u
#define USB_HUB_PORT_RESET 4u
#define USB_HUB_C_PORT_CONNECTION 16u
#define USB_HUB_C_PORT_ENABLE 17u
#define USB_HUB_C_PORT_RESET 20u

struct __attribute__((packed, aligned(16))) uhci_td {
    volatile uint32_t link;
    volatile uint32_t status;
    volatile uint32_t token;
    volatile uint32_t buffer;
    volatile uint32_t reserved[4];
};

struct __attribute__((packed, aligned(16))) uhci_qh {
    volatile uint32_t link;
    volatile uint32_t element;
    volatile uint32_t reserved[2];
};

struct usb_hid_device {
    uint8_t used;
    uint8_t keyboard;
    uint8_t mouse;
    uint8_t controller;
    uint8_t address;
    uint8_t low_speed;
    uint8_t interface_number;
    uint8_t endpoint;
    uint16_t max_packet;
    uint8_t qh_index;
    uint8_t td_index;
    uint8_t data_toggle;
    uint8_t modifier;
    uint8_t keys[6];
    int32_t x;
    int32_t y;
    uint8_t buttons;
};

struct uhci_controller {
    uint16_t io_base;
    uint8_t bus;
    uint8_t slot;
    uint8_t function;
    uint8_t port_count;
    uint8_t ready;
    uint8_t qh_count;
    uint8_t qh_tail;
    uint8_t next_address;
};

static struct uhci_controller uhci_controllers[USB_UHCI_MAX_CONTROLLERS];
static struct usb_hid_device usb_hid_devices[USB_UHCI_MAX_CONTROLLERS][USB_UHCI_MAX_HID];
static uint32_t uhci_frame_lists[USB_UHCI_MAX_CONTROLLERS][USB_UHCI_FRAME_COUNT]
    __attribute__((aligned(4096)));
static struct uhci_qh uhci_qhs[USB_UHCI_MAX_CONTROLLERS][USB_UHCI_QH_COUNT]
    __attribute__((aligned(16)));
static struct uhci_td uhci_control_tds[USB_UHCI_MAX_CONTROLLERS][USB_UHCI_MAX_CONTROL_TDS]
    __attribute__((aligned(16)));
static struct uhci_td uhci_hid_tds[USB_UHCI_MAX_CONTROLLERS][USB_UHCI_MAX_HID_TDS]
    __attribute__((aligned(16)));
static uint8_t uhci_control_setup[USB_UHCI_MAX_CONTROLLERS][8]
    __attribute__((aligned(16)));
static uint8_t uhci_control_data[USB_UHCI_MAX_CONTROLLERS][USB_UHCI_MAX_CONFIG]
    __attribute__((aligned(16)));
static uint8_t uhci_hid_reports[USB_UHCI_MAX_CONTROLLERS][USB_UHCI_MAX_HID][8]
    __attribute__((aligned(16)));

static uint32_t usb_phys(const void *ptr)
{
    return (uint32_t)(uintptr_t)ptr;
}

static void usb_memzero(void *ptr, uint32_t length)
{
    uint8_t *bytes = (uint8_t *)ptr;
    while (ptr && length--) {
        *bytes++ = 0;
    }
}

static void usb_memcpy(void *dst, const void *src, uint32_t length)
{
    uint8_t *out = (uint8_t *)dst;
    const uint8_t *in = (const uint8_t *)src;
    while (dst && src && length--) {
        *out++ = *in++;
    }
}

static void uhci_delay(uint32_t loops)
{
    while (loops--) {
        __asm__ volatile("pause");
    }
}

static uint16_t uhci_read16(const struct uhci_controller *controller, uint16_t offset)
{
    return controller ? x86_64_inw((uint16_t)(controller->io_base + offset)) : 0xffffu;
}

static void uhci_write16(const struct uhci_controller *controller, uint16_t offset,
                         uint16_t value)
{
    if (controller) {
        x86_64_outw(value, (uint16_t)(controller->io_base + offset));
    }
}

static void uhci_write32(const struct uhci_controller *controller, uint16_t offset,
                         uint32_t value)
{
    if (controller) {
        x86_64_outl(value, (uint16_t)(controller->io_base + offset));
    }
}

static void uhci_schedule_init(uint32_t controller_id)
{
    struct uhci_controller *controller = &uhci_controllers[controller_id];
    struct uhci_qh *head = &uhci_qhs[controller_id][0];
    struct uhci_qh *control = &uhci_qhs[controller_id][1];
    usb_memzero(uhci_qhs[controller_id], sizeof(uhci_qhs[controller_id]));
    head->link = usb_phys(control) | UHCI_LINK_QH;
    head->element = UHCI_LINK_TERMINATE;
    control->link = usb_phys(head) | UHCI_LINK_QH;
    control->element = UHCI_LINK_TERMINATE;
    controller->qh_count = 2;
    controller->qh_tail = 1;
    for (uint32_t frame = 0; frame < USB_UHCI_FRAME_COUNT; ++frame) {
        uhci_frame_lists[controller_id][frame] = usb_phys(head) | UHCI_LINK_QH;
    }
    uhci_write32(controller, UHCI_REG_FLBASEADD, usb_phys(uhci_frame_lists[controller_id]));
    uhci_write16(controller, UHCI_REG_FRNUM, 0);
}

static int uhci_schedule_add(uint32_t controller_id, uint8_t qh_index)
{
    struct uhci_controller *controller = &uhci_controllers[controller_id];
    struct uhci_qh *head = &uhci_qhs[controller_id][0];
    struct uhci_qh *tail = &uhci_qhs[controller_id][controller->qh_tail];
    struct uhci_qh *qh;
    if (qh_index < 2 || qh_index >= USB_UHCI_QH_COUNT ||
        controller->qh_count >= USB_UHCI_QH_COUNT) {
        return -1;
    }
    qh = &uhci_qhs[controller_id][qh_index];
    qh->link = usb_phys(head) | UHCI_LINK_QH;
    qh->element = UHCI_LINK_TERMINATE;
    tail->link = usb_phys(qh) | UHCI_LINK_QH;
    controller->qh_tail = qh_index;
    ++controller->qh_count;
    return 0;
}

static void uhci_schedule_start(struct uhci_controller *controller)
{
    uhci_write16(controller, UHCI_REG_INTR, 0);
    uhci_write16(controller, UHCI_REG_STATUS, 0xffffu);
    uhci_write16(controller, UHCI_REG_CMD,
                 UHCI_CMD_RUN | UHCI_CMD_CONFIGURE | UHCI_CMD_MAX_PACKET_64);
}

static int uhci_reset_controller(struct uhci_controller *controller)
{
    uint16_t command;
    if (!controller || !controller->io_base) {
        return -1;
    }
    uhci_write16(controller, UHCI_REG_CMD, 0);
    uhci_delay(10000);
    command = UHCI_CMD_HCRESET;
    uhci_write16(controller, UHCI_REG_CMD, command);
    for (uint32_t spin = 0; spin < 1000000u; ++spin) {
        if ((uhci_read16(controller, UHCI_REG_CMD) & UHCI_CMD_HCRESET) == 0) {
            break;
        }
        __asm__ volatile("pause");
    }
    if (uhci_read16(controller, UHCI_REG_CMD) & UHCI_CMD_HCRESET) {
        return -1;
    }
    uhci_write16(controller, UHCI_REG_STATUS, 0xffffu);
    uhci_write16(controller, UHCI_REG_SOFMOD, 0x40u);
    uhci_schedule_init((uint32_t)(controller - uhci_controllers));
    uhci_schedule_start(controller);
    return 0;
}

static int uhci_reset_port(struct uhci_controller *controller, uint8_t port)
{
    uint16_t offset;
    uint16_t status;
    if (!controller || port >= controller->port_count) {
        return -1;
    }
    offset = (uint16_t)(UHCI_REG_PORTSC1 + port * 2u);
    status = uhci_read16(controller, offset);
    if (!(status & UHCI_PORT_CONNECT)) {
        return -2;
    }
    uhci_write16(controller, offset, (uint16_t)(status | UHCI_PORT_RESET));
    time_sleep_ms(60);
    status = uhci_read16(controller, offset);
    uhci_write16(controller, offset,
                 (uint16_t)((status & (UHCI_PORT_CONNECT | UHCI_PORT_LOW_SPEED)) |
                            UHCI_PORT_CONNECT_CHANGE | UHCI_PORT_ENABLE_CHANGE));
    time_sleep_ms(10);
    status = uhci_read16(controller, offset);
    if (!(status & UHCI_PORT_ENABLE)) {
        /* Some UHCI implementations need the enable bit written after reset. */
        uhci_write16(controller, offset, (uint16_t)(status | UHCI_PORT_ENABLE));
        time_sleep_ms(5);
        status = uhci_read16(controller, offset);
    }
    return (status & UHCI_PORT_ENABLE) ? 0 : -1;
}

static uint32_t uhci_token(uint8_t pid, uint8_t address, uint8_t endpoint,
                           uint8_t toggle, uint16_t length)
{
    uint32_t max_length = length ? (uint32_t)(length - 1u) : 0x7ffu;
    return (uint32_t)pid |
           ((uint32_t)address << 8) |
           ((uint32_t)endpoint << 15) |
           ((uint32_t)(toggle & 1u) << 19) |
           ((max_length & 0x7ffu) << 21);
}

static int uhci_td_failed(const struct uhci_td *td)
{
    return td && (td->status & UHCI_TD_FATAL) != 0;
}

static int uhci_wait_control_td(struct uhci_td *td, uint32_t *out_length)
{
    if (!td) {
        return -1;
    }
    for (uint32_t spin = 0; spin < 500000u; ++spin) {
        uint32_t status = td->status;
        if (!(status & UHCI_TD_ACTIVE)) {
            uint32_t actual = status & 0x7ffu;
            if (uhci_td_failed(td)) {
                return -1;
            }
            if (out_length) {
                *out_length = actual == 0x7ffu ? 0 : actual + 1u;
            }
            return 0;
        }
        if ((spin & 0x3ffu) == 0) {
            /* UHCI completion does not need an IRQ here; use the PIT to
             * yield while the controller advances its frame schedule. */
            __asm__ volatile("sti; hlt; cli");
        } else {
            __asm__ volatile("pause");
        }
    }
    return -1;
}

static int uhci_control_transfer(struct uhci_controller *controller, uint8_t address,
                                 uint8_t low_speed, uint8_t max_packet,
                                 const uint8_t setup[8], void *data, uint16_t length)
{
    uint32_t controller_id;
    struct uhci_qh *qh;
    struct uhci_td *tds;
    uint32_t td_count = 0;
    uint32_t data_offset = 0;
    uint8_t data_pid;
    uint8_t status_pid;
    uint8_t toggle = 0;
    uint32_t actual = 0;
    if (!controller || !setup || length > USB_UHCI_MAX_CONFIG) {
        return -1;
    }
    controller_id = (uint32_t)(controller - uhci_controllers);
    if (controller_id >= USB_UHCI_MAX_CONTROLLERS) {
        return -1;
    }
    qh = &uhci_qhs[controller_id][1];
    tds = uhci_control_tds[controller_id];
    usb_memzero(tds, sizeof(uhci_control_tds[controller_id]));
    usb_memcpy(uhci_control_setup[controller_id], setup, 8);

    if (td_count >= USB_UHCI_MAX_CONTROL_TDS) {
        return -1;
    }
    tds[td_count].status = UHCI_TD_ACTIVE | UHCI_TD_ERROR_COUNT |
                           (low_speed ? UHCI_TD_LOW_SPEED : 0);
    tds[td_count].token = uhci_token(UHCI_PID_SETUP, address, 0, 0, 8);
    tds[td_count].buffer = usb_phys(uhci_control_setup[controller_id]);
    ++td_count;

    if (length) {
        data_pid = (setup[0] & 0x80u) ? UHCI_PID_IN : UHCI_PID_OUT;
        status_pid = data_pid == UHCI_PID_IN ? UHCI_PID_OUT : UHCI_PID_IN;
        while (data_offset < length) {
            uint16_t chunk = (uint16_t)(length - data_offset);
            if (chunk > max_packet) {
                chunk = max_packet;
            }
            if (td_count >= USB_UHCI_MAX_CONTROL_TDS) {
                return -1;
            }
            tds[td_count].status = UHCI_TD_ACTIVE | UHCI_TD_ERROR_COUNT |
                                   (low_speed ? UHCI_TD_LOW_SPEED : 0);
            tds[td_count].token = uhci_token(data_pid, address, 0, ++toggle, chunk);
            tds[td_count].buffer = usb_phys((uint8_t *)data + data_offset);
            data_offset += chunk;
            ++td_count;
        }
    } else {
        status_pid = UHCI_PID_IN;
    }

    if (td_count >= USB_UHCI_MAX_CONTROL_TDS) {
        return -1;
    }
    tds[td_count].status = UHCI_TD_ACTIVE | UHCI_TD_ERROR_COUNT |
                           (low_speed ? UHCI_TD_LOW_SPEED : 0);
    tds[td_count].token = uhci_token(status_pid, address, 0, 1, 0);
    tds[td_count].buffer = 0;
    ++td_count;

    for (uint32_t i = 0; i + 1u < td_count; ++i) {
        tds[i].link = usb_phys(&tds[i + 1u]);
    }
    tds[td_count - 1u].link = UHCI_LINK_TERMINATE;
    qh->element = usb_phys(tds);
    __asm__ volatile("mfence" ::: "memory");
    if (uhci_wait_control_td(&tds[td_count - 1u], &actual) < 0) {
        qh->element = UHCI_LINK_TERMINATE;
        return -1;
    }
    qh->element = UHCI_LINK_TERMINATE;
    return 0;
}

static void usb_setup_request(uint8_t setup[8], uint8_t request_type,
                              uint8_t request, uint16_t value, uint16_t index,
                              uint16_t length)
{
    setup[0] = request_type;
    setup[1] = request;
    setup[2] = (uint8_t)value;
    setup[3] = (uint8_t)(value >> 8);
    setup[4] = (uint8_t)index;
    setup[5] = (uint8_t)(index >> 8);
    setup[6] = (uint8_t)length;
    setup[7] = (uint8_t)(length >> 8);
}

static int usb_control_no_data(struct uhci_controller *controller, uint8_t address,
                               uint8_t low_speed, uint8_t max_packet,
                               uint8_t request_type, uint8_t request,
                               uint16_t value, uint16_t index)
{
    uint8_t setup[8];
    usb_setup_request(setup, request_type, request, value, index, 0);
    return uhci_control_transfer(controller, address, low_speed, max_packet,
                                 setup, 0, 0);
}

static int usb_control_in_type(struct uhci_controller *controller, uint8_t address,
                               uint8_t low_speed, uint8_t max_packet,
                               uint8_t request_type, uint8_t request,
                               uint16_t value, uint16_t index, void *data,
                               uint16_t length)
{
    uint8_t setup[8];
    usb_setup_request(setup, request_type, request, value, index, length);
    return uhci_control_transfer(controller, address, low_speed, max_packet,
                                 setup, data, length);
}

static int usb_control_in(struct uhci_controller *controller, uint8_t address,
                          uint8_t low_speed, uint8_t max_packet, uint8_t request,
                          uint16_t value, uint16_t index, void *data, uint16_t length)
{
    return usb_control_in_type(controller, address, low_speed, max_packet,
                               0x80u, request, value, index, data, length);
}

static uint8_t usb_hid_usage_to_keycode(uint8_t usage)
{
    static const uint8_t letters[26] = {
        0x1e, 0x30, 0x2e, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24,
        0x25, 0x26, 0x32, 0x31, 0x18, 0x19, 0x10, 0x13, 0x1f, 0x14,
        0x16, 0x2f, 0x11, 0x2d, 0x15, 0x2c,
    };
    if (usage >= 0x04u && usage <= 0x1du) {
        return letters[usage - 0x04u];
    }
    if (usage >= 0x1eu && usage <= 0x27u) {
        static const uint8_t digits[10] = {0x02, 0x03, 0x04, 0x05, 0x06,
                                           0x07, 0x08, 0x09, 0x0a, 0x0b};
        return digits[usage - 0x1eu];
    }
    switch (usage) {
    case 0x28: return 0x1cu;
    case 0x29: return 0x01u;
    case 0x2a: return 0x0eu;
    case 0x2b: return 0x0fu;
    case 0x2c: return 0x39u;
    case 0x2d: return 0x0cu;
    case 0x2e: return 0x0du;
    case 0x2f: return 0x1au;
    case 0x30: return 0x1bu;
    case 0x31: return 0x2bu;
    case 0x33: return 0x27u;
    case 0x34: return 0x28u;
    case 0x35: return 0x29u;
    case 0x36: return 0x33u;
    case 0x37: return 0x34u;
    case 0x38: return 0x35u;
    case 0x39: return 0x3au;
    case 0x3a: return 0x3bu;
    case 0x3b: return 0x3cu;
    case 0x3c: return 0x3du;
    case 0x3d: return 0x3eu;
    case 0x3e: return 0x3fu;
    case 0x3f: return 0x40u;
    case 0x40: return 0x41u;
    case 0x41: return 0x42u;
    case 0x42: return 0x43u;
    case 0x43: return 0x44u;
    case 0x44: return 0x57u;
    case 0x45: return 0x58u;
    case 0x49: return 0x52u;
    case 0x4a: return 0x47u;
    case 0x4b: return 0x49u;
    case 0x4c: return 0x53u;
    case 0x4d: return 0x4fu;
    case 0x4e: return 0x51u;
    case 0x4f: return 0x4du;
    case 0x50: return 0x4bu;
    case 0x51: return 0x50u;
    case 0x52: return 0x48u;
    case 0x53: return 0x47u;
    case 0x54: return 0x49u;
    case 0x55: return 0x52u;
    case 0x56: return 0x53u;
    case 0x57: return 0x4fu;
    case 0x58: return 0x35u;
    default: return 0;
    }
}

static uint8_t usb_hid_modifier_keycode(uint8_t bit)
{
    static const uint8_t codes[8] = {29u, 42u, 56u, 112u, 116u, 54u, 115u, 113u};
    return bit < 8u ? codes[bit] : 0;
}

static int usb_hid_contains(const uint8_t keys[6], uint8_t usage)
{
    for (uint32_t i = 0; i < 6; ++i) {
        if (keys[i] == usage) {
            return 1;
        }
    }
    return 0;
}

static void usb_hid_keyboard_report(struct usb_hid_device *device,
                                    const uint8_t *report, uint32_t length)
{
    uint8_t modifier;
    uint8_t keys[6] = {0};
    if (!device || !report || length < 2) {
        return;
    }
    modifier = report[0];
    for (uint32_t i = 0; i < 6 && i + 2u < length; ++i) {
        keys[i] = report[i + 2u];
    }
    for (uint8_t bit = 0; bit < 8; ++bit) {
        uint8_t old_state = (uint8_t)((device->modifier >> bit) & 1u);
        uint8_t new_state = (uint8_t)((modifier >> bit) & 1u);
        if (old_state != new_state) {
            uint8_t code = usb_hid_modifier_keycode(bit);
            input_push_key(code, new_state);
            pty_console_key_event(code, new_state);
        }
    }
    for (uint32_t i = 0; i < 6; ++i) {
        if (device->keys[i] && !usb_hid_contains(keys, device->keys[i])) {
            uint8_t code = usb_hid_usage_to_keycode(device->keys[i]);
            if (code) {
                input_push_key(code, 0);
                pty_console_key_event(code, 0);
            }
        }
    }
    for (uint32_t i = 0; i < 6; ++i) {
        if (keys[i] && !usb_hid_contains(device->keys, keys[i])) {
            uint8_t code = usb_hid_usage_to_keycode(keys[i]);
            if (code) {
                input_push_key(code, 1);
                pty_console_key_event(code, 1);
            }
        }
    }
    device->modifier = modifier;
    usb_memcpy(device->keys, keys, sizeof(device->keys));
}

static void usb_hid_mouse_report(struct usb_hid_device *device,
                                 const uint8_t *report, uint32_t length)
{
    const struct framebuffer *fb;
    int32_t dx;
    int32_t dy;
    uint8_t buttons;
    if (!device || !report || length < 3) {
        return;
    }
    buttons = report[0] & 0x07u;
    dx = (int8_t)report[1];
    dy = (int8_t)report[2];
    fb = framebuffer_get();
    if (fb && fb->available) {
        device->x += dx;
        device->y -= dy;
        if (device->x < 0) device->x = 0;
        if (device->y < 0) device->y = 0;
        if (device->x >= (int32_t)fb->width) device->x = (int32_t)fb->width - 1;
        if (device->y >= (int32_t)fb->height) device->y = (int32_t)fb->height - 1;
    }
    device->buttons = buttons;
    input_push_mouse(device->x, device->y, dx, -dy, buttons);
    if (length >= 4 && report[3]) {
        input_push_mouse_wheel(device->x, device->y, (int8_t)report[3], buttons);
    }
}

static int uhci_enumerate_device(uint32_t controller_id, uint8_t root_port,
                                 uint8_t parent_address, uint8_t parent_low_speed,
                                 uint8_t parent_max_packet, uint8_t parent_port,
                                 uint8_t hub_depth);

static int uhci_hub_reset_port(struct uhci_controller *controller,
                               uint8_t hub_address, uint8_t hub_low_speed,
                               uint8_t hub_max_packet, uint8_t port,
                               uint8_t *low_speed)
{
    uint8_t status_data[4];
    uint16_t status;
    if (!controller || !hub_address || !port || !low_speed) {
        return -1;
    }
    /* Powering an already-powered hub port is harmless; some hubs do not
     * implement the request, so only reset and status are required below. */
    (void)usb_control_no_data(controller, hub_address, hub_low_speed,
                              hub_max_packet, 0x23u, USB_REQ_SET_FEATURE,
                              USB_HUB_PORT_POWER, port);
    time_sleep_ms(20);
    if (usb_control_no_data(controller, hub_address, hub_low_speed,
                            hub_max_packet, 0x23u, USB_REQ_SET_FEATURE,
                            USB_HUB_PORT_RESET, port) < 0) {
        return -1;
    }
    time_sleep_ms(60);
    if (usb_control_in_type(controller, hub_address, hub_low_speed,
                            hub_max_packet, 0xa3u, USB_REQ_GET_STATUS, 0,
                            port, status_data, sizeof(status_data)) < 0) {
        return -1;
    }
    status = (uint16_t)status_data[0] | ((uint16_t)status_data[1] << 8);
    (void)usb_control_no_data(controller, hub_address, hub_low_speed,
                              hub_max_packet, 0x23u, USB_REQ_CLEAR_FEATURE,
                              USB_HUB_C_PORT_CONNECTION, port);
    (void)usb_control_no_data(controller, hub_address, hub_low_speed,
                              hub_max_packet, 0x23u, USB_REQ_CLEAR_FEATURE,
                              USB_HUB_C_PORT_ENABLE, port);
    (void)usb_control_no_data(controller, hub_address, hub_low_speed,
                              hub_max_packet, 0x23u, USB_REQ_CLEAR_FEATURE,
                              USB_HUB_C_PORT_RESET, port);
    if (!(status & USB_HUB_PORT_CONNECTION)) {
        return -2;
    }
    if (!(status & USB_HUB_PORT_ENABLE)) {
        return -1;
    }
    *low_speed = (uint8_t)((status & USB_HUB_PORT_LOW_SPEED) != 0);
    return 0;
}

static int uhci_enumerate_device(uint32_t controller_id, uint8_t root_port,
                                 uint8_t parent_address, uint8_t parent_low_speed,
                                 uint8_t parent_max_packet, uint8_t parent_port,
                                 uint8_t hub_depth)
{
    struct uhci_controller *controller = &uhci_controllers[controller_id];
    struct {
        uint8_t interface_number;
        uint8_t protocol;
        uint8_t endpoint;
        uint16_t max_packet;
    } pending[USB_UHCI_MAX_HID];
    uint8_t *data = uhci_control_data[controller_id];
    uint8_t low_speed;
    uint8_t max_packet = 8;
    uint8_t address;
    uint8_t configuration;
    uint16_t total_length;
    uint32_t pending_count = 0;
    int current_hid = -1;
    uint8_t hub_found = 0;
    int port_status;
    uint8_t port = parent_address ? parent_port : root_port;
    if (parent_address) {
        port_status = uhci_hub_reset_port(controller, parent_address,
                                          parent_low_speed, parent_max_packet,
                                          parent_port, &low_speed);
    } else {
        port_status = uhci_reset_port(controller, root_port);
    }
    if (port_status < 0) {
        return port_status;
    }
    if (!parent_address) {
        low_speed = (uint8_t)((uhci_read16(controller,
                                           (uint16_t)(UHCI_REG_PORTSC1 + root_port * 2u)) &
                               UHCI_PORT_LOW_SPEED) != 0);
    }
    usb_memzero(data, USB_UHCI_MAX_CONFIG);
    if (usb_control_in(controller, 0, low_speed, max_packet, USB_REQ_GET_DESCRIPTOR,
                       (uint16_t)(USB_DESC_DEVICE << 8), 0, data, 8) < 0) {
        return -1;
    }
    if (data[0] < 8 || data[1] != USB_DESC_DEVICE) {
        return -1;
    }
    max_packet = data[7];
    if (max_packet != 8 && max_packet != 16 && max_packet != 32 && max_packet != 64) {
        max_packet = 8;
    }
    address = controller->next_address++;
    if (!address || usb_control_no_data(controller, 0, low_speed, max_packet,
                                        0, USB_REQ_SET_ADDRESS, address, 0) < 0) {
        return -1;
    }
    time_sleep_ms(2);
    if (usb_control_in(controller, address, low_speed, max_packet,
                       USB_REQ_GET_DESCRIPTOR, (uint16_t)(USB_DESC_DEVICE << 8),
                       0, data, 18) < 0) {
        return -1;
    }
    if (usb_control_in(controller, address, low_speed, max_packet,
                       USB_REQ_GET_DESCRIPTOR, (uint16_t)(USB_DESC_CONFIGURATION << 8),
                       0, data, 9) < 0 || data[1] != USB_DESC_CONFIGURATION) {
        return -1;
    }
    total_length = (uint16_t)data[2] | ((uint16_t)data[3] << 8);
    if (total_length < 9 || total_length > USB_UHCI_MAX_CONFIG) {
        return -1;
    }
    if (usb_control_in(controller, address, low_speed, max_packet,
                       USB_REQ_GET_DESCRIPTOR, (uint16_t)(USB_DESC_CONFIGURATION << 8),
                       0, data, total_length) < 0) {
        return -1;
    }
    configuration = data[5];
    for (uint32_t offset = 0; offset + 2u <= total_length;) {
        uint8_t length = data[offset];
        uint8_t type = data[offset + 1u];
        if (length < 2 || offset + length > total_length) {
            break;
        }
        if (type == 4 && length >= 9) {
            uint8_t class_code = data[offset + 5u];
            uint8_t subclass = data[offset + 6u];
            uint8_t protocol = data[offset + 7u];
            current_hid = -1;
            if (class_code == USB_CLASS_HUB && !hub_found) {
                hub_found = 1;
            }
            if (class_code == USB_CLASS_HID && subclass == USB_HID_SUBCLASS_BOOT &&
                (protocol == USB_HID_PROTOCOL_KEYBOARD || protocol == USB_HID_PROTOCOL_MOUSE) &&
                pending_count < USB_UHCI_MAX_HID) {
                current_hid = (int)pending_count;
                pending[pending_count].interface_number = data[offset + 2u];
                pending[pending_count].protocol = protocol;
                pending[pending_count].endpoint = 0;
                pending[pending_count].max_packet = 8;
                ++pending_count;
            }
        } else if (type == 5 && length >= 7 && current_hid >= 0) {
            uint8_t endpoint = data[offset + 2u];
            uint8_t attributes = data[offset + 3u];
            if (!pending[current_hid].endpoint && (endpoint & 0x80u) &&
                (attributes & 0x03u) == USB_EP_INTERRUPT) {
                pending[current_hid].endpoint = endpoint & 0x0fu;
                pending[current_hid].max_packet =
                    ((uint16_t)data[offset + 4u] |
                     ((uint16_t)data[offset + 5u] << 8)) & 0x07ffu;
                if (!pending[current_hid].max_packet ||
                    pending[current_hid].max_packet > 8) {
                    pending[current_hid].max_packet = 8;
                }
            }
        }
        offset += length;
    }
    if (!pending_count && !hub_found) {
        return -1;
    }
    if (usb_control_no_data(controller, address, low_speed, max_packet,
                            0, USB_REQ_SET_CONFIGURATION, configuration, 0) < 0) {
        console_printf("[usb] UHCI port=%u set configuration failed\n", port);
        return -1;
    }
    if (hub_found && hub_depth < USB_UHCI_MAX_HUB_DEPTH) {
        uint8_t hub_descriptor[9];
        if (usb_control_in_type(controller, address, low_speed, max_packet,
                                0xa0u, USB_REQ_GET_DESCRIPTOR,
                                (uint16_t)(USB_DESC_HUB << 8), 0,
                                hub_descriptor, sizeof(hub_descriptor)) == 0 &&
            hub_descriptor[1] == USB_DESC_HUB && hub_descriptor[2] != 0) {
            uint8_t hub_ports = hub_descriptor[2];
            if (hub_ports > USB_UHCI_MAX_HUB_PORTS) {
                hub_ports = USB_UHCI_MAX_HUB_PORTS;
            }
            console_printf("[usb] UHCI hub address=%u ports=%u depth=%u\n",
                           address, hub_ports, hub_depth);
            for (uint8_t hub_port = 1; hub_port <= hub_ports; ++hub_port) {
                (void)uhci_enumerate_device(controller_id, hub_port, address,
                                             low_speed, max_packet, hub_port,
                                             (uint8_t)(hub_depth + 1u));
            }
        }
    }
    for (uint32_t i = 0; i < pending_count; ++i) {
        struct usb_hid_device *device;
        uint8_t qh_index;
        if (!pending[i].endpoint || controller->qh_count >= USB_UHCI_QH_COUNT) {
            continue;
        }
        if (usb_control_no_data(controller, address, low_speed, max_packet,
                                0x21u, USB_REQ_SET_PROTOCOL, 0,
                                pending[i].interface_number) < 0 ||
            usb_control_no_data(controller, address, low_speed, max_packet,
                                0x21u, USB_REQ_SET_IDLE, 0,
                                pending[i].interface_number) < 0) {
            console_printf("[usb] UHCI port=%u HID interface=%u protocol setup failed\n",
                           port, pending[i].interface_number);
            continue;
        }
        qh_index = controller->qh_count;
        if (uhci_schedule_add(controller_id, qh_index) < 0) {
            console_printf("[usb] UHCI port=%u HID interface=%u schedule full\n",
                           port, pending[i].interface_number);
            continue;
        }
        device = &usb_hid_devices[controller_id][qh_index - 2u];
        usb_memzero(device, sizeof(*device));
        device->used = 1;
        device->keyboard = pending[i].protocol == USB_HID_PROTOCOL_KEYBOARD;
        device->mouse = pending[i].protocol == USB_HID_PROTOCOL_MOUSE;
        device->controller = (uint8_t)controller_id;
        device->address = address;
        device->low_speed = low_speed;
        device->interface_number = pending[i].interface_number;
        device->endpoint = pending[i].endpoint;
        device->max_packet = pending[i].max_packet;
        device->qh_index = qh_index;
        device->td_index = (uint8_t)(qh_index - 2u);
        if (device->mouse) {
            const struct framebuffer *fb = framebuffer_get();
            device->x = fb && fb->available ? (int32_t)(fb->width / 2u) : 0;
            device->y = fb && fb->available ? (int32_t)(fb->height / 2u) : 0;
        }
        console_printf("[usb] UHCI HID %s address=%u port=%u endpoint=%u packet=%u speed=%s\n",
                       device->keyboard ? "keyboard" : "mouse", address, port,
                       device->endpoint, device->max_packet,
                       low_speed ? "low" : "full");
    }
    return 0;
}

static void uhci_arm_hid(struct usb_hid_device *device)
{
    struct uhci_td *td;
    struct uhci_qh *qh;
    if (!device || !device->used) {
        return;
    }
    td = &uhci_hid_tds[device->controller][device->td_index];
    qh = &uhci_qhs[device->controller][device->qh_index];
    usb_memzero(td, sizeof(*td));
    td->link = UHCI_LINK_TERMINATE;
    td->status = UHCI_TD_ACTIVE | UHCI_TD_ERROR_COUNT | UHCI_TD_INTERRUPT |
                 (device->low_speed ? UHCI_TD_LOW_SPEED : 0);
    td->token = uhci_token(UHCI_PID_IN, device->address, device->endpoint,
                           device->data_toggle, device->max_packet);
    td->buffer = usb_phys(uhci_hid_reports[device->controller][device->td_index]);
    qh->element = usb_phys(td);
    __asm__ volatile("mfence" ::: "memory");
}

static void uhci_poll_controller(uint32_t controller_id)
{
    for (uint32_t i = 0; i < USB_UHCI_MAX_HID; ++i) {
        struct usb_hid_device *device = &usb_hid_devices[controller_id][i];
        struct uhci_td *td;
        uint32_t status;
        uint32_t actual;
        if (!device->used) {
            continue;
        }
        td = &uhci_hid_tds[controller_id][i];
        status = td->status;
        if (status & UHCI_TD_ACTIVE) {
            continue;
        }
        actual = status & 0x7ffu;
        actual = actual == 0x7ffu ? 0 : actual + 1u;
        if (!(status & UHCI_TD_FATAL) && actual) {
            if (actual > device->max_packet) {
                actual = device->max_packet;
            }
            if (device->keyboard) {
                usb_hid_keyboard_report(device, uhci_hid_reports[controller_id][i], actual);
            } else if (device->mouse) {
                usb_hid_mouse_report(device, uhci_hid_reports[controller_id][i], actual);
            }
            device->data_toggle ^= 1u;
        }
        uhci_arm_hid(device);
    }
}

void usb_init(void)
{
    uint32_t found = 0;
    usb_memzero(uhci_controllers, sizeof(uhci_controllers));
    usb_memzero(usb_hid_devices, sizeof(usb_hid_devices));
    for (uint16_t bus = 0; bus < 256 && found < USB_UHCI_MAX_CONTROLLERS; ++bus) {
        for (uint8_t slot = 0; slot < 32 && found < USB_UHCI_MAX_CONTROLLERS; ++slot) {
            for (uint8_t function = 0; function < 8 && found < USB_UHCI_MAX_CONTROLLERS; ++function) {
                uint32_t id = pci_config_read32((uint8_t)bus, slot, function, 0x00);
                uint32_t class_reg;
                uint32_t bar;
                struct uhci_controller *controller;
                if ((id & 0xffffu) == 0xffffu) {
                    if (function == 0) break;
                    continue;
                }
                class_reg = pci_config_read32((uint8_t)bus, slot, function, 0x08);
                if ((class_reg >> 24) != UHCI_PCI_CLASS ||
                    ((class_reg >> 16) & 0xffu) != UHCI_PCI_SUBCLASS ||
                    ((class_reg >> 8) & 0xffu) != UHCI_PCI_PROGIF) {
                    continue;
                }
                bar = pci_config_read32((uint8_t)bus, slot, function, 0x20) & ~0x0fu;
                if (!bar || bar > 0xffffu) {
                    continue;
                }
                controller = &uhci_controllers[found];
                controller->io_base = (uint16_t)bar;
                controller->bus = (uint8_t)bus;
                controller->slot = slot;
                controller->function = function;
                controller->port_count = USB_UHCI_MAX_PORTS;
                controller->next_address = 1;
                pci_config_write16((uint8_t)bus, slot, function, 0x04,
                                   (uint16_t)(pci_config_read16((uint8_t)bus, slot,
                                                                function, 0x04) | 0x0005u));
                if (uhci_reset_controller(controller) < 0) {
                    continue;
                }
                controller->ready = 1;
                ++found;
                for (uint8_t port = 0; port < controller->port_count; ++port) {
                    if (uhci_enumerate_device(found - 1u, port, 0, 0, 8, 0, 0) < 0) {
                        continue;
                    }
                }
            }
        }
    }
    if (found) {
        console_printf("[usb] UHCI controllers=%u HID polling enabled\n", found);
    }
}

void usb_poll(void)
{
    for (uint32_t i = 0; i < USB_UHCI_MAX_CONTROLLERS; ++i) {
        if (uhci_controllers[i].ready) {
            uhci_poll_controller(i);
        }
    }
}

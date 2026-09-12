#include <leonos/driver.h>

#include <ntclks/framebuffer.h>
#include <ntclks/input.h>
#include <ntclks/mouse.h>

static const struct leonos_driver_kernel_api *kernel_api;
static struct framebuffer module_framebuffer;

static const struct framebuffer *module_framebuffer_get(void)
{
    uint32_t width = 1024;
    uint32_t height = 768;
    int available = kernel_api && kernel_api->framebuffer_size &&
                    kernel_api->framebuffer_size(&width, &height) == 0;
    module_framebuffer = (struct framebuffer){
        .width = width,
        .height = height,
        .pitch = width * 4U,
        .bpp = 32,
        .available = available != 0,
    };
    return &module_framebuffer;
}

static void module_console_notice(void)
{
    if (kernel_api && kernel_api->console_write) {
        kernel_api->console_write("[driver] mouse activity\n");
    }
}

#define framebuffer_get() module_framebuffer_get()
#define input_push_mouse(x, y, dx, dy, buttons) \
    kernel_api->input_push_mouse((x), (y), (dx), (dy), (buttons))
#define input_push_mouse_wheel(x, y, wheel, buttons) \
    kernel_api->input_push_mouse_wheel((x), (y), (wheel), (buttons))
#define x86_64_inb(port) kernel_api->inb((port))
#define x86_64_outb(value, port) kernel_api->outb((port), (value))
#define console_printf(...) module_console_notice()

#define PS2_DATA 0x60
#define PS2_STATUS 0x64
#define PS2_COMMAND 0x64
#define PS2_MAX_DRAIN 128

#define VMWARE_MAGIC 0x564d5868u
#define VMWARE_PORT 0x5658u
#define VMWARE_CMD_GETVERSION 10u
#define VMWARE_CMD_ABSPOINTER_DATA 39u
#define VMWARE_CMD_ABSPOINTER_STATUS 40u
#define VMWARE_CMD_ABSPOINTER_COMMAND 41u
#define VMWARE_CMD_ABSPOINTER_RESTRICT 86u

#define VMMOUSE_CMD_ENABLE 0x45414552u
#define VMMOUSE_CMD_DISABLE 0x000000f5u
#define VMMOUSE_CMD_REQUEST_ABSOLUTE 0x53424152u

#define VMMOUSE_VERSION_ID 0x3442554au
#define VMMOUSE_ERROR 0xffff0000u
#define VMMOUSE_RELATIVE_PACKET 0x00010000u
#define VMMOUSE_LEFT_BUTTON 0x20u
#define VMMOUSE_RIGHT_BUTTON 0x10u
#define VMMOUSE_MIDDLE_BUTTON 0x08u
#define VMMOUSE_RESTRICT_CPL0 0x01u

static struct mouse_state state = {
    .x = 320,
    .y = 240,
    .buttons = 0,
    .present = false,
    .absolute = false,
};

static uint8_t packet[4];
static uint8_t packet_index;
static uint8_t packet_size = 3;
static uint32_t events;
static uint8_t last_status;
static uint8_t last_data;
static uint8_t last_ack;
static uint32_t vmware_version;
static bool vmware_backdoor;
static bool vmware_absolute;

struct vmware_call_regs {
    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
    uint32_t esi;
    uint32_t edi;
};

static struct vmware_call_regs vmware_call(uint32_t cmd, uint32_t arg1)
{
    struct vmware_call_regs regs;
    regs.eax = VMWARE_MAGIC;
    regs.ebx = arg1;
    regs.ecx = cmd;
    regs.edx = VMWARE_PORT;
    regs.esi = 0;
    regs.edi = 0;
    __asm__ volatile("inl %%dx, %%eax"
                     : "+a"(regs.eax),
                       "+b"(regs.ebx),
                       "+c"(regs.ecx),
                       "+d"(regs.edx),
                       "+S"(regs.esi),
                       "+D"(regs.edi)
                     :
                     : "memory");
    return regs;
}

static void mouse_clamp_to_framebuffer(void)
{
    const struct framebuffer *fb = framebuffer_get();
    int32_t max_x = fb->available ? (int32_t)fb->width - 1 : 1023;
    int32_t max_y = fb->available ? (int32_t)fb->height - 1 : 767;
    if (max_x < 0) {
        max_x = 0;
    }
    if (max_y < 0) {
        max_y = 0;
    }
    if (state.x < 0) {
        state.x = 0;
    }
    if (state.y < 0) {
        state.y = 0;
    }
    if (state.x > max_x) {
        state.x = max_x;
    }
    if (state.y > max_y) {
        state.y = max_y;
    }
}

static void mouse_reset_position(void)
{
    const struct framebuffer *fb = framebuffer_get();
    if (fb->available) {
        state.x = (int32_t)(fb->width / 2);
        state.y = (int32_t)(fb->height / 2);
    } else {
        state.x = 320;
        state.y = 240;
    }
    mouse_clamp_to_framebuffer();
}

static void mouse_publish(int32_t new_x, int32_t new_y, uint8_t buttons, const char *tag)
{
    int32_t old_x = state.x;
    int32_t old_y = state.y;
    state.x = new_x;
    state.y = new_y;
    mouse_clamp_to_framebuffer();
    state.buttons = buttons;
    (void)tag;
    ++events;
    input_push_mouse(state.x, state.y, state.x - old_x, state.y - old_y, state.buttons);
    if (events <= 8) {
        console_printf("[ntclks] %s mouse event #%u buttons=%u pos=%d,%d delta=%d,%d\n",
                       tag,
                       events,
                       state.buttons,
                       state.x,
                       state.y,
                       state.x - old_x,
                       state.y - old_y);
    }
}

static void mouse_publish_wheel(int32_t wheel, const char *tag)
{
    if (wheel == 0) {
        return;
    }
    (void)tag;
    ++events;
    input_push_mouse_wheel(state.x, state.y, wheel, state.buttons);
    if (events <= 8) {
        console_printf("[ntclks] %s mouse wheel #%u delta=%d pos=%d,%d buttons=%u\n",
                       tag,
                       events,
                       (int)wheel,
                       state.x,
                       state.y,
                       state.buttons);
    }
}

static int32_t vmware_wheel_delta(uint32_t raw_wheel)
{
    int8_t wheel = (int8_t)(raw_wheel & 0xffu);
    if (wheel > 0) {
        return -1;
    }
    if (wheel < 0) {
        return 1;
    }
    return 0;
}

static int wait_input_clear(void)
{
    for (int i = 0; i < 100000; ++i) {
        if ((x86_64_inb(PS2_STATUS) & 0x02) == 0) {
            return 1;
        }
    }
    return 0;
}

static int wait_output_full(void)
{
    for (int i = 0; i < 100000; ++i) {
        if (x86_64_inb(PS2_STATUS) & 0x01) {
            return 1;
        }
    }
    return 0;
}

static void ps2_write_command(uint8_t value)
{
    if (wait_input_clear()) {
        x86_64_outb(value, PS2_COMMAND);
    }
}

static void ps2_write_data(uint8_t value)
{
    if (wait_input_clear()) {
        x86_64_outb(value, PS2_DATA);
    }
}

static void mouse_write(uint8_t value)
{
    ps2_write_command(0xd4);
    ps2_write_data(value);
}

static uint8_t ps2_read_data(void)
{
    if (!wait_output_full()) {
        return 0;
    }
    last_data = x86_64_inb(PS2_DATA);
    return last_data;
}

static void ps2_flush_output(void)
{
    for (int i = 0; i < PS2_MAX_DRAIN; ++i) {
        uint8_t status = x86_64_inb(PS2_STATUS);
        if ((status & 0x01) == 0) {
            break;
        }
        (void)x86_64_inb(PS2_DATA);
    }
}

static int mouse_write_ack(uint8_t value)
{
    for (int attempt = 0; attempt < 3; ++attempt) {
        mouse_write(value);
        uint8_t ack = ps2_read_data();
        last_ack = ack;
        if (ack == 0xfa) {
            return 1;
        }
        if (ack != 0xfe) {
            break;
        }
    }
    return 0;
}

static int mouse_write_value_ack(uint8_t value)
{
    for (int attempt = 0; attempt < 3; ++attempt) {
        /* The controller routes only one byte per 0xd4 prefix, including
         * command parameters. A bare data write goes to the keyboard. */
        mouse_write(value);
        uint8_t ack = ps2_read_data();
        last_ack = ack;
        if (ack == 0xfa) {
            return 1;
        }
        if (ack != 0xfe) {
            break;
        }
    }
    return 0;
}

static int mouse_write_param(uint8_t command, uint8_t value)
{
    return mouse_write_ack(command) && mouse_write_value_ack(value);
}

static uint8_t mouse_get_device_id(void)
{
    if (!mouse_write_ack(0xf2)) {
        return 0;
    }
    return ps2_read_data();
}

static int mouse_enable_wheel(void)
{
    if (!mouse_write_param(0xf3, 200) ||
        !mouse_write_param(0xf3, 100) ||
        !mouse_write_param(0xf3, 80)) {
        return 0;
    }
    return mouse_get_device_id() == 3;
}

static uint8_t vmware_buttons(uint32_t status)
{
    uint8_t buttons = 0;
    if (status & VMMOUSE_LEFT_BUTTON) {
        buttons |= 0x01;
    }
    if (status & VMMOUSE_RIGHT_BUTTON) {
        buttons |= 0x02;
    }
    if (status & VMMOUSE_MIDDLE_BUTTON) {
        buttons |= 0x04;
    }
    return buttons;
}

static bool vmware_detect(void)
{
    struct vmware_call_regs regs = vmware_call(VMWARE_CMD_GETVERSION, 0);
    if (regs.ebx != VMWARE_MAGIC || regs.eax == 0xffffffffu) {
        return false;
    }
    vmware_version = regs.eax;
    return true;
}

static void vmware_disable_absolute(void)
{
    (void)vmware_call(VMWARE_CMD_ABSPOINTER_COMMAND, VMMOUSE_CMD_DISABLE);
    vmware_absolute = false;
    state.absolute = false;
}

static bool vmware_enable_absolute(void)
{
    struct vmware_call_regs status_regs;
    struct vmware_call_regs version_regs;

    (void)vmware_call(VMWARE_CMD_ABSPOINTER_COMMAND, VMMOUSE_CMD_ENABLE);
    status_regs = vmware_call(VMWARE_CMD_ABSPOINTER_STATUS, 0);
    if ((status_regs.eax & 0xffffu) == 0) {
        return false;
    }

    version_regs = vmware_call(VMWARE_CMD_ABSPOINTER_DATA, 1);
    if (version_regs.eax != VMMOUSE_VERSION_ID) {
        vmware_disable_absolute();
        return false;
    }

    (void)vmware_call(VMWARE_CMD_ABSPOINTER_RESTRICT, VMMOUSE_RESTRICT_CPL0);
    (void)vmware_call(VMWARE_CMD_ABSPOINTER_COMMAND, VMMOUSE_CMD_REQUEST_ABSOLUTE);
    vmware_absolute = true;
    state.absolute = true;
    return true;
}

static int32_t vmware_axis_position(uint32_t value, uint32_t extent)
{
    if (!extent) return 0;
    if (value > 0xffffu) value = 0xffffu;
    /* Map the pointer hotspot, allowing the cursor image to clip at edges. */
    return (int32_t)(((uint64_t)value * (extent - 1u)) / 0xffffu);
}

static void vmware_drain_events(void)
{
    const struct framebuffer *fb = framebuffer_get();
    uint32_t width = fb->available ? fb->width : 1024;
    uint32_t height = fb->available ? fb->height : 768;

    for (int count = 0; count < 255; ++count) {
        struct vmware_call_regs status_regs = vmware_call(VMWARE_CMD_ABSPOINTER_STATUS, 0);
        uint32_t queue_length = status_regs.eax & 0xffffu;
        if ((status_regs.eax & VMMOUSE_ERROR) == VMMOUSE_ERROR || queue_length == 0) {
            return;
        }
        if (queue_length % 4 != 0) {
            return;
        }

        struct vmware_call_regs data_regs = vmware_call(VMWARE_CMD_ABSPOINTER_DATA, 4);
        uint32_t status = data_regs.eax;
        uint8_t buttons = vmware_buttons(status);
        int32_t wheel = vmware_wheel_delta(data_regs.edx);

        if (status & VMMOUSE_RELATIVE_PACKET) {
            mouse_publish(state.x + (int32_t)data_regs.ebx,
                          state.y - (int32_t)data_regs.ecx,
                          buttons,
                          "vmware-rel");
            mouse_publish_wheel(wheel, "vmware-rel");
            continue;
        }

        int32_t new_x = vmware_axis_position(data_regs.ebx, width);
        int32_t new_y = vmware_axis_position(data_regs.ecx, height);
        mouse_publish(new_x, new_y, buttons, "vmware-abs");
        mouse_publish_wheel(wheel, "vmware-abs");
    }
}

static void mouse_hardware_init(void)
{
    packet_index = 0;
    events = 0;
    last_status = 0;
    last_data = 0;
    last_ack = 0;
    vmware_version = 0;
    vmware_backdoor = false;
    vmware_absolute = false;
    state.buttons = 0;
    state.present = false;
    state.absolute = false;
    mouse_reset_position();

    ps2_flush_output();
    ps2_write_command(0xa8);
    ps2_write_command(0x20);
    uint8_t status = ps2_read_data();
    status |= 0x02;
    status &= (uint8_t)~0x20;
    ps2_write_command(0x60);
    ps2_write_data(status);
    ps2_flush_output();

    int defaults_ok = mouse_write_ack(0xf6);
    int scale_ok = mouse_write_ack(0xe6);
    int resolution_ok = mouse_write_param(0xe8, 0x03);
    /* Keep the wheel-detection 200/100/80 sequence uninterrupted. A leading
     * 200 selects the Explorer probe instead and leaves a basic 3-byte mouse. */
    int wheel_ok = mouse_enable_wheel();
    int sample_ok = mouse_write_param(0xf3, 200);
    packet_size = wheel_ok ? 4 : 3;
    int enable_ok = mouse_write_ack(0xf4);
    state.present = defaults_ok && enable_ok;
    vmware_backdoor = vmware_detect();
    if (vmware_backdoor) {
        vmware_absolute = vmware_enable_absolute();
    }
    (void)scale_ok;
    (void)resolution_ok;
    (void)sample_ok;
    console_printf("[ntclks] mouse init ps2 defaults=%d scale=%d res=%d sample=%d wheel=%d enable=%d vmware=%d absolute=%d version=0x%x at %d,%d\n",
                   defaults_ok,
                   scale_ok,
                   resolution_ok,
                   sample_ok,
                   wheel_ok,
                   enable_ok,
                   vmware_backdoor ? 1 : 0,
                   vmware_absolute ? 1 : 0,
                   vmware_version,
                   state.x,
                   state.y);
}

static void mouse_hardware_poll(void)
{
    int saw_aux_data = 0;

    for (int limit = 0; limit < PS2_MAX_DRAIN; ++limit) {
        uint8_t status = x86_64_inb(PS2_STATUS);
        last_status = status;
        if ((status & 0x01) == 0) {
            break;
        }
        uint8_t data = x86_64_inb(PS2_DATA);
        last_data = data;
        if ((status & 0x20) == 0) {
            continue;
        }
        saw_aux_data = 1;
        if (vmware_absolute) {
            continue;
        }
        if (packet_index == 0 && (data & 0x08) == 0) {
            continue;
        }
        packet[packet_index++] = data;
        if (packet_index < packet_size) {
            continue;
        }
        packet_index = 0;

        int dx = (int8_t)packet[1];
        int dy = (int8_t)packet[2];
        mouse_publish(state.x + dx, state.y - dy, packet[0] & 0x07, "ps2");
        if (packet_size == 4) {
            int8_t wheel = (int8_t)(packet[3] & 0x0f);
            if (wheel & 0x08) {
                wheel = (int8_t)(wheel | 0xf0);
            }
            /* PS/2 uses positive for down; evdev uses positive for up. */
            mouse_publish_wheel(-wheel, "ps2");
        }
    }

    if (vmware_absolute && saw_aux_data) {
        vmware_drain_events();
    }
}

static const struct mouse_state *mouse_hardware_get_state(void)
{
    return &state;
}

static uint32_t mouse_hardware_event_count(void)
{
    return events;
}

static uint8_t mouse_hardware_last_status(void)
{
    return last_status;
}

static uint8_t mouse_hardware_last_data(void)
{
    return last_data;
}

static uint8_t mouse_hardware_last_ack(void)
{
    return last_ack;
}

static void mouse_driver_poll(void)
{
    mouse_hardware_poll();
}

static void mouse_driver_get_state(struct leonos_driver_mouse_state *out)
{
    const struct mouse_state *source = mouse_hardware_get_state();
    if (!out || !source) {
        return;
    }
    *out = (struct leonos_driver_mouse_state){
        .x = source->x,
        .y = source->y,
        .buttons = source->buttons,
        .present = source->present ? 1U : 0U,
        .absolute = source->absolute ? 1U : 0U,
        .reserved = 0,
        .event_count = mouse_hardware_event_count(),
        .last_status = mouse_hardware_last_status(),
        .last_data = mouse_hardware_last_data(),
        .last_ack = mouse_hardware_last_ack(),
        .reserved2 = 0,
    };
}

static int mouse_driver_init(const struct leonos_driver_kernel_api *api)
{
    static const struct leonos_driver_mouse_ops ops = {
        .poll = mouse_driver_poll,
        .get_state = mouse_driver_get_state,
    };
    if (!api || api->abi_version != LEONOS_DRIVER_ABI_VERSION ||
        api->struct_size < sizeof(*api)) {
        return -22;
    }
    kernel_api = api;
    mouse_hardware_init();
    return kernel_api->register_mouse(&ops);
}

static void mouse_driver_fini(void)
{
    if (kernel_api) {
        (void)mouse_write_ack(0xf5);
    }
    vmware_disable_absolute();
    state.present = false;
}

struct leonos_driver_module leonos_driver_module = {
    .magic = LEONOS_DRIVER_MODULE_MAGIC,
    .abi_version = LEONOS_DRIVER_ABI_VERSION,
    .struct_size = sizeof(struct leonos_driver_module),
    .kind = LEONOS_DRIVER_KIND_INPUT,
    .name = "mouse",
    .version = 1U,
    .reserved = 0,
    .init = mouse_driver_init,
    .fini = mouse_driver_fini,
};

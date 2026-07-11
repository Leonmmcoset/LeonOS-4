#include <leonos/driver.h>

#define COM1 0x3f8

static const struct leonos_driver_kernel_api *kernel_api;
static int serial_ready;

static int serial_transmit_empty(void)
{
    return kernel_api->inb(COM1 + 5) & 0x20;
}

static void serial_hardware_init(void)
{
    kernel_api->outb(COM1 + 1, 0x00);
    kernel_api->outb(COM1 + 3, 0x80);
    kernel_api->outb(COM1 + 0, 0x03);
    kernel_api->outb(COM1 + 1, 0x00);
    kernel_api->outb(COM1 + 3, 0x03);
    kernel_api->outb(COM1 + 2, 0xc7);
    kernel_api->outb(COM1 + 4, 0x0b);
    serial_ready = 1;
}

static int serial_is_ready(void)
{
    return serial_ready;
}

static void serial_putc(char ch)
{
    if (!serial_ready) {
        return;
    }
    (void)serial_transmit_empty;
    kernel_api->outb(COM1, (uint8_t)ch);
}

static void serial_write(const char *s)
{
    while (s && *s) {
        if (*s == '\n') {
            serial_putc('\r');
        }
        serial_putc(*s++);
    }
}

static int serial_driver_init(const struct leonos_driver_kernel_api *api)
{
    static const struct leonos_driver_serial_ops ops = {
        .is_ready = serial_is_ready,
        .write = serial_write,
    };
    if (!api || api->abi_version != LEONOS_DRIVER_ABI_VERSION ||
        api->struct_size < sizeof(*api)) {
        return -22;
    }
    kernel_api = api;
    serial_hardware_init();
    return kernel_api->register_serial(&ops);
}

static void serial_driver_fini(void)
{
    if (kernel_api) {
        kernel_api->outb(COM1 + 1, 0x00);
    }
    serial_ready = 0;
}

struct leonos_driver_module leonos_driver_module = {
    .magic = LEONOS_DRIVER_MODULE_MAGIC,
    .abi_version = LEONOS_DRIVER_ABI_VERSION,
    .struct_size = sizeof(struct leonos_driver_module),
    .kind = LEONOS_DRIVER_KIND_SERIAL,
    .name = "serial",
    .version = 1U,
    .reserved = 0,
    .init = serial_driver_init,
    .fini = serial_driver_fini,
};

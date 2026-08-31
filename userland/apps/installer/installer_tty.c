#include <leonos/stdio.h>
#include <leonos/syscall.h>
#include <leonos/system.h>

#include <stdlib.h>

#include "installer_tty.h"

static int tty_read_line(const char *prompt, char *buffer, uint32_t capacity)
{
    uint32_t length = 0;
    char input;
    if (!buffer || capacity < 2U) {
        return 0;
    }
    buffer[0] = 0;
    if (prompt) {
        write(1, prompt, strlen(prompt));
    }
    while (read(0, &input, 1) > 0) {
        if (input == '\r') {
            continue;
        }
        if (input == '\n') {
            buffer[length] = 0;
            return 1;
        }
        if ((uint8_t)input >= 32U && length + 1U < capacity) {
            buffer[length++] = input;
            buffer[length] = 0;
        }
    }
    return 0;
}

static int tty_line_is(const char *line, const char *expected)
{
    uint32_t i = 0;
    if (!line || !expected) {
        return 0;
    }
    while (line[i] && expected[i]) {
        char a = line[i];
        char b = expected[i];
        if (a >= 'a' && a <= 'z') {
            a = (char)(a - 'a' + 'A');
        }
        if (b >= 'a' && b <= 'z') {
            b = (char)(b - 'a' + 'A');
        }
        if (a != b) {
            return 0;
        }
        ++i;
    }
    return line[i] == 0 && expected[i] == 0;
}

static void tty_print_disks(const struct installer_tty_context *context)
{
    char line[160];
    puts("\nAvailable disks:");
    for (uint32_t i = 0; i < *context->disk_count; ++i) {
        context->format_disk_line(line, sizeof(line), &context->disks[i]);
        printf("  [%u] %s\n", i, line);
    }
}

static int tty_choose_disk(const struct installer_tty_context *context)
{
    char input[24];
    char line[160];
    char *end;
    unsigned long value;
    for (;;) {
        tty_print_disks(context);
        if (!tty_read_line("Select disk number (r to refresh, q to quit): ",
                           input, sizeof(input))) {
            return 0;
        }
        if (tty_line_is(input, "q")) {
            return 0;
        }
        if (tty_line_is(input, "r")) {
            context->refresh_disks();
            continue;
        }
        value = strtoul(input, &end, 10);
        if (end != input && *end == 0 && value < *context->disk_count) {
            *context->selected_disk = (int32_t)value;
            context->format_disk_line(line, sizeof(line),
                                      &context->disks[*context->selected_disk]);
            printf("Selected: %s\n", line);
            return 1;
        }
        puts("Invalid disk selection.");
    }
}

int installer_tty_main(const struct installer_tty_context *context)
{
    char input[32];
    if (!context || !context->disks || !context->disk_count ||
        !context->selected_disk || !context->install_mode ||
        !context->install_success || !context->page ||
        !context->refresh_disks || !context->format_disk_line ||
        !context->print_update_packages || !context->prepare_update ||
        !context->perform_install || !context->perform_update) {
        return 1;
    }

    puts("LeonOS 4 installer (TTY)");
    puts("This installer uses the same disk formatter and payload as the graphical installer.");
    puts("A fresh installation erases the selected disk.");
    for (;;) {
        if (!tty_read_line("Mode [install/update]: ", input, sizeof(input))) {
            return 1;
        }
        if (tty_line_is(input, "install") || tty_line_is(input, "i")) {
            *context->install_mode = INSTALLER_TTY_MODE_INSTALL;
            break;
        }
        if (tty_line_is(input, "update") || tty_line_is(input, "u")) {
            *context->install_mode = INSTALLER_TTY_MODE_UPDATE;
            break;
        }
        puts("Enter install or update.");
    }

    context->refresh_disks();
    if (!*context->disk_count || !tty_choose_disk(context)) {
        puts("No disk selected. Installation cancelled.");
        return 1;
    }

    if (*context->install_mode == INSTALLER_TTY_MODE_UPDATE) {
        context->prepare_update();
        if (*context->page != context->update_apps_page) {
            return 1;
        }
        context->print_update_packages();
        if (!tty_read_line("Type UPDATE to confirm an in-place update: ",
                           input, sizeof(input)) || !tty_line_is(input, "UPDATE")) {
            puts("Update not confirmed. Installation cancelled.");
            return 1;
        }
        context->perform_update();
    } else {
        if (!tty_read_line("Type INSTALL to confirm erasing this disk: ",
                           input, sizeof(input)) || !tty_line_is(input, "INSTALL")) {
            puts("Installation not confirmed. Installation cancelled.");
            return 1;
        }
        context->perform_install();
    }

    if (!*context->install_success) {
        return 1;
    }
    if (tty_read_line("Reboot now? [Y/n]: ", input, sizeof(input)) &&
        input[0] != 'n' && input[0] != 'N') {
        leonos_system_reboot();
    }

    puts("Installation finished.");
    for (;;) {
        if (!tty_read_line("Command [reboot/shutdown/exit]: ", input, sizeof(input))) {
            return 0;
        }
        if (tty_line_is(input, "reboot") || tty_line_is(input, "r")) {
            leonos_system_reboot();
        } else if (tty_line_is(input, "shutdown") || tty_line_is(input, "poweroff")) {
            leonos_system_shutdown();
        } else if (tty_line_is(input, "exit") || tty_line_is(input, "q")) {
            return 0;
        } else {
            puts("Unknown command. Use reboot, shutdown, or exit.");
        }
    }
}

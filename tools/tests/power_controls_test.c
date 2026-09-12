#include <assert.h>
#include <errno.h>
#include <string.h>
#include <sys/wait.h>

#define wait4 leonos_decl_wait4
int leonos_decl_wait4(int pid, int *status, int options, void *usage);
#include "../../userland/apps/desktop/input.c"
#undef wait4

uint8_t power_confirm_action, full_redraw_pending;
static unsigned reboots, shutdowns, messages;
static char last_message[128];

uint32_t fb_w(void) { return 1920; }
uint32_t fb_h(void) { return 1080; }
int hit_rect(uint32_t x, uint32_t y, int bx, int by, uint32_t w, uint32_t h)
{ return x >= (uint32_t)bx && y >= (uint32_t)by && x - bx < w && y - by < h; }
int leonos_system_reboot(void) { ++reboots; errno = EPERM; return -1; }
int leonos_system_shutdown(void) { ++shutdowns; errno = EINVAL; return -1; }
const char *leonos_i18n(const char *en, const char *zh) { (void)zh; return en; }
void desktop_show_message(const char *title, const char *message)
{
    assert(strstr(title, "failed"));
    ++messages;
    snprintf(last_message, sizeof(last_message), "%s", message);
}

int main(void)
{
    power_confirm_action = POWER_CONFIRM_REBOOT;
    assert(desktop_handle_power_confirm_click(1088, 588));
    assert(!power_confirm_action && !reboots && !shutdowns && !messages);
    power_confirm_action = POWER_CONFIRM_REBOOT;
    assert(desktop_handle_power_confirm_click(1008, 588));
    assert(!power_confirm_action && reboots == 1 && !shutdowns && messages == 1);
    assert(!strcmp(last_message, strerror(EPERM)));
    power_confirm_action = POWER_CONFIRM_SHUTDOWN;
    assert(desktop_handle_power_confirm_click(1008, 588));
    assert(!power_confirm_action && reboots == 1 && shutdowns == 1 && messages == 2);
    assert(!strcmp(last_message, strerror(EINVAL)));
    assert(!desktop_handle_power_confirm_click(1008, 588));
    assert(messages == 2);
    puts("PASS desktop power controls: cancel, command selection, error reporting and dismissed dialog");
}

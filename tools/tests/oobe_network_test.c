#include <assert.h>
#include <signal.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define wait4 leonos_decl_wait4
#include "../../userland/apps/desktop/screen.c"
#undef wait4

struct leonos_ui_surface ui;
uint8_t desktop_service_network_icon = 1;
uint8_t desktop_service_rtc_clock = 1;
uint8_t desktop_taskbar_visible = 1;
uint8_t oobe_lock_active;
uint8_t login_lock_active;
uint8_t full_redraw_pending;
static int parent_pid;
uint32_t fb_w(void) { return 1280; }
uint32_t desktop_tray_width(void) { return 160; }
unsigned long leonos_uptime_ms(void) { return 1000; }
int sleep_ms(unsigned long ms) { return usleep(ms * 1000); }
int leonos_decl_wait4(int pid, int *status, int options, void *usage)
{ assert(options == WNOHANG && !usage); return waitpid(pid, status, options); }

int net_service_config(net_service_config_t *config)
{
    static int requests;
    assert(getpid() != parent_pid);
    usleep(350000);
    *config = (net_service_config_t){
        .flags = NET_SERVICE_CONFIG_FLAG_ACTIVE | NET_SERVICE_CONFIG_FLAG_DHCP,
        .source = NET_SERVICE_CONFIG_SOURCE_DHCP, .local_ip = 1, .gateway_ip = 2};
    if (++requests > 1) config->flags = 0;
    return 0;
}

void leonos_ui_rect(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                    uint32_t width, uint32_t height, uint32_t color)
{ (void)surface; (void)x; (void)y; (void)width; (void)height; (void)color; }
void leonos_ui_bevel(struct leonos_ui_surface *surface, uint32_t x, uint32_t y,
                     uint32_t width, uint32_t height, uint32_t color, uint32_t flags)
{ (void)surface; (void)x; (void)y; (void)width; (void)height; (void)color; (void)flags; }
uint32_t leonos_ui_text_width(const char *text) { (void)text; return 0; }
uint32_t leonos_ui_color(uint32_t role) { return role; }
void leonos_ui_text_transparent_clipped(struct leonos_ui_surface *surface,
                                       uint32_t x, uint32_t y, uint32_t width,
                                       const char *text, uint32_t color)
{ (void)surface; (void)x; (void)y; (void)width; (void)text; (void)color; }

int main(void)
{
    struct timespec before, after;
    parent_pid = getpid();
    clock_gettime(CLOCK_MONOTONIC, &before);
    desktop_poll_network_state();
    draw_taskbar_network_icon(766);
    clock_gettime(CLOCK_MONOTONIC, &after);
    double elapsed = (after.tv_sec - before.tv_sec) +
                     (after.tv_nsec - before.tv_nsec) / 1e9;
    assert(elapsed < 0.1);
    for (int i = 0; i < 2000 && !taskbar_network_cache_valid; ++i) {
        desktop_poll_network_state();
        usleep(1000);
    }
    assert(taskbar_network_connected && full_redraw_pending);
    full_redraw_pending = 0;
    for (int i = 0; i < 2000 && taskbar_network_connected; ++i) {
        desktop_poll_network_state();
        draw_taskbar_network_icon(766);
        usleep(1000);
    }
    assert(!taskbar_network_connected && full_redraw_pending);
    assert(taskbar_network_worker_pid > 0);
    assert(kill(taskbar_network_worker_pid, SIGTERM) == 0);
    for (int i = 0; i < 1000 && taskbar_network_worker_pid; ++i) {
        desktop_poll_network_state();
        usleep(1000);
    }
    assert(taskbar_network_worker_pid == 0 && taskbar_network_pipe == -1);
    puts("OOBE taskbar: slow network replies update the icon without blocking rendering");
    return 0;
}

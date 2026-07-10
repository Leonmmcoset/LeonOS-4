#include <leonos/fs.h>
#include <leonos/gui.h>
#include <leonos/net.h>
#include <leonos/stdio.h>
#include <leonos/syscall.h>
#include <leonos/system.h>

#define SERVICE_CONFIG_PATH "0:/etc/services.cfg"
#define SERVICE_STATE_PATH "0:/var/run/services.state"
#define SERVICE_COMMAND_PATH "0:/var/run/services.cmd"
#define SERVICE_LOG_PATH "0:/var/log/services.log"
#define SERVICE_CONFIG_MAX 512U
#define SERVICE_COMMAND_MAX 512U
#define SERVICE_STATE_MAX 1024U
#define SERVICE_DETAIL_LEN 96U
#define SERVICE_STATE_LEN 16U
#define SERVICE_COUNT 5U
#define DHCP_RETRY_MS 30000UL
#define DHCP_TIMEOUT_MS 4000U
#define NTP_SUCCESS_INTERVAL_MS 21600000UL
#define NTP_FAILURE_RETRY_MS 300000UL
#define NTP_TIMEOUT_MS 4000U

struct service_entry {
    const char *key;
    uint8_t default_enabled;
    uint8_t locked;
    uint8_t enabled;
    char state[SERVICE_STATE_LEN];
    char detail[SERVICE_DETAIL_LEN];
    uint32_t pid;
};

static struct service_entry services[SERVICE_COUNT] = {
    {"desktop", 1, 1, 1, "running", "window server owns desktop", 0},
    {"dhcp", 1, 0, 1, "starting", "waiting for network state", 0},
    {"network_icon", 1, 0, 1, "running", "desktop taskbar switch enabled", 0},
    {"rtc_clock", 1, 0, 1, "running", "desktop taskbar switch enabled", 0},
    {"ntp_sync", 0, 0, 0, "stopped", "disabled by policy", 0},
};

static unsigned long last_dhcp_attempt_ms;
static uint8_t dhcp_attempted;
static uint8_t force_dhcp_renew;
static unsigned long last_ntp_attempt_ms;
static uint8_t ntp_attempted;
static uint8_t ntp_last_result_ok;

static uint32_t text_len(const char *text)
{
    uint32_t n = 0;
    while (text && text[n]) {
        ++n;
    }
    return n;
}

static int text_eq(const char *a, const char *b)
{
    if (!a || !b) {
        return 0;
    }
    while (*a && *b && *a == *b) {
        ++a;
        ++b;
    }
    return *a == 0 && *b == 0;
}

static void copy_text(char *dst, uint32_t cap, const char *src)
{
    uint32_t i = 0;
    if (!dst || cap == 0) {
        return;
    }
    while (src && src[i] && i + 1U < cap) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = 0;
}

static void append_char(char *dst, uint32_t *pos, uint32_t cap, char ch)
{
    if (dst && pos && *pos + 1U < cap) {
        dst[*pos] = ch;
        ++(*pos);
        dst[*pos] = 0;
    }
}

static void append_text(char *dst, uint32_t *pos, uint32_t cap,
                        const char *src)
{
    while (src && *src) {
        append_char(dst, pos, cap, *src++);
    }
}

static void append_u32(char *dst, uint32_t *pos, uint32_t cap, uint32_t value)
{
    char tmp[12];
    uint32_t n = 0;
    if (value == 0) {
        append_char(dst, pos, cap, '0');
        return;
    }
    while (value && n < sizeof(tmp)) {
        tmp[n++] = (char)('0' + (value % 10U));
        value /= 10U;
    }
    while (n) {
        append_char(dst, pos, cap, tmp[--n]);
    }
}

static void append_ipv4(char *dst, uint32_t *pos, uint32_t cap, uint32_t ip)
{
    append_u32(dst, pos, cap, (ip >> 24) & 0xffU);
    append_char(dst, pos, cap, '.');
    append_u32(dst, pos, cap, (ip >> 16) & 0xffU);
    append_char(dst, pos, cap, '.');
    append_u32(dst, pos, cap, (ip >> 8) & 0xffU);
    append_char(dst, pos, cap, '.');
    append_u32(dst, pos, cap, ip & 0xffU);
}

static int read_file_text(const char *path, char *buffer, uint32_t cap,
                          uint32_t *out_len)
{
    uint32_t len = 0;
    int fd;
    if (!buffer || cap == 0) {
        return -1;
    }
    buffer[0] = 0;
    fd = open(path, LEONOS_O_RDONLY, 0);
    if (fd < 0) {
        if (out_len) {
            *out_len = 0;
        }
        return fd;
    }
    while (len + 1U < cap) {
        long got = read(fd, buffer + len, cap - len - 1U);
        if (got < 0) {
            close(fd);
            return (int)got;
        }
        if (got == 0) {
            break;
        }
        len += (uint32_t)got;
    }
    close(fd);
    buffer[len] = 0;
    if (out_len) {
        *out_len = len;
    }
    return 0;
}

static int write_file_text(const char *path, const char *buffer, uint32_t len,
                           uint32_t append)
{
    int flags = LEONOS_O_WRONLY | LEONOS_O_CREAT;
    int fd;
    long wrote;
    flags |= append ? LEONOS_O_APPEND : LEONOS_O_TRUNC;
    fd = open(path, flags, 0);
    if (fd < 0) {
        return fd;
    }
    wrote = write(fd, buffer, len);
    close(fd);
    if (wrote < 0) {
        return (int)wrote;
    }
    return (uint32_t)wrote == len ? 0 : -1;
}

static void log_line(const char *message)
{
    char line[192];
    uint32_t pos = 0;
    line[0] = 0;
    append_char(line, &pos, sizeof(line), '[');
    append_u32(line, &pos, sizeof(line), (uint32_t)(leonos_uptime_ms() / 1000UL));
    append_text(line, &pos, sizeof(line), "s] ");
    append_text(line, &pos, sizeof(line), message);
    append_char(line, &pos, sizeof(line), '\n');
    (void)write_file_text(SERVICE_LOG_PATH, line, pos, 1);
}

static const char *net_status_name(uint32_t status)
{
    switch (status) {
    case LEONOS_NET_STATUS_OK:
        return "OK";
    case LEONOS_NET_STATUS_NO_DEVICE:
        return "no e1000 adapter";
    case LEONOS_NET_STATUS_DHCP_TIMEOUT:
        return "DHCP timeout";
    case LEONOS_NET_STATUS_DHCP_FAILED:
        return "DHCP failed";
    case LEONOS_NET_STATUS_TX_FAILED:
        return "transmit failed";
    case LEONOS_NET_STATUS_DNS_TIMEOUT:
        return "DNS timeout";
    case LEONOS_NET_STATUS_DNS_FAILED:
        return "DNS failed";
    default:
        return "network error";
    }
}

static void set_service_state(uint32_t index, const char *state,
                              const char *detail, uint32_t pid)
{
    if (index >= SERVICE_COUNT) {
        return;
    }
    copy_text(services[index].state, sizeof(services[index].state), state);
    copy_text(services[index].detail, sizeof(services[index].detail), detail);
    services[index].pid = pid;
}

static int service_line_matches(const char *line, uint32_t len,
                                const char *key, uint8_t *value)
{
    uint32_t key_len = text_len(key);
    if (!line || !key || !value || key_len == 0 || len <= key_len ||
        line[key_len] != '=') {
        return 0;
    }
    for (uint32_t i = 0; i < key_len; ++i) {
        if (line[i] != key[i]) {
            return 0;
        }
    }
    *value = line[key_len + 1U] == '1' ||
             line[key_len + 1U] == 'y' ||
             line[key_len + 1U] == 'Y';
    return 1;
}

static void load_config(void)
{
    char cfg[SERVICE_CONFIG_MAX];
    uint32_t len = 0;
    uint32_t pos = 0;
    for (uint32_t i = 0; i < SERVICE_COUNT; ++i) {
        services[i].enabled = services[i].default_enabled;
    }
    services[0].enabled = 1;
    if (read_file_text(SERVICE_CONFIG_PATH, cfg, sizeof(cfg), &len) < 0) {
        return;
    }
    while (pos < len) {
        uint32_t start = pos;
        uint32_t line_len;
        while (pos < len && cfg[pos] != '\n' && cfg[pos] != '\r') {
            ++pos;
        }
        line_len = pos - start;
        while (pos < len && (cfg[pos] == '\n' || cfg[pos] == '\r')) {
            ++pos;
        }
        for (uint32_t i = 0; i < SERVICE_COUNT; ++i) {
            uint8_t value = 0;
            if (!services[i].locked &&
                service_line_matches(cfg + start, line_len, services[i].key, &value)) {
                services[i].enabled = value;
            }
        }
    }
}

static void save_config(void)
{
    char cfg[SERVICE_CONFIG_MAX];
    uint32_t pos = 0;
    cfg[0] = 0;
    append_text(cfg, &pos, sizeof(cfg), "# LeonOS service startup settings\n");
    for (uint32_t i = 0; i < SERVICE_COUNT; ++i) {
        append_text(cfg, &pos, sizeof(cfg), services[i].key);
        append_char(cfg, &pos, sizeof(cfg), '=');
        append_char(cfg, &pos, sizeof(cfg), services[i].enabled ? '1' : '0');
        append_char(cfg, &pos, sizeof(cfg), '\n');
    }
    (void)write_file_text(SERVICE_CONFIG_PATH, cfg, pos, 0);
}

static int find_service(const char *key)
{
    for (uint32_t i = 0; i < SERVICE_COUNT; ++i) {
        if (text_eq(key, services[i].key)) {
            return (int)i;
        }
    }
    return -1;
}

static void read_word(const char *line, uint32_t len, uint32_t *pos,
                      char *out, uint32_t cap)
{
    uint32_t dst = 0;
    while (*pos < len && (line[*pos] == ' ' || line[*pos] == '\t')) {
        ++(*pos);
    }
    while (*pos < len && line[*pos] != ' ' && line[*pos] != '\t' &&
           line[*pos] != '\r' && line[*pos] != '\n') {
        if (dst + 1U < cap) {
            out[dst++] = line[*pos];
        }
        ++(*pos);
    }
    if (cap) {
        out[dst] = 0;
    }
}

static void apply_command_line(const char *line, uint32_t len)
{
    char action[16];
    char key[32];
    char logbuf[96];
    uint32_t pos = 0;
    uint32_t log_pos = 0;
    int index;
    read_word(line, len, &pos, action, sizeof(action));
    read_word(line, len, &pos, key, sizeof(key));
    index = find_service(key);
    if (index < 0 || services[index].locked) {
        return;
    }
    if (text_eq(action, "start")) {
        services[index].enabled = 1;
    } else if (text_eq(action, "stop")) {
        services[index].enabled = 0;
    } else if (text_eq(action, "restart")) {
        services[index].enabled = 1;
        if (text_eq(key, "dhcp")) {
            force_dhcp_renew = 1;
        } else if (text_eq(key, "ntp_sync")) {
            ntp_attempted = 0;
        }
    } else {
        return;
    }
    save_config();
    logbuf[0] = 0;
    append_text(logbuf, &log_pos, sizeof(logbuf), "command ");
    append_text(logbuf, &log_pos, sizeof(logbuf), action);
    append_char(logbuf, &log_pos, sizeof(logbuf), ' ');
    append_text(logbuf, &log_pos, sizeof(logbuf), key);
    log_line(logbuf);
}

static void process_commands(void)
{
    char cmd[SERVICE_COMMAND_MAX];
    uint32_t len = 0;
    uint32_t pos = 0;
    if (read_file_text(SERVICE_COMMAND_PATH, cmd, sizeof(cmd), &len) < 0 || len == 0) {
        return;
    }
    while (pos < len) {
        uint32_t start = pos;
        while (pos < len && cmd[pos] != '\n' && cmd[pos] != '\r') {
            ++pos;
        }
        if (pos > start) {
            apply_command_line(cmd + start, pos - start);
        }
        while (pos < len && (cmd[pos] == '\n' || cmd[pos] == '\r')) {
            ++pos;
        }
    }
    (void)write_file_text(SERVICE_COMMAND_PATH, "", 0, 0);
}

static void update_dhcp(unsigned long now)
{
    struct leonos_net_config config;
    uint32_t pid = (uint32_t)getpid();
    char detail[SERVICE_DETAIL_LEN];
    uint32_t pos = 0;
    if (!services[1].enabled) {
        dhcp_attempted = 0;
        set_service_state(1, "stopped", "disabled by policy", 0);
        return;
    }
    if (leonos_net_config(&config) < 0) {
        set_service_state(1, "failed", "network config query failed", pid);
        return;
    }
    if ((config.flags & LEONOS_NET_CONFIG_FLAG_ACTIVE) &&
        (config.flags & LEONOS_NET_CONFIG_FLAG_DHCP) &&
        config.source == LEONOS_NET_CONFIG_SOURCE_DHCP &&
        config.local_ip && config.gateway_ip) {
        detail[0] = 0;
        append_text(detail, &pos, sizeof(detail), "DHCP lease active ip=");
        append_ipv4(detail, &pos, sizeof(detail), config.local_ip);
        set_service_state(1, "running", detail, pid);
        force_dhcp_renew = 0;
        return;
    }
    if (force_dhcp_renew || !dhcp_attempted ||
        now - last_dhcp_attempt_ms >= DHCP_RETRY_MS) {
        struct leonos_net_dhcp dhcp;
        int ret;
        last_dhcp_attempt_ms = now;
        dhcp_attempted = 1;
        force_dhcp_renew = 0;
        log_line("DHCP renew attempt");
        ret = leonos_net_dhcp_renew(DHCP_TIMEOUT_MS, &dhcp);
        if (ret == 0 && dhcp.status == LEONOS_NET_STATUS_OK) {
            detail[0] = 0;
            pos = 0;
            append_text(detail, &pos, sizeof(detail), "DHCP lease active ip=");
            append_ipv4(detail, &pos, sizeof(detail), dhcp.config.local_ip);
            set_service_state(1, "running", detail, pid);
            log_line("DHCP lease acquired");
            return;
        }
        detail[0] = 0;
        pos = 0;
        append_text(detail, &pos, sizeof(detail), net_status_name(dhcp.status));
        append_text(detail, &pos, sizeof(detail), "; static fallback active");
        set_service_state(1, "failed", detail, pid);
        log_line("DHCP renew failed");
        return;
    }
    set_service_state(1, "failed", "static fallback active; retry pending", pid);
}

static void update_simple_services(void)
{
    set_service_state(0, "running", "window server owns desktop", 0);
    set_service_state(2,
                      services[2].enabled ? "running" : "stopped",
                      services[2].enabled ? "desktop taskbar switch enabled"
                                          : "disabled by policy",
                      0);
    set_service_state(3,
                      services[3].enabled ? "running" : "stopped",
                      services[3].enabled ? "desktop taskbar switch enabled"
                                          : "disabled by policy",
                      0);
    if (!services[4].enabled) {
        ntp_attempted = 0;
        ntp_last_result_ok = 0;
        set_service_state(4, "stopped", "disabled by policy", 0);
    }
}

static void update_ntp(unsigned long now)
{
    struct leonos_time_sync sync;
    char detail[SERVICE_DETAIL_LEN];
    uint32_t pos = 0;
    int ret;
    if (!services[4].enabled) {
        return;
    }
    if (ntp_attempted &&
        now - last_ntp_attempt_ms < (ntp_last_result_ok ? NTP_SUCCESS_INTERVAL_MS
                                                        : NTP_FAILURE_RETRY_MS)) {
        return;
    }
    last_ntp_attempt_ms = now;
    ntp_attempted = 1;
    sync = (struct leonos_time_sync){0};
    ret = leonos_time_ntp_sync(NTP_TIMEOUT_MS, &sync);
    if (ret == 0 && sync.status == LEONOS_NET_STATUS_OK && sync.valid) {
        ntp_last_result_ok = 1;
        detail[0] = 0;
        append_text(detail, &pos, sizeof(detail), "synced from ");
        append_text(detail, &pos, sizeof(detail), sync.server);
        set_service_state(4, "running", detail, (uint32_t)getpid());
        log_line("NTP clock synchronized");
        return;
    }
    ntp_last_result_ok = 0;
    detail[0] = 0;
    append_text(detail, &pos, sizeof(detail), ret < 0 ? "NTP permission failure: " : "NTP ");
    append_text(detail, &pos, sizeof(detail), net_status_name(sync.status));
    set_service_state(4, "failed", detail, (uint32_t)getpid());
    log_line("NTP clock synchronization failed");
}

static void update_services(void)
{
    unsigned long now = leonos_uptime_ms();
    update_simple_services();
    update_dhcp(now);
    update_ntp(now);
}

static void write_state(void)
{
    char state[SERVICE_STATE_MAX];
    uint32_t pos = 0;
    state[0] = 0;
    append_text(state, &pos, sizeof(state), "# leonos-services-v1\n");
    for (uint32_t i = 0; i < SERVICE_COUNT; ++i) {
        append_text(state, &pos, sizeof(state), services[i].key);
        append_char(state, &pos, sizeof(state), '|');
        append_text(state, &pos, sizeof(state), services[i].state);
        append_char(state, &pos, sizeof(state), '|');
        append_u32(state, &pos, sizeof(state), services[i].pid);
        append_char(state, &pos, sizeof(state), '|');
        append_text(state, &pos, sizeof(state), services[i].detail);
        append_char(state, &pos, sizeof(state), '\n');
    }
    (void)write_file_text(SERVICE_STATE_PATH, state, pos, 0);
}

static void ensure_runtime_dirs(void)
{
    (void)mkdir("0:/var", 0);
    (void)mkdir("0:/var/run", 0);
    (void)mkdir("0:/var/log", 0);
}

int main(void)
{
    puts("[serviced.elf] service runtime starting");
    ensure_runtime_dirs();
    log_line("service runtime starting");
    load_config();
    update_services();
    write_state();
    for (;;) {
        load_config();
        process_commands();
        update_services();
        write_state();
        sleep_ms(1000);
    }
}

#include "desktop.h"

static uint32_t service_key_len(const char *text)
{
    uint32_t len = 0;
    while (text && text[len]) {
        ++len;
    }
    return len;
}

static int service_line_matches(const char *line, uint32_t len,
                                const char *key, uint8_t *value)
{
    uint32_t key_len = service_key_len(key);
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

int desktop_load_service_config(void)
{
    char cfg[SERVICES_CONFIG_MAX];
    uint32_t len = 0;
    uint32_t pos = 0;
    uint8_t network_icon = 1;
    uint8_t rtc_clock = 1;
    int changed;
    int fd = open(SERVICES_CONFIG_PATH, LEONOS_O_RDONLY, 0);
    if (fd >= 0) {
        while (len + 1U < sizeof(cfg)) {
            long got = read(fd, cfg + len, sizeof(cfg) - len - 1U);
            if (got < 0) {
                break;
            }
            if (got == 0) {
                break;
            }
            len += (uint32_t)got;
        }
        close(fd);
    }
    cfg[len] = 0;
    while (pos < len) {
        uint32_t start = pos;
        uint32_t line_len;
        uint8_t value = 0;
        while (pos < len && cfg[pos] != '\n' && cfg[pos] != '\r') {
            ++pos;
        }
        line_len = pos - start;
        while (pos < len && (cfg[pos] == '\n' || cfg[pos] == '\r')) {
            ++pos;
        }
        if (service_line_matches(cfg + start, line_len,
                                 "network_icon", &value)) {
            network_icon = value;
        } else if (service_line_matches(cfg + start, line_len,
                                        "rtc_clock", &value)) {
            rtc_clock = value;
        }
    }
    changed = desktop_service_network_icon != network_icon ||
              desktop_service_rtc_clock != rtc_clock;
    desktop_service_network_icon = network_icon;
    desktop_service_rtc_clock = rtc_clock;
    return changed;
}

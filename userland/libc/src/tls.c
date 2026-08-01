#include <leonos/fs.h>
#include <leonos/gui.h>
#include <leonos/net.h>
#include <leonos/system.h>
#include <leonos/syscall.h>
#include <leonos/tls.h>

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/entropy_poll.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/platform_util.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>

#include <string.h>

#define LEONOS_TLS_CA_BUNDLE "0:/system/certs/cacert.pem"
#define LEONOS_TLS_CA_BUNDLE_MAX (512U * 1024U)

struct leonos_tls_io {
    int socket;
    uint32_t timeout_ms;
    leonos_tls_stream_callback activity;
    void *activity_context;
};

static mbedtls_x509_crt leonos_tls_roots;
static int leonos_tls_roots_state;

static int leonos_tls_is_leap_year(int year)
{
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

int64_t leonos_mbedtls_time(int64_t *seconds)
{
    struct leonos_time_info info;
    int64_t value = 0;
    if (leonos_time_info(&info) == 0 && info.valid) {
        value = (int64_t)info.unix_seconds;
    }
    if (seconds) {
        *seconds = value;
    }
    return value;
}

struct tm *mbedtls_platform_gmtime_r(const int64_t *seconds, struct tm *out)
{
    static const int month_days[] = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31,
    };
    int64_t days;
    int64_t remainder;
    int year = 1970;
    int month = 0;
    if (!seconds || !out || *seconds < 0) {
        return 0;
    }
    days = *seconds / 86400;
    remainder = *seconds % 86400;
    while (days >= (leonos_tls_is_leap_year(year) ? 366 : 365)) {
        days -= leonos_tls_is_leap_year(year) ? 366 : 365;
        ++year;
    }
    while (month < 12) {
        int count = month_days[month];
        if (month == 1 && leonos_tls_is_leap_year(year)) {
            ++count;
        }
        if (days < count) {
            break;
        }
        days -= count;
        ++month;
    }
    out->tm_sec = (int)(remainder % 60);
    out->tm_min = (int)((remainder / 60) % 60);
    out->tm_hour = (int)(remainder / 3600);
    out->tm_mday = (int)days + 1;
    out->tm_mon = month;
    out->tm_year = year - 1900;
    out->tm_wday = (int)(((*seconds / 86400) + 4) % 7);
    out->tm_yday = (int)((*seconds / 86400) -
                         ((int64_t)(year - 1970) * 365));
    for (int current = 1970; current < year; ++current) {
        if (leonos_tls_is_leap_year(current)) {
            --out->tm_yday;
        }
    }
    out->tm_isdst = 0;
    return out;
}

static int leonos_tls_rdrand_available(void)
{
    uint32_t eax = 1;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
    __asm__ volatile("cpuid"
                     : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                     : "c"(0));
    (void)ebx;
    (void)edx;
    return (ecx & (1U << 30)) != 0;
}

static int leonos_tls_rdrand64(uint64_t *value)
{
    unsigned char ready;
    __asm__ volatile("rdrand %0; setc %1"
                     : "=r"(*value), "=qm"(ready));
    return ready != 0;
}

int mbedtls_hardware_poll(void *data, unsigned char *output,
                          size_t len, size_t *olen)
{
    size_t written = 0;
    (void)data;
    if (olen) {
        *olen = 0;
    }
    if (!output || !olen || !leonos_tls_rdrand_available()) {
        return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
    }
    while (written < len) {
        uint64_t random = 0;
        int ready = 0;
        for (int attempt = 0; attempt < 10 && !ready; ++attempt) {
            ready = leonos_tls_rdrand64(&random);
        }
        if (!ready) {
            return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
        }
        for (uint32_t index = 0; index < sizeof(random) && written < len; ++index) {
            output[written++] = (unsigned char)(random >> (index * 8U));
        }
    }
    *olen = written;
    return 0;
}

static int leonos_tls_load_roots(void)
{
    struct leonos_stat stat_info;
    unsigned char *pem;
    uint32_t length = 0;
    int fd;
    int ret;
    if (leonos_tls_roots_state != 0) {
        return leonos_tls_roots_state > 0 ? 0 : -1;
    }
    leonos_tls_roots_state = -1;
    if (stat(LEONOS_TLS_CA_BUNDLE, &stat_info) < 0 ||
        stat_info.type != LEONOS_FS_TYPE_FILE || stat_info.size == 0 ||
        stat_info.size > LEONOS_TLS_CA_BUNDLE_MAX) {
        return -1;
    }
    pem = malloc((size_t)stat_info.size + 1U);
    if (!pem) {
        return -1;
    }
    fd = open(LEONOS_TLS_CA_BUNDLE, LEONOS_O_RDONLY, 0);
    if (fd < 0) {
        free(pem);
        return -1;
    }
    while ((uint64_t)length < stat_info.size) {
        long got = read(fd, pem + length, (size_t)(stat_info.size - length));
        if (got <= 0) {
            close(fd);
            mbedtls_platform_zeroize(pem, (size_t)stat_info.size + 1U);
            free(pem);
            return -1;
        }
        length += (uint32_t)got;
    }
    close(fd);
    pem[length] = 0;
    mbedtls_x509_crt_init(&leonos_tls_roots);
    ret = mbedtls_x509_crt_parse(&leonos_tls_roots, pem, (size_t)length + 1U);
    mbedtls_platform_zeroize(pem, (size_t)length + 1U);
    free(pem);
    if (ret < 0 || leonos_tls_roots.version == 0) {
        mbedtls_x509_crt_free(&leonos_tls_roots);
        return -1;
    }
    leonos_tls_roots_state = 1;
    return 0;
}

static int leonos_tls_send(void *context, const unsigned char *buffer,
                           size_t length)
{
    struct leonos_tls_io *io = (struct leonos_tls_io *)context;
    uint32_t status = LEONOS_NET_STATUS_TCP_FAILED;
    long sent = leonos_socket_send(io->socket, buffer, (uint32_t)length,
                                   io->timeout_ms, &status);
    if (sent < 0 || status != LEONOS_NET_STATUS_OK) {
        return MBEDTLS_ERR_NET_SEND_FAILED;
    }
    return (int)sent;
}

static int leonos_tls_recv(void *context, unsigned char *buffer,
                            size_t length)
{
    struct leonos_tls_io *io = (struct leonos_tls_io *)context;
    uint32_t status = LEONOS_NET_STATUS_TCP_FAILED;
    unsigned long started = leonos_uptime_ms();
    for (;;) {
        uint32_t wait_ms = io->timeout_ms;
        long received;
        if (io->activity) {
            unsigned long elapsed = leonos_uptime_ms() - started;
            if (elapsed >= io->timeout_ms ||
                io->activity(0, 0, io->activity_context) < 0) {
                return MBEDTLS_ERR_NET_RECV_FAILED;
            }
            wait_ms = io->timeout_ms - (uint32_t)elapsed;
            if (wait_ms > 200U) {
                wait_ms = 200U;
            }
        }
        received = leonos_socket_recv(io->socket, buffer, (uint32_t)length,
                                      wait_ms, &status);
        if (received == 0 && status == LEONOS_NET_STATUS_TCP_TIMEOUT &&
            io->activity) {
            continue;
        }
        if (received == 0 && status == LEONOS_NET_STATUS_OK) {
            return 0;
        }
        if (received < 0 || status != LEONOS_NET_STATUS_OK) {
            return MBEDTLS_ERR_NET_RECV_FAILED;
        }
        return (int)received;
    }
}

static int leonos_tls_write_all(mbedtls_ssl_context *ssl,
                                 const unsigned char *data, uint32_t length)
{
    uint32_t written = 0;
    while (written < length) {
        int ret = mbedtls_ssl_write(ssl, data + written, length - written);
        if (ret <= 0) {
            return -1;
        }
        written += (uint32_t)ret;
    }
    return 0;
}

int leonos_tls_http_exchange(int socket, const char *hostname,
                             uint32_t timeout_ms,
                             const void *request_headers,
                             uint32_t request_headers_len,
                             const void *request_body,
                             uint32_t request_body_len,
                             char *response, uint32_t response_capacity,
                             uint32_t *response_len)
{
    struct leonos_time_info time_info;
    struct leonos_tls_io io;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config config;
    mbedtls_ctr_drbg_context drbg;
    mbedtls_entropy_context entropy;
    uint32_t received = 0;
    int ret = -1;
    if (response_len) {
        *response_len = 0;
    }
    if (!hostname || !hostname[0] || !request_headers || !request_headers_len ||
        !response || response_capacity < 2U || !response_len ||
        leonos_time_info(&time_info) < 0 || !time_info.valid ||
        leonos_tls_load_roots() < 0) {
        return -1;
    }
    io.socket = socket;
    io.timeout_ms = timeout_ms;
    io.activity = 0;
    io.activity_context = 0;
    mbedtls_ssl_init(&ssl);
    mbedtls_ssl_config_init(&config);
    mbedtls_ctr_drbg_init(&drbg);
    mbedtls_entropy_init(&entropy);
    if (mbedtls_ctr_drbg_seed(&drbg, mbedtls_entropy_func, &entropy,
                              (const unsigned char *)"LeonOS TLS", 10) != 0 ||
        mbedtls_ssl_config_defaults(&config, MBEDTLS_SSL_IS_CLIENT,
                                    MBEDTLS_SSL_TRANSPORT_STREAM,
                                    MBEDTLS_SSL_PRESET_DEFAULT) != 0) {
        goto cleanup;
    }
    mbedtls_ssl_conf_min_version(&config, MBEDTLS_SSL_MAJOR_VERSION_3,
                                 MBEDTLS_SSL_MINOR_VERSION_3);
    mbedtls_ssl_conf_max_version(&config, MBEDTLS_SSL_MAJOR_VERSION_3,
                                 MBEDTLS_SSL_MINOR_VERSION_3);
    mbedtls_ssl_conf_authmode(&config, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_ca_chain(&config, &leonos_tls_roots, 0);
    mbedtls_ssl_conf_rng(&config, mbedtls_ctr_drbg_random, &drbg);
    if (mbedtls_ssl_setup(&ssl, &config) != 0 ||
        mbedtls_ssl_set_hostname(&ssl, hostname) != 0) {
        goto cleanup;
    }
    mbedtls_ssl_set_bio(&ssl, &io, leonos_tls_send, leonos_tls_recv, 0);
    if (mbedtls_ssl_handshake(&ssl) != 0 ||
        mbedtls_ssl_get_verify_result(&ssl) != 0 ||
        leonos_tls_write_all(&ssl, request_headers, request_headers_len) < 0 ||
        (request_body_len &&
         leonos_tls_write_all(&ssl, request_body, request_body_len) < 0)) {
        goto cleanup;
    }
    while (received + 1U < response_capacity) {
        int got = mbedtls_ssl_read(&ssl, (unsigned char *)response + received,
                                   response_capacity - received - 1U);
        if (got == 0 || got == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
            break;
        }
        if (got < 0 && received == 0) {
            goto cleanup;
        }
        if (got < 0) {
            break;
        }
        received += (uint32_t)got;
    }
    response[received] = 0;
    *response_len = received;
    ret = 0;

cleanup:
    mbedtls_ssl_close_notify(&ssl);
    mbedtls_ssl_free(&ssl);
    mbedtls_ssl_config_free(&config);
    mbedtls_ctr_drbg_free(&drbg);
    mbedtls_entropy_free(&entropy);
    return ret;
}

int leonos_tls_http_stream(int socket, const char *hostname,
                           uint32_t timeout_ms,
                           const void *request_headers,
                           uint32_t request_headers_len,
                           const void *request_body,
                           uint32_t request_body_len,
                           leonos_tls_stream_callback callback,
                           void *context)
{
    struct leonos_time_info time_info;
    struct leonos_tls_io io;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config config;
    mbedtls_ctr_drbg_context drbg;
    mbedtls_entropy_context entropy;
    unsigned char buffer[4096];
    int ret = -1;
    if (!hostname || !hostname[0] || !request_headers || !request_headers_len ||
        !callback || leonos_time_info(&time_info) < 0 || !time_info.valid ||
        leonos_tls_load_roots() < 0) {
        return -1;
    }
    io.socket = socket;
    io.timeout_ms = timeout_ms;
    io.activity = callback;
    io.activity_context = context;
    mbedtls_ssl_init(&ssl);
    mbedtls_ssl_config_init(&config);
    mbedtls_ctr_drbg_init(&drbg);
    mbedtls_entropy_init(&entropy);
    if (mbedtls_ctr_drbg_seed(&drbg, mbedtls_entropy_func, &entropy,
                              (const unsigned char *)"LeonOS TLS", 10) != 0 ||
        mbedtls_ssl_config_defaults(&config, MBEDTLS_SSL_IS_CLIENT,
                                    MBEDTLS_SSL_TRANSPORT_STREAM,
                                    MBEDTLS_SSL_PRESET_DEFAULT) != 0) {
        goto cleanup;
    }
    mbedtls_ssl_conf_min_version(&config, MBEDTLS_SSL_MAJOR_VERSION_3,
                                 MBEDTLS_SSL_MINOR_VERSION_3);
    mbedtls_ssl_conf_max_version(&config, MBEDTLS_SSL_MAJOR_VERSION_3,
                                 MBEDTLS_SSL_MINOR_VERSION_3);
    mbedtls_ssl_conf_authmode(&config, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_ca_chain(&config, &leonos_tls_roots, 0);
    mbedtls_ssl_conf_rng(&config, mbedtls_ctr_drbg_random, &drbg);
    if (mbedtls_ssl_setup(&ssl, &config) != 0 ||
        mbedtls_ssl_set_hostname(&ssl, hostname) != 0) {
        goto cleanup;
    }
    mbedtls_ssl_set_bio(&ssl, &io, leonos_tls_send, leonos_tls_recv, 0);
    if (mbedtls_ssl_handshake(&ssl) != 0 ||
        mbedtls_ssl_get_verify_result(&ssl) != 0 ||
        leonos_tls_write_all(&ssl, request_headers, request_headers_len) < 0 ||
        (request_body_len &&
         leonos_tls_write_all(&ssl, request_body, request_body_len) < 0)) {
        goto cleanup;
    }
    for (;;) {
        int got = mbedtls_ssl_read(&ssl, buffer, sizeof(buffer));
        if (got == 0 || got == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
            ret = 0;
            break;
        }
        if (got < 0 || callback(buffer, (uint32_t)got, context) < 0) {
            break;
        }
    }

cleanup:
    mbedtls_ssl_close_notify(&ssl);
    mbedtls_ssl_free(&ssl);
    mbedtls_ssl_config_free(&config);
    mbedtls_ctr_drbg_free(&drbg);
    mbedtls_entropy_free(&entropy);
    return ret;
}

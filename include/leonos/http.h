#ifndef LEONOS_HTTP_H
#define LEONOS_HTTP_H

#include <leonos/fs.h>
#include <leonos/net.h>
#include <stdint.h>

#define LEONOS_HTTP_URL_LEN LEONOS_FS_PATH_LEN
#define LEONOS_HTTP_CONTENT_TYPE_LEN 64U
#define LEONOS_HTTP_DEFAULT_TIMEOUT_MS 10000U
#define LEONOS_HTTP_DEFAULT_REDIRECTS 5U
#define LEONOS_HTTP_HEADER_MAX 2048U
#define LEONOS_HTTP_BODY_MAX 8192U

#define LEONOS_HTTP_FLAG_TRUNCATED 0x00000001U
#define LEONOS_HTTP_FLAG_CHUNKED 0x00000002U
#define LEONOS_HTTP_FLAG_REDIRECTED 0x00000004U
#define LEONOS_HTTP_FLAG_CONTENT_LENGTH 0x00000008U

struct leonos_http_request {
    const char *url;
    const char *method;
    const char *extra_headers;
    const char *request_body;
    uint32_t request_body_len;
    uint32_t timeout_ms;
    uint32_t max_redirects;
    char *response_body;
    uint32_t response_body_capacity;
    char *response_headers;
    uint32_t response_headers_capacity;
};

struct leonos_http_response {
    uint32_t net_status;
    uint32_t http_status;
    uint32_t flags;
    uint32_t body_len;
    uint32_t headers_len;
    uint32_t content_length;
    uint32_t redirect_count;
    char content_type[LEONOS_HTTP_CONTENT_TYPE_LEN];
    char final_url[LEONOS_HTTP_URL_LEN];
};

int leonos_http_request(const struct leonos_http_request *request,
                        struct leonos_http_response *response);
int leonos_http_get(const char *url, uint32_t timeout_ms,
                    char *response_body, uint32_t response_body_capacity,
                    char *response_headers, uint32_t response_headers_capacity,
                    struct leonos_http_response *response);
int leonos_http_resolve_url(const char *base_url, const char *location,
                            char *out, uint32_t capacity);

#endif

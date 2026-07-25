#ifndef LEONOS_TLS_H
#define LEONOS_TLS_H

#include <stdint.h>

int leonos_tls_http_exchange(int socket, const char *hostname,
                             uint32_t timeout_ms,
                             const void *request_headers,
                             uint32_t request_headers_len,
                             const void *request_body,
                             uint32_t request_body_len,
                             char *response, uint32_t response_capacity,
                             uint32_t *response_len);

#endif

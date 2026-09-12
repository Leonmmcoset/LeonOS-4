/* Historical regression fixture only. Never build or stage into LeonOS. */
/* Internal authd client plumbing shared by the auth, sudo and fileop clients.
 *
 * Not a public ABI header: it lives in the libc implementation so the socket
 * framing, connect/HELLO handshake and reply pump exist exactly once. The
 * public entry points stay in <leonos/auth.h> and <leonos/sudo.h>.
 */
#ifndef LEONOS_AUTHD_CLIENT_INTERNAL_H
#define LEONOS_AUTHD_CLIENT_INTERNAL_H

#include <stdint.h>

/* Connect and complete the HELLO handshake, or return -1 with errno set. The
 * descriptor is cached for the calling process. */
int leonos_authd_client_open(void);
int leonos_authd_client_open_private(void);
int leonos_authd_client_wait_fd(int fd, uint32_t expected, void *payload,
                              uint32_t capacity, uint32_t *length, int *received_fd);

/* Read frames until one of the expected type arrives, or the deadline passes.
 * A negative status in an ACK frame is converted to errno. */
int leonos_authd_client_wait(int fd, uint32_t expected, void *payload,
                             uint32_t capacity, uint32_t *length);

/* Drop the cached descriptor. Used after a credential transition, because the
 * daemon records the peer uid at accept time. */
void leonos_authd_client_close(void);

#endif

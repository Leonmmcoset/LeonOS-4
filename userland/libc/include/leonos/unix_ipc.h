#ifndef LEONOS_UNIX_IPC_H
#define LEONOS_UNIX_IPC_H

#include <stdint.h>
#include <sys/socket.h>

#define LEONOS_IPC_MAGIC 0x554e4c4cU /* 'LNXU' */
#define LEONOS_IPC_VERSION 1U

#define LEONOS_IPC_SOCK_WINDOWD "/run/leonos/windowd.sock"
#define LEONOS_IPC_SOCK_INPUT_METHOD "/run/leonos/input-method.sock"
#define LEONOS_IPC_SOCK_NET "/run/leonos/net.sock"
#define LEONOS_IPC_SOCK_AUTH "/run/leonos/authd.sock"
#define LEONOS_IPC_SOCK_SESSION "/run/leonos/session.sock"
#define LEONOS_IPC_SOCK_DEVICE "/run/leonos/devman.sock"

struct leonos_ipc_frame {
    uint32_t magic;
    uint32_t version;
    uint32_t length;
};

int leonos_ipc_connect(const char *path);
int leonos_ipc_bind_listen(const char *path, int backlog);
int leonos_ipc_accept(int listen_fd, struct ucred *peer);
int leonos_ipc_send(int fd, uint32_t type, const void *payload, uint32_t length);
int leonos_ipc_send_fd(int fd, uint32_t type, const void *payload,
                       uint32_t length, int send_fd);
int leonos_ipc_recv(int fd, uint32_t *type, void *payload, uint32_t capacity,
                    uint32_t *length);
int leonos_ipc_recv_fd(int fd, uint32_t *type, void *payload, uint32_t capacity,
                       uint32_t *length, int *received_fd);
int leonos_ipc_set_nonblock(int fd, int enabled);
int leonos_ipc_peer_credentials(int fd, struct ucred *credentials);
int leonos_ipc_close(int fd);

#endif

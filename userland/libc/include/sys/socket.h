#ifndef LEONOS_SYS_SOCKET_H
#define LEONOS_SYS_SOCKET_H

#include <sys/types.h>
#include <linux/socket.h>

int socket(int domain, int type, int protocol);
int connect(int fd, const struct sockaddr *address, socklen_t length);
int bind(int fd, const struct sockaddr *address, socklen_t length);
int listen(int fd, int backlog);
int accept(int fd, struct sockaddr *address, socklen_t *length);
int getsockname(int fd, struct sockaddr *address, socklen_t *length);
int getsockopt(int fd, int level, int option, void *value, socklen_t *length);
int setsockopt(int fd, int level, int option, const void *value, socklen_t length);
int shutdown(int fd, int how);
ssize_t send(int fd, const void *buffer, size_t length, int flags);
ssize_t recv(int fd, void *buffer, size_t length, int flags);
ssize_t sendto(int fd, const void *buffer, size_t length, int flags,
               const struct sockaddr *destination, socklen_t destination_length);
ssize_t recvfrom(int fd, void *buffer, size_t length, int flags,
                 struct sockaddr *source, socklen_t *source_length);
int socketpair(int domain, int type, int protocol, int socket_vector[2]);
int accept4(int fd, struct sockaddr *address, socklen_t *length, int flags);
ssize_t sendmsg(int fd, const struct msghdr *message, int flags);
ssize_t recvmsg(int fd, struct msghdr *message, int flags);

#endif

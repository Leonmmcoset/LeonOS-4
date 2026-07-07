#ifndef NTCLKS_NET_H
#define NTCLKS_NET_H

#include <leonos/net.h>
#include <ntclks/types.h>

struct task;

void net_init(void);
int net_is_ready(void);
int net_get_config(struct leonos_net_config *config);
int net_dhcp_renew(struct leonos_net_dhcp *request);
int net_ping(struct leonos_net_ping *request);
int net_dns_resolve(struct leonos_net_dns *request);
int net_http_get(struct leonos_net_http_get *request);
int net_socket_open(struct leonos_net_socket_open *request, uint32_t owner_pid,
                    uint32_t owner_uid);
int net_socket_connect(struct leonos_net_socket_connect *request, uint32_t owner_pid);
int net_socket_send(struct leonos_net_socket_io *request, uint32_t owner_pid);
int net_socket_recv(struct leonos_net_socket_io *request, uint32_t owner_pid);
int net_socket_close(struct leonos_net_socket_close *request, uint32_t owner_pid);
int net_connections(struct leonos_net_connection_list *request, const struct task *viewer);
void net_close_owner_sockets(uint32_t owner_pid);
void net_device_info(uint32_t *flags, uint64_t *mac_value, uint32_t *local_ip);

#endif

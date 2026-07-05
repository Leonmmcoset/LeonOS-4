#ifndef NTCLKS_NET_H
#define NTCLKS_NET_H

#include <leonos/net.h>
#include <ntclks/types.h>

void net_init(void);
int net_is_ready(void);
int net_get_config(struct leonos_net_config *config);
int net_dhcp_renew(struct leonos_net_dhcp *request);
int net_ping(struct leonos_net_ping *request);
int net_dns_resolve(struct leonos_net_dns *request);
int net_http_get(struct leonos_net_http_get *request);
void net_device_info(uint32_t *flags, uint64_t *mac_value, uint32_t *local_ip);

#endif

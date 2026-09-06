/* Versioned network-service SDK compatibility layer. The device channel is
 * gone; every request is forwarded to netmand over /run/leonos/net.sock. */
#include <leonos/net.h>
#include <leonos/net_service.h>
#include <errno.h>

int net_service_config(net_service_config_t *config)
{
    return leonos_net_config(config);
}

int net_service_get_dns_policy(net_service_dns_policy_t *result)
{
    return leonos_net_get_dns_policy(result);
}

int net_service_set_dns_policy(uint32_t mode, uint32_t custom_dns_ip,
                               net_service_dns_policy_t *result)
{
    return leonos_net_set_dns_policy(mode, custom_dns_ip, result);
}

int net_service_dhcp_renew(uint32_t timeout_ms, net_service_dhcp_t *result)
{
    return leonos_net_dhcp_renew(timeout_ms, result);
}

int net_service_ping(uint32_t target_ip, uint32_t timeout_ms,
                     net_service_ping_t *result)
{
    return leonos_net_ping(target_ip, timeout_ms, result);
}

int net_service_dns_resolve(const char *name, uint32_t timeout_ms,
                            net_service_dns_t *result)
{
    return leonos_net_dns_resolve(name, timeout_ms, result);
}

int net_service_connections(net_service_connection_info_t *entries,
                            uint32_t capacity, uint32_t *out_count)
{
    return leonos_net_connections(entries, capacity, out_count);
}

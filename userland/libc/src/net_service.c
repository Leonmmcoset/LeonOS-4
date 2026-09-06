/* Versioned network-service SDK: all requests go through /dev/net0. */
#include <leonos/device.h>
#include <leonos/net.h>
#include <leonos/net_service.h>
#include <leonos/syscall.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

static int net_service_device_fd(void)
{
    static int fd = -1;
    if (fd < 0) {
        fd = open(LEONOS_DEV_NET0, O_RDWR, 0);
    }
    return fd;
}

static int net_service_result(int fd, unsigned long request, void *arg)
{
    long result = syscall3(SYS_ioctl, fd, (long)request, (long)arg);
    if (result < 0) {
        errno = (int)-result;
        return -1;
    }
    return (int)result;
}

int net_service_config(net_service_config_t *config)
{
    if (!config) {
        errno = EINVAL;
        return -1;
    }
    return net_service_result(net_service_device_fd(), LEONOS_IOCTL_NET_CONFIG,
                              config);
}

int net_service_get_dns_policy(net_service_dns_policy_t *result)
{
    net_service_dns_policy_t query = {
        .mode = LEONOS_NET_DNS_MODE_QUERY,
        .status = LEONOS_NET_STATUS_BAD_ARGUMENT,
    };
    int ret;
    if (!result) {
        errno = EINVAL;
        return -1;
    }
    ret = net_service_result(net_service_device_fd(),
                             LEONOS_IOCTL_NET_DNS_POLICY, &query);
    *result = query;
    return ret;
}

int net_service_set_dns_policy(uint32_t mode, uint32_t custom_dns_ip,
                               net_service_dns_policy_t *result)
{
    net_service_dns_policy_t query = {
        .mode = mode,
        .custom_dns_ip = custom_dns_ip,
        .status = LEONOS_NET_STATUS_BAD_ARGUMENT,
    };
    int ret;
    if (!result) {
        errno = EINVAL;
        return -1;
    }
    ret = net_service_result(net_service_device_fd(),
                             LEONOS_IOCTL_NET_DNS_POLICY, &query);
    *result = query;
    return ret;
}

int net_service_dhcp_renew(uint32_t timeout_ms, net_service_dhcp_t *result)
{
    net_service_dhcp_t query = {
        .timeout_ms = timeout_ms,
        .status = LEONOS_NET_STATUS_DHCP_FAILED,
    };
    int ret;
    if (!result) {
        errno = EINVAL;
        return -1;
    }
    ret = net_service_result(net_service_device_fd(), LEONOS_IOCTL_NET_DHCP,
                             &query);
    *result = query;
    return ret;
}

int net_service_ping(uint32_t target_ip, uint32_t timeout_ms,
                     net_service_ping_t *result)
{
    net_service_ping_t query = {
        .target_ip = target_ip,
        .timeout_ms = timeout_ms,
        .status = LEONOS_NET_STATUS_BAD_ARGUMENT,
    };
    int ret;
    if (!result) {
        errno = EINVAL;
        return -1;
    }
    ret = net_service_result(net_service_device_fd(), LEONOS_IOCTL_NET_PING,
                             &query);
    *result = query;
    return ret;
}

int net_service_dns_resolve(const char *name, uint32_t timeout_ms,
                            net_service_dns_t *result)
{
    net_service_dns_t query;
    uint32_t i = 0;
    int ret;
    if (!name || !result) {
        errno = EINVAL;
        return -1;
    }
    query = (net_service_dns_t){0};
    query.timeout_ms = timeout_ms;
    query.status = LEONOS_NET_STATUS_DNS_FAILED;
    while (name[i] && i + 1U < sizeof(query.name)) {
        query.name[i] = name[i];
        ++i;
    }
    query.name[i] = 0;
    ret = net_service_result(net_service_device_fd(), LEONOS_IOCTL_NET_DNS,
                             &query);
    *result = query;
    return ret;
}

int net_service_connections(net_service_connection_info_t *entries,
                            uint32_t capacity, uint32_t *out_count)
{
    net_service_connection_list_t query = {
        .capacity = capacity,
        .count = 0,
        .entries = entries,
    };
    int ret;
    ret = net_service_result(net_service_device_fd(),
                             LEONOS_IOCTL_NET_CONNECTIONS, &query);
    if (out_count) {
        *out_count = query.count;
    }
    return ret;
}

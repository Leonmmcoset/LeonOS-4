/*
 * LeonOS kernel networking interface: declares sockets and packet services.
 * Defines the internal contract between network drivers and syscalls.
 */
#ifndef NTCLKS_NET_H
#define NTCLKS_NET_H

#include <leonos/net.h>
#include <leonos/system.h>
#include <ntclks/types.h>

struct task;

/**
 * @brief Probe and configure the network stack: load DNS policy, init the NIC, and try a DHCP lease.
 */
void net_init(void);
/**
 * @brief Return non-zero when a network interface is present and usable.
 */
int net_is_ready(void);
/**
 * @brief Copy the current interface configuration (IP, MAC, flags) into config; 0 on success.
 */
int net_get_config(struct leonos_net_config *config);
/**
 * @brief Read or change the DNS resolver mode and server; results and status are written back into request.
 */
int net_set_dns_policy(struct leonos_net_dns_policy *request);
/**
 * @brief Renew the DHCP lease, writing the resulting config and status back into request.
 */
int net_dhcp_renew(struct leonos_net_dhcp *request);
/**
 * @brief Send an ICMP echo request to the target and record sent/received/rtt_ms in request.
 */
int net_ping(struct leonos_net_ping *request);
/**
 * @brief Resolve a hostname to IPv4 addresses and fill request with results and status.
 */
int net_dns_resolve(struct leonos_net_dns *request);
/**
 * @brief Query the configured NTP server and fill request with the resulting time offset.
 */
int net_ntp_sync(struct leonos_time_sync *request);
/**
 * @brief Fetch a URL over HTTP and fill request with the status code and response body.
 */
int net_http_get(struct leonos_net_http_get *request);
/**
 * @brief Create a socket owned by owner_pid/owner_uid; returns the new fd or a negative error.
 */
int net_socket_open(struct leonos_net_socket_open *request, uint32_t owner_pid,
                    uint32_t owner_uid);
/**
 * @brief Connect the socket in request to its remote address; 0 on success.
 */
int net_socket_connect(struct leonos_net_socket_connect *request, uint32_t owner_pid);
/**
 * @brief Send the request's payload over the socket; returns bytes sent or a negative error.
 */
int net_socket_send(struct leonos_net_socket_io *request, uint32_t owner_pid);
/**
 * @brief Receive data into the request's buffer; returns bytes read or a negative error.
 */
int net_socket_recv(struct leonos_net_socket_io *request, uint32_t owner_pid);
/**
 * @brief Close the socket identified by request; 0 on success.
 */
int net_socket_close(struct leonos_net_socket_close *request, uint32_t owner_pid);
/**
 * @brief List the connections visible to viewer into request; 0 on success.
 */
int net_connections(struct leonos_net_connection_list *request, const struct task *viewer);
/**
 * @brief Close every socket owned by owner_pid (used when a process exits).
 */
void net_close_owner_sockets(uint32_t owner_pid);
/**
 * @brief Drop network state after the NIC driver is removed so callers stop using stale sockets.
 */
void net_driver_detached(void);
/**
 * @brief Report the NIC's presence/active flags, 48-bit MAC, and current local IPv4 address.
 */
void net_device_info(uint32_t *flags, uint64_t *mac_value, uint32_t *local_ip);

#endif

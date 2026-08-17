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
 * @brief Coordinates the net init operation.
 */
void net_init(void);
/**
 * @brief Coordinates the net is ready operation.
 * @return Result, status, or value defined by this API.
 */
int net_is_ready(void);
/**
 * @brief Coordinates the net get config operation.
 * @param config Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
int net_get_config(struct leonos_net_config *config);
/**
 * @brief Coordinates the net set dns policy operation.
 * @param request Request structure consumed and, where defined, updated by this operation.
 * @return Result, status, or value defined by this API.
 */
int net_set_dns_policy(struct leonos_net_dns_policy *request);
/**
 * @brief Coordinates the net dhcp renew operation.
 * @param request Request structure consumed and, where defined, updated by this operation.
 * @return Result, status, or value defined by this API.
 */
int net_dhcp_renew(struct leonos_net_dhcp *request);
/**
 * @brief Coordinates the net ping operation.
 * @param request Request structure consumed and, where defined, updated by this operation.
 * @return Result, status, or value defined by this API.
 */
int net_ping(struct leonos_net_ping *request);
/**
 * @brief Coordinates the net dns resolve operation.
 * @param request Request structure consumed and, where defined, updated by this operation.
 * @return Result, status, or value defined by this API.
 */
int net_dns_resolve(struct leonos_net_dns *request);
/**
 * @brief Coordinates the net ntp sync operation.
 * @param request Request structure consumed and, where defined, updated by this operation.
 * @return Result, status, or value defined by this API.
 */
int net_ntp_sync(struct leonos_time_sync *request);
/**
 * @brief Coordinates the net http get operation.
 * @param request Request structure consumed and, where defined, updated by this operation.
 * @return Result, status, or value defined by this API.
 */
int net_http_get(struct leonos_net_http_get *request);
/**
 * @brief Coordinates the net socket open operation.
 * @param request Request structure consumed and, where defined, updated by this operation.
 * @param owner_pid Input or output value used by this operation.
 * @param owner_uid Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
int net_socket_open(struct leonos_net_socket_open *request, uint32_t owner_pid,
                    uint32_t owner_uid);
/**
 * @brief Coordinates the net socket connect operation.
 * @param request Request structure consumed and, where defined, updated by this operation.
 * @param owner_pid Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
int net_socket_connect(struct leonos_net_socket_connect *request, uint32_t owner_pid);
/**
 * @brief Coordinates the net socket send operation.
 * @param request Request structure consumed and, where defined, updated by this operation.
 * @param owner_pid Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
int net_socket_send(struct leonos_net_socket_io *request, uint32_t owner_pid);
/**
 * @brief Coordinates the net socket recv operation.
 * @param request Request structure consumed and, where defined, updated by this operation.
 * @param owner_pid Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
int net_socket_recv(struct leonos_net_socket_io *request, uint32_t owner_pid);
/**
 * @brief Coordinates the net socket close operation.
 * @param request Request structure consumed and, where defined, updated by this operation.
 * @param owner_pid Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
int net_socket_close(struct leonos_net_socket_close *request, uint32_t owner_pid);
/**
 * @brief Coordinates the net connections operation.
 * @param request Request structure consumed and, where defined, updated by this operation.
 * @param viewer Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
int net_connections(struct leonos_net_connection_list *request, const struct task *viewer);
/**
 * @brief Coordinates the net close owner sockets operation.
 * @param owner_pid Input or output value used by this operation.
 */
void net_close_owner_sockets(uint32_t owner_pid);
/**
 * @brief Coordinates the net driver detached operation.
 */
void net_driver_detached(void);
/**
 * @brief Coordinates the net device info operation.
 * @param flags Input or output value used by this operation.
 * @param mac_value Input or output value used by this operation.
 * @param local_ip Input or output value used by this operation.
 */
void net_device_info(uint32_t *flags, uint64_t *mac_value, uint32_t *local_ip);

#endif

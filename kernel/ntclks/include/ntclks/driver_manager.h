/*
 * LeonOS driver-manager interface: declares driver registration and dispatch.
 * Defines the kernel-facing contract for device discovery and lifecycle.
 */
#ifndef NTCLKS_DRIVER_MANAGER_H
#define NTCLKS_DRIVER_MANAGER_H

#include <leonos/driver.h>
#include <leonos/audio.h>
#include <ntclks/mouse.h>
#include <ntclks/types.h>

void driver_manager_init(void);
void driver_manager_autoload(void);
int driver_manager_list(struct leonos_driver_list *query);
int driver_manager_control(struct leonos_driver_control *request);

void driver_manager_mouse_state(struct mouse_state *out);
uint32_t driver_manager_mouse_event_count(void);
uint8_t driver_manager_mouse_last_status(void);
uint8_t driver_manager_mouse_last_data(void);
uint8_t driver_manager_mouse_last_ack(void);
void driver_manager_mouse_poll(void);
int driver_manager_audio_configure(const struct leonos_audio_format *format);
long driver_manager_audio_write(const void *data, uint32_t length,
                                uint32_t *out_status);
void driver_manager_audio_get_state(struct leonos_audio_state *out);

#endif

#ifndef NTCLKS_DRIVER_MANAGER_H
#define NTCLKS_DRIVER_MANAGER_H

#include <leonos/driver.h>
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

#endif

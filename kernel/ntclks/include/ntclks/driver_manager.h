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

/**
 * @brief Coordinates the driver manager init operation.
 */
void driver_manager_init(void);
/**
 * @brief Coordinates the driver manager autoload operation.
 */
void driver_manager_autoload(void);
/**
 * @brief Coordinates the driver manager list operation.
 * @param query Request structure consumed and, where defined, updated by this operation.
 * @return Result, status, or value defined by this API.
 */
int driver_manager_list(struct leonos_driver_list *query);
/**
 * @brief Coordinates the driver manager control operation.
 * @param request Request structure consumed and, where defined, updated by this operation.
 * @return Result, status, or value defined by this API.
 */
int driver_manager_control(struct leonos_driver_control *request);

/**
 * @brief Coordinates the driver manager mouse state operation.
 * @param out Caller-provided storage that receives output from this operation.
 */
void driver_manager_mouse_state(struct mouse_state *out);
/**
 * @brief Coordinates the driver manager mouse event count operation.
 * @return Result, status, or value defined by this API.
 */
uint32_t driver_manager_mouse_event_count(void);
/**
 * @brief Coordinates the driver manager mouse last status operation.
 * @return Result, status, or value defined by this API.
 */
uint8_t driver_manager_mouse_last_status(void);
/**
 * @brief Coordinates the driver manager mouse last data operation.
 * @return Result, status, or value defined by this API.
 */
uint8_t driver_manager_mouse_last_data(void);
/**
 * @brief Coordinates the driver manager mouse last ack operation.
 * @return Result, status, or value defined by this API.
 */
uint8_t driver_manager_mouse_last_ack(void);
/**
 * @brief Coordinates the driver manager mouse poll operation.
 */
void driver_manager_mouse_poll(void);
/**
 * @brief Coordinates the driver manager audio configure operation.
 * @param format Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
int driver_manager_audio_configure(const struct leonos_audio_format *format);
/**
 * @brief Coordinates the driver manager audio write operation.
 * @param data Input or output value used by this operation.
 * @param length Length, size, or element count associated with the operation.
 * @param out_status Caller-provided storage that receives output from this operation.
 * @return Result, status, or value defined by this API.
 */
long driver_manager_audio_write(const void *data, uint32_t length,
                                uint32_t *out_status);
/**
 * @brief Coordinates the driver manager audio get state operation.
 * @param out Caller-provided storage that receives output from this operation.
 */
void driver_manager_audio_get_state(struct leonos_audio_state *out);

#endif

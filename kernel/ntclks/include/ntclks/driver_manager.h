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
 * @brief Initialize the driver manager and its driver/device registries.
 */
void driver_manager_init(void);
/**
 * @brief Discover and load the drivers marked for boot-time activation.
 */
void driver_manager_autoload(void);
/**
 * @brief Fill query with the registered drivers and set query->count; returns 0 or a negative errno.
 */
int driver_manager_list(struct leonos_driver_list *query);
/**
 * @brief Load, unload, or rescan the driver named in request, storing the outcome in request->status.
 */
int driver_manager_control(struct leonos_driver_control *request);

/**
 * @brief Copy the current mouse position and button state into out.
 */
void driver_manager_mouse_state(struct mouse_state *out);
/**
 * @brief Return how many mouse events are waiting in the queue.
 */
uint32_t driver_manager_mouse_event_count(void);
/**
 * @brief Return the last PS/2 mouse status byte received.
 */
uint8_t driver_manager_mouse_last_status(void);
/**
 * @brief Return the last PS/2 mouse data byte received.
 */
uint8_t driver_manager_mouse_last_data(void);
/**
 * @brief Return the last PS/2 mouse acknowledge byte received.
 */
uint8_t driver_manager_mouse_last_ack(void);
/**
 * @brief Service pending mouse events and refresh the shared mouse state.
 */
void driver_manager_mouse_poll(void);
/**
 * @brief Configure the audio device with the given sample format; returns status.
 */
int driver_manager_audio_configure(const struct leonos_audio_format *format);
/**
 * @brief Write length bytes of audio samples, reporting the device status in out_status.
 */
long driver_manager_audio_write(const void *data, uint32_t length,
                                uint32_t *out_status);
/**
 * @brief Copy the current audio device state into out.
 */
void driver_manager_audio_get_state(struct leonos_audio_state *out);

#endif

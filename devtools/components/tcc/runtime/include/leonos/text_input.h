#ifndef LEONOS_TEXT_INPUT_H
#define LEONOS_TEXT_INPUT_H

/* Versioned LeonOS text-input service SDK.
 *
 * Consumers talk to the imd daemon over /run/leonos/input-method.sock through
 * this library; the pre-migration /dev/input-method device and fd 3 channel
 * are gone. Wire structures are the versioned LeonOS input-method protocol.
 */
#include <leonos/inputm.h>
#include <stdint.h>

#define TEXT_INPUT_SERVICE_ABI_VERSION 1U

typedef struct leonos_inputm_provider text_input_provider_t;
typedef struct leonos_inputm_key_event text_input_key_event_t;
typedef struct leonos_inputm_result text_input_result_t;
typedef struct leonos_inputm_active_request text_input_active_request_t;
typedef struct leonos_inputm_config_request text_input_config_request_t;
typedef struct leonos_inputm_provider_list text_input_provider_list_t;
typedef struct leonos_inputm_context text_input_context_t;
typedef struct leonos_inputm_state text_input_state_t;

#define TEXT_INPUT_MAX_PROVIDERS LEONOS_INPUTM_MAX_PROVIDERS
#define TEXT_INPUT_MAX_CANDIDATES LEONOS_INPUTM_MAX_CANDIDATES
#define TEXT_INPUT_ID_LEN LEONOS_INPUTM_ID_LEN
#define TEXT_INPUT_NAME_LEN LEONOS_INPUTM_NAME_LEN
#define TEXT_INPUT_ABBREV_LEN LEONOS_INPUTM_ABBREV_LEN
#define TEXT_INPUT_TEXT_LEN LEONOS_INPUTM_TEXT_LEN

#define TEXT_INPUT_START_MANUAL LEONOS_INPUTM_START_MANUAL
#define TEXT_INPUT_START_LOGIN LEONOS_INPUTM_START_LOGIN
#define TEXT_INPUT_START_ON_DEMAND LEONOS_INPUTM_START_ON_DEMAND

#define TEXT_INPUT_CONTEXT_FOCUSED LEONOS_INPUTM_CONTEXT_FOCUSED
#define TEXT_INPUT_CONTEXT_SECURE LEONOS_INPUTM_CONTEXT_SECURE

#define TEXT_INPUT_RESULT_COMPOSITION LEONOS_INPUTM_RESULT_COMPOSITION
#define TEXT_INPUT_RESULT_COMMIT LEONOS_INPUTM_RESULT_COMMIT
#define TEXT_INPUT_RESULT_CANCEL LEONOS_INPUTM_RESULT_CANCEL
#define TEXT_INPUT_RESULT_PASSTHROUGH LEONOS_INPUTM_RESULT_PASSTHROUGH

#define TEXT_INPUT_RENDER_CONTROLS LEONOS_INPUTM_RENDER_CONTROLS
#define TEXT_INPUT_RENDER_PIXELS LEONOS_INPUTM_RENDER_PIXELS

int text_input_register(const text_input_provider_t *provider);
int text_input_unregister(void);
int text_input_provider_next(text_input_key_event_t *event);
int text_input_provider_result(const text_input_result_t *result);
int text_input_submit_key(uint32_t window_id, uint8_t keycode, uint8_t pressed);
int text_input_poll_result(text_input_result_t *result);
int text_input_set_active(uint32_t uid, const char *id);
int text_input_list(uint32_t uid, text_input_provider_t *providers,
                    uint32_t capacity, uint32_t *out_count);
int text_input_set_context(const text_input_context_t *context);
int text_input_get_state(uint32_t uid, text_input_state_t *state);
int text_input_notify_config(uint32_t uid);

/* GUI integration helpers. */
int text_input_observe_gui_key(uint32_t window_id, uint8_t *keycode,
                               uint8_t pressed);
int text_input_poll_gui_commit(uint32_t window_id);
int text_input_take_text(char *buffer, uint32_t capacity);
int text_input_take_key(uint8_t *keycode, uint8_t *pressed);
void text_input_note_gui_window(uint32_t window_id);
int text_input_set_current_context(uint32_t flags, int32_t caret_x,
                                   int32_t caret_y, uint32_t caret_w,
                                   uint32_t caret_h);

#endif

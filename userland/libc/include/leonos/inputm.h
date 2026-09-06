#ifndef LEONOS_INPUTM_H
#define LEONOS_INPUTM_H

#include <stdint.h>


#define LEONOS_INPUTM_MAX_PROVIDERS 8U
#define LEONOS_INPUTM_MAX_CANDIDATES 5U
#define LEONOS_INPUTM_ID_LEN 32U
#define LEONOS_INPUTM_NAME_LEN 64U
#define LEONOS_INPUTM_ABBREV_LEN 8U
#define LEONOS_INPUTM_TEXT_LEN 128U

#define LEONOS_INPUTM_START_MANUAL 0U
#define LEONOS_INPUTM_START_LOGIN 1U
#define LEONOS_INPUTM_START_ON_DEMAND 2U

#define LEONOS_INPUTM_CONTEXT_FOCUSED 0x00000001U
#define LEONOS_INPUTM_CONTEXT_SECURE 0x00000002U

#define LEONOS_INPUTM_RESULT_COMPOSITION 1U
#define LEONOS_INPUTM_RESULT_COMMIT 2U
#define LEONOS_INPUTM_RESULT_CANCEL 3U
#define LEONOS_INPUTM_RESULT_PASSTHROUGH 4U

#define LEONOS_INPUTM_RENDER_CONTROLS 0x00000001U
#define LEONOS_INPUTM_RENDER_PIXELS 0x00000002U

struct leonos_inputm_provider { char id[LEONOS_INPUTM_ID_LEN]; char name[LEONOS_INPUTM_NAME_LEN]; char abbreviation[LEONOS_INPUTM_ABBREV_LEN]; uint32_t startup_mode; uint32_t render_flags; uint32_t enabled; };
struct leonos_inputm_key_event { uint32_t sequence; uint32_t client_pid; uint32_t window_id; uint32_t context_flags; uint8_t keycode; uint8_t pressed; uint8_t reserved0; uint8_t reserved1; int32_t caret_x; int32_t caret_y; uint32_t caret_w; uint32_t caret_h; };
struct leonos_inputm_result { uint32_t sequence; uint32_t client_pid; uint32_t window_id; uint32_t type; char text[LEONOS_INPUTM_TEXT_LEN]; char candidates[LEONOS_INPUTM_MAX_CANDIDATES][LEONOS_INPUTM_TEXT_LEN]; uint32_t candidate_count; uint32_t selected_candidate; uint8_t keycode; uint8_t pressed; uint8_t reserved0; uint8_t reserved1; };
struct leonos_inputm_active_request { uint32_t uid; char id[LEONOS_INPUTM_ID_LEN]; };
struct leonos_inputm_config_request { uint32_t uid; };
struct leonos_inputm_provider_list { uint32_t uid; uint32_t capacity; uint32_t count; uint32_t reserved; struct leonos_inputm_provider *providers; };
struct leonos_inputm_context { uint32_t window_id; uint32_t flags; int32_t caret_x; int32_t caret_y; uint32_t caret_w; uint32_t caret_h; };
struct leonos_inputm_state { uint32_t uid; char active_id[LEONOS_INPUTM_ID_LEN]; char composition[LEONOS_INPUTM_TEXT_LEN]; char candidates[LEONOS_INPUTM_MAX_CANDIDATES][LEONOS_INPUTM_TEXT_LEN]; uint32_t candidate_count; uint32_t selected_candidate; uint32_t render_flags; uint32_t config_generation; uint32_t window_id; int32_t caret_x; int32_t caret_y; uint32_t caret_w; uint32_t caret_h; };

int leonos_inputm_register(const struct leonos_inputm_provider *provider);
int leonos_inputm_unregister(void);
int leonos_inputm_provider_next(struct leonos_inputm_key_event *event);
int leonos_inputm_provider_result(const struct leonos_inputm_result *result);
int leonos_inputm_submit_key(uint32_t window_id, uint8_t keycode, uint8_t pressed);
int leonos_inputm_poll_result(struct leonos_inputm_result *result);
int leonos_inputm_set_active(uint32_t uid, const char *id);
int leonos_inputm_list(uint32_t uid, struct leonos_inputm_provider *providers, uint32_t capacity, uint32_t *out_count);
int leonos_inputm_set_context(const struct leonos_inputm_context *context);
int leonos_inputm_get_state(uint32_t uid, struct leonos_inputm_state *state);
int leonos_inputm_notify_config(uint32_t uid);
int leonos_inputm_observe_gui_key(uint32_t window_id, uint8_t *keycode, uint8_t pressed);
int leonos_inputm_take_text(char *buffer, uint32_t capacity);
int leonos_inputm_take_key(uint8_t *keycode, uint8_t *pressed);
int leonos_inputm_poll_gui_commit(uint32_t window_id);
void leonos_inputm_note_gui_window(uint32_t window_id);
int leonos_inputm_set_current_context(uint32_t flags, int32_t caret_x, int32_t caret_y, uint32_t caret_w, uint32_t caret_h);

#endif

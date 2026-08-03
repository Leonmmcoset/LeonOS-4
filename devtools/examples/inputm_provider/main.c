#include <leonos/inputm.h>
#include <leonos/gui.h>
#include <leonos/syscall.h>

static void copy_text(char *dst, unsigned capacity, const char *src)
{
    unsigned i = 0;
    while (src && src[i] && i + 1U < capacity) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = 0;
}

int main(void)
{
    struct leonos_inputm_provider provider = {0};
    struct leonos_inputm_key_event event = {0};
    copy_text(provider.id, sizeof(provider.id), "imtest");
    copy_text(provider.name, sizeof(provider.name), "InputM test provider");
    copy_text(provider.abbreviation, sizeof(provider.abbreviation), "TEST");
    provider.startup_mode = LEONOS_INPUTM_START_MANUAL;
    provider.render_flags = LEONOS_INPUTM_RENDER_CONTROLS;
    provider.enabled = 1;
    if (leonos_inputm_register(&provider) <= 0) {
        return 1;
    }
    for (;;) {
        if (leonos_inputm_provider_next(&event) > 0) {
            struct leonos_inputm_result result = {0};
            result.sequence = event.sequence;
            result.client_pid = event.client_pid;
            result.window_id = event.window_id;
            if (event.pressed && event.keycode == LEONOS_KEY_SPACE) {
                result.type = LEONOS_INPUTM_RESULT_COMMIT;
                copy_text(result.text, sizeof(result.text), "InputM test");
            } else {
                result.type = LEONOS_INPUTM_RESULT_PASSTHROUGH;
            }
            (void)leonos_inputm_provider_result(&result);
            event = (struct leonos_inputm_key_event){0};
        } else {
            sleep_ms(8);
        }
    }
}

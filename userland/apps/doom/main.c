#include <leonos/gui.h>
#include <leonos/mouse.h>
#include <leonos/syscall.h>
#include <leonos/ui.h>
#include <stdint.h>

#include "doomgeneric.h"
#include "doomkeys.h"
#include "i_system.h"
#include "m_argv.h"

#define DOOM_KEY_QUEUE_CAP 64U
#define DOOM_WINDOW_WIDTH DOOMGENERIC_RESX
#define DOOM_WINDOW_HEIGHT DOOMGENERIC_RESY

struct doom_key_event {
    int pressed;
    unsigned char key;
};

static struct doom_key_event key_queue[DOOM_KEY_QUEUE_CAP];
static uint32_t key_read;
static uint32_t key_write;
static uint32_t window_id;
static uint32_t frame[DOOM_WINDOW_WIDTH * DOOM_WINDOW_HEIGHT];
static struct leonos_ui_surface ui;

static void restore_mouse(void)
{
    if (window_id) {
        leonos_mouse_show(window_id);
    }
}

static unsigned char doom_key(uint8_t keycode)
{
    switch (keycode) {
    case 1: return KEY_ESCAPE;
    case 14: return KEY_BACKSPACE;
    case 15: return KEY_TAB;
    case 28: return KEY_ENTER;
    case 29: return KEY_FIRE;
    case 42:
    case 54: return KEY_RSHIFT;
    case 56:
    case 115: return KEY_RALT;
    case 57: return KEY_USE;
    case 59: return KEY_F1;
    case 60: return KEY_F2;
    case 61: return KEY_F3;
    case 62: return KEY_F4;
    case 63: return KEY_F5;
    case 64: return KEY_F6;
    case 65: return KEY_F7;
    case 66: return KEY_F8;
    case 67: return KEY_F9;
    case 68: return KEY_F10;
    case 87: return KEY_F11;
    case 88: return KEY_F12;
    case 72:
    case 103: return KEY_UPARROW;
    case 75:
    case 105: return KEY_LEFTARROW;
    case 77:
    case 106: return KEY_RIGHTARROW;
    case 80:
    case 108: return KEY_DOWNARROW;
    case 116: return KEY_RCTRL;
    default:
        break;
    }
    if (keycode >= 2 && keycode <= 11) {
        return (unsigned char)(keycode == 11 ? '0' : '1' + keycode - 2);
    }
    if (keycode == 12) return '-';
    if (keycode == 13) return '=';
    if (keycode >= 16 && keycode <= 25) {
        static const char keys[] = "qwertyuiop";
        return (unsigned char)keys[keycode - 16];
    }
    if (keycode >= 30 && keycode <= 38) {
        static const char keys[] = "asdfghjkl";
        return (unsigned char)keys[keycode - 30];
    }
    if (keycode >= 44 && keycode <= 50) {
        static const char keys[] = "zxcvbnm";
        return (unsigned char)keys[keycode - 44];
    }
    switch (keycode) {
    case 26: return '[';
    case 27: return ']';
    case 39: return ';';
    case 40: return '\'';
    case 41: return '`';
    case 43: return '\\';
    case 51: return ',';
    case 52: return '.';
    case 53: return '/';
    default: return 0;
    }
}

static void queue_key(uint8_t keycode, uint8_t pressed)
{
    unsigned char key = doom_key(keycode);
    uint32_t next;
    if (!key) {
        return;
    }
    next = (key_write + 1U) % DOOM_KEY_QUEUE_CAP;
    if (next == key_read) {
        key_read = (key_read + 1U) % DOOM_KEY_QUEUE_CAP;
    }
    key_queue[key_write].pressed = pressed != 0;
    key_queue[key_write].key = key;
    key_write = next;
}

static void pump_events(void)
{
    struct leonos_gui_app_event event = {.window_id = window_id};
    while (leonos_gui_poll_app_event(&event) > 0) {
        if (event.type == LEONOS_GUI_APP_EVENT_CLOSE) {
            leonos_mouse_show(window_id);
            leonos_gui_destroy_app_window(window_id);
            exit(0);
        }
        if (event.type == LEONOS_GUI_APP_EVENT_KEY_DOWN ||
            event.type == LEONOS_GUI_APP_EVENT_KEY_UP) {
            queue_key(event.keycode, event.pressed);
        }
        event.window_id = window_id;
    }
}

void DG_Init(void)
{
    uint32_t flags = LEONOS_GUI_WINDOW_FULLSCREEN;
    if (M_CheckParm("-windowed") > 0) {
        flags = LEONOS_GUI_WINDOW_NO_RESIZE;
    }
    window_id = (uint32_t)leonos_gui_create_app_window_ex("Doom", "DoomGeneric",
                                                             DOOM_WINDOW_WIDTH,
                                                             DOOM_WINDOW_HEIGHT,
                                                             flags);
    if (!window_id) {
        exit(1);
    }
    if (flags & LEONOS_GUI_WINDOW_FULLSCREEN) {
        leonos_mouse_hide(window_id);
    }
    I_AtExit(restore_mouse, true);
}

void DG_StartupProgress(uint32_t progress, const char *message)
{
    if (!window_id) {
        return;
    }
    if (progress > 100U) {
        progress = 100U;
    }
    leonos_ui_bind(&ui, frame, DOOM_WINDOW_WIDTH, DOOM_WINDOW_HEIGHT,
                   DOOM_WINDOW_WIDTH);
    leonos_ui_rect(&ui, 0, 0, DOOM_WINDOW_WIDTH, DOOM_WINDOW_HEIGHT, 0x00101814U);
    leonos_ui_rect(&ui, 64, 104, DOOM_WINDOW_WIDTH - 128U, 192, 0x001d2c25U);
    leonos_ui_text(&ui, 96, 140, "DOOM", 0x00f0f5edU, 0x001d2c25U);
    leonos_ui_text(&ui, 96, 184, message ? message : "Loading", 0x00f0f5edU,
                   0x001d2c25U);
    leonos_ui_progress(&ui, 96, 224, DOOM_WINDOW_WIDTH - 192U, 20, progress, 100);
    leonos_gui_present_window(window_id, DOOM_WINDOW_WIDTH, DOOM_WINDOW_HEIGHT,
                               DOOM_WINDOW_WIDTH, frame);
    sched_yield();
}

void DG_DrawFrame(void)
{
    if (!window_id || !DG_ScreenBuffer) {
        return;
    }
    leonos_gui_present_window(window_id, DOOM_WINDOW_WIDTH, DOOM_WINDOW_HEIGHT,
                               DOOM_WINDOW_WIDTH, DG_ScreenBuffer);
    pump_events();
}

void DG_SleepMs(uint32_t ms)
{
    pump_events();
    sleep_ms(ms);
}

uint32_t DG_GetTicksMs(void)
{
    return (uint32_t)leonos_uptime_ms();
}

int DG_GetKey(int *pressed, unsigned char *key)
{
    if (key_read == key_write) {
        return 0;
    }
    *pressed = key_queue[key_read].pressed;
    *key = key_queue[key_read].key;
    key_read = (key_read + 1U) % DOOM_KEY_QUEUE_CAP;
    return 1;
}

void DG_SetWindowTitle(const char *title)
{
    (void)title;
}

int main(int argc, char **argv, char **envp)
{
    static char *default_argv[] = {
        "doom.elf", "-iwad", "0:/programs/doom/freedoom1.wad", "-nosound", 0
    };
    (void)envp;
    if (argc <= 1 || !argv || !argv[0]) {
        argc = (int)(sizeof(default_argv) / sizeof(default_argv[0])) - 1;
        argv = default_argv;
    }
    doomgeneric_Create(argc, argv);
    for (;;) {
        doomgeneric_Tick();
    }
    leonos_mouse_show(window_id);
    leonos_gui_destroy_app_window(window_id);
    return 0;
}

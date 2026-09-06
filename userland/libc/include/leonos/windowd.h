#ifndef LEONOS_WINDOWD_H
#define LEONOS_WINDOWD_H

#include <leonos/gui.h>
#include <stdint.h>

enum leonos_windowd_msg {
    LEONOS_WIN_MSG_ERROR = 0,
    LEONOS_WIN_MSG_HELLO = 10,
    LEONOS_WIN_MSG_HELLO_ACK = 11,
    LEONOS_WIN_MSG_POLICY_HELLO = 12,
    LEONOS_WIN_MSG_CREATE = 20,
    LEONOS_WIN_MSG_CREATE_ACK = 21,
    LEONOS_WIN_MSG_DESTROY = 22,
    LEONOS_WIN_MSG_PRESENT = 23,
    LEONOS_WIN_MSG_UPDATE = 24,
    LEONOS_WIN_MSG_FETCH = 25,
    LEONOS_WIN_MSG_FETCH_ACK = 26,
    LEONOS_WIN_MSG_EVENT = 30,
    LEONOS_WIN_MSG_INPUT = 31,
    LEONOS_WIN_MSG_WINDOW_NOTIFY = 32,
    LEONOS_WIN_MSG_MOUSE_VISIBLE = 40,
    LEONOS_WIN_MSG_CURSOR_REQUEST = 41,
    LEONOS_WIN_MSG_CURSOR_REGION = 42,
    LEONOS_WIN_MSG_TASKBAR = 43,
    LEONOS_WIN_MSG_DISPLAY_STATE = 50,
    LEONOS_WIN_MSG_DISPLAY_REQUEST = 51,
    LEONOS_WIN_MSG_APPEARANCE_STATE = 52,
    LEONOS_WIN_MSG_APPEARANCE_REQUEST = 53,
};

#define LEONOS_WIN_POLICY_TOKEN "desktop-policy-v1"
#define LEONOS_WIN_ROLE_APP 1u
#define LEONOS_WIN_ROLE_POLICY 2u

struct leonos_win_hello {
    uint32_t pid;
    uint32_t role;
};

struct leonos_win_hello_ack {
    uint32_t version;
    uint32_t reserved;
};

struct leonos_win_policy_hello {
    uint32_t pid;
    uint32_t reserved;
    char token[32];
};

struct leonos_win_create {
    uint32_t width;
    uint32_t height;
    uint32_t flags;
    char title[48];
    char text[1024];
};

struct leonos_win_create_ack {
    uint32_t window_id;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
};

struct leonos_win_destroy {
    uint32_t window_id;
    uint32_t reserved;
};

struct leonos_win_present {
    uint32_t window_id;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
};

struct leonos_win_update {
    uint32_t window_id;
    uint32_t mask;
    uint32_t flags;
    char title[48];
};

struct leonos_win_fetch {
    uint32_t window_id;
    uint32_t capacity_width;
    uint32_t capacity_height;
    uint32_t stride;
};

struct leonos_win_fetch_ack {
    uint32_t window_id;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
};

struct leonos_win_taskbar {
    uint32_t window_id;
    uint32_t visible;
};

struct leonos_win_mouse_visible {
    uint32_t window_id;
    uint32_t visible;
};

struct leonos_win_error {
    int32_t code;
    uint32_t reserved;
};

#endif

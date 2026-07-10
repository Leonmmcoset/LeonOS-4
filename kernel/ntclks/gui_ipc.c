#include <ntclks/gui_ipc.h>
#include <ntclks/mm.h>
#include <ntclks/osmlayer.h>
#include <ntclks/sched.h>
#include <ntclks/usercopy.h>

#define GUI_IPC_QUEUE_CAP 128
#define GUI_IPC_MAX_WINDOWS 32
#define GUI_IPC_WINDOW_EVENT_CAP 32

static struct gui_ipc_window queue[GUI_IPC_QUEUE_CAP];
static uint32_t head;
static uint32_t tail;
static uint32_t next_window_id;
static struct gui_ipc_display_request display_request;
static uint8_t display_request_pending;
static struct gui_ipc_display_state display_state;
static uint8_t display_state_valid;
static struct gui_ipc_appearance_state appearance_state = {.theme = 1u};
static struct gui_ipc_appearance_request appearance_request;
static uint8_t appearance_request_pending;

struct gui_window_slot {
    uint8_t used;
    uint32_t id;
    uint32_t owner_pid;
    uint32_t width;
    uint32_t height;
    uint32_t flags;
    uint32_t buffer_pages;
    uint64_t buffer_phys;
    struct gui_ipc_app_event events[GUI_IPC_WINDOW_EVENT_CAP];
    uint32_t event_head;
    uint32_t event_tail;
    char title[GUI_IPC_WINDOW_TITLE_MAX];
    char text[GUI_IPC_WINDOW_TEXT_MAX];
    char app_path[GUI_IPC_WINDOW_PATH_MAX];
};

static struct gui_window_slot windows[GUI_IPC_MAX_WINDOWS];

static int utf8_cont(uint8_t byte)
{
    return (byte & 0xc0u) == 0x80u;
}

static uint32_t user_utf8_sequence_len(const char *src)
{
    uint8_t b0;
    uint8_t b1;
    uint8_t b2;
    uint8_t b3;
    if (!user_range_ok((uint64_t)(uintptr_t)src, 1)) {
        return 0;
    }
    b0 = (uint8_t)src[0];
    if (b0 == 0) {
        return 0;
    }
    if (b0 < 0x80u) {
        return 1;
    }
    if (b0 < 0xc2u) {
        return 0;
    }
    if (b0 < 0xe0u) {
        if (!user_range_ok((uint64_t)(uintptr_t)(src + 1), 1)) {
            return 0;
        }
        b1 = (uint8_t)src[1];
        return utf8_cont(b1) ? 2u : 0u;
    }
    if (b0 < 0xf0u) {
        if (!user_range_ok((uint64_t)(uintptr_t)(src + 1), 2)) {
            return 0;
        }
        b1 = (uint8_t)src[1];
        b2 = (uint8_t)src[2];
        if (!utf8_cont(b1) || !utf8_cont(b2) ||
            (b0 == 0xe0u && b1 < 0xa0u) ||
            (b0 == 0xedu && b1 >= 0xa0u)) {
            return 0;
        }
        return 3;
    }
    if (b0 < 0xf5u) {
        if (!user_range_ok((uint64_t)(uintptr_t)(src + 1), 3)) {
            return 0;
        }
        b1 = (uint8_t)src[1];
        b2 = (uint8_t)src[2];
        b3 = (uint8_t)src[3];
        if (!utf8_cont(b1) || !utf8_cont(b2) || !utf8_cont(b3) ||
            (b0 == 0xf0u && b1 < 0x90u) ||
            (b0 == 0xf4u && b1 >= 0x90u)) {
            return 0;
        }
        return 4;
    }
    return 0;
}

static void copy_user_string(char *dst, size_t dst_len, const char *src)
{
    if (!dst || dst_len == 0) {
        return;
    }
    size_t i = 0;
    if (src) {
        size_t src_pos = 0;
        while (i + 1 < dst_len) {
            uint32_t len = user_utf8_sequence_len(src + src_pos);
            if (len == 0 || i + len >= dst_len) {
                break;
            }
            for (uint32_t j = 0; j < len; ++j) {
                dst[i++] = src[src_pos++];
            }
        }
    }
    dst[i] = 0;
}

static void copy_kernel_string(char *dst, size_t dst_len, const char *src)
{
    size_t i = 0;
    if (!dst || dst_len == 0) {
        return;
    }
    if (src) {
        size_t safe = osmlayer_unicode_safe_truncate_utf8(src, (uint32_t)dst_len);
        while (i < safe) {
            dst[i] = src[i];
            ++i;
        }
    }
    dst[i] = 0;
}

static struct gui_window_slot *find_window(uint32_t window_id)
{
    for (uint32_t i = 0; i < GUI_IPC_MAX_WINDOWS; ++i) {
        if (windows[i].used && windows[i].id == window_id) {
            return &windows[i];
        }
    }
    return NULL;
}

static struct gui_window_slot *alloc_window_slot(void)
{
    for (uint32_t i = 0; i < GUI_IPC_MAX_WINDOWS; ++i) {
        if (!windows[i].used) {
            windows[i].used = 1;
            return &windows[i];
        }
    }
    return NULL;
}

static uint32_t min_u32(uint32_t a, uint32_t b)
{
    return a < b ? a : b;
}

static uint32_t *window_pixels(const struct gui_window_slot *slot)
{
    return slot && slot->buffer_phys ? (uint32_t *)(uintptr_t)slot->buffer_phys : NULL;
}

static int caller_is_window_server(uint32_t caller_pid)
{
    struct task *task = sched_find(caller_pid);
    return task && (task->flags & TASK_FLAG_WINDOW_SERVER);
}

static void free_window_buffer(struct gui_window_slot *slot)
{
    if (!slot || !slot->buffer_phys || !slot->buffer_pages) {
        return;
    }
    for (uint32_t i = 0; i < slot->buffer_pages; ++i) {
        mm_free_page(slot->buffer_phys + (uint64_t)i * 4096ULL);
    }
    slot->buffer_phys = 0;
    slot->buffer_pages = 0;
}

static int ensure_window_buffer(struct gui_window_slot *slot, uint32_t width, uint32_t height)
{
    uint64_t bytes;
    uint32_t pages;
    uint64_t phys;
    if (!slot || !width || !height) {
        return 0;
    }
    bytes = (uint64_t)width * height * sizeof(uint32_t);
    pages = (uint32_t)((bytes + 4095ULL) / 4096ULL);
    if (pages <= slot->buffer_pages && slot->buffer_phys) {
        return 1;
    }
    phys = mm_alloc_pages(pages);
    if (!phys) {
        return 0;
    }
    free_window_buffer(slot);
    slot->buffer_phys = phys;
    slot->buffer_pages = pages;
    return 1;
}

static void fill_message(struct gui_ipc_window *msg, uint32_t type,
                         const struct gui_window_slot *slot)
{
    msg->type = type;
    msg->pid = slot->owner_pid;
    msg->window_id = slot->id;
    msg->width = slot->width;
    msg->height = slot->height;
    msg->flags = slot->flags;
    copy_kernel_string(msg->title, sizeof(msg->title), slot->title);
    copy_kernel_string(msg->text, sizeof(msg->text), slot->text);
    copy_kernel_string(msg->app_path, sizeof(msg->app_path), slot->app_path);
}

static int coalesce_dirty_message(uint32_t type, const struct gui_window_slot *slot)
{
    uint32_t i;
    if (type != GUI_IPC_WINDOW_MSG_DIRTY || !slot) {
        return 0;
    }
    i = tail;
    while (i != head) {
        struct gui_ipc_window *msg = &queue[i];
        if (msg->type == GUI_IPC_WINDOW_MSG_DIRTY &&
            msg->window_id == slot->id) {
            fill_message(msg, type, slot);
            return 1;
        }
        i = (i + 1) % GUI_IPC_QUEUE_CAP;
    }
    return 0;
}

static int push_message(uint32_t type, const struct gui_window_slot *slot)
{
    uint32_t next = (head + 1) % GUI_IPC_QUEUE_CAP;
    struct gui_ipc_window *msg;
    if (!slot) {
        return 0;
    }
    if (coalesce_dirty_message(type, slot)) {
        return 1;
    }
    if (next == tail) {
        tail = (tail + 1) % GUI_IPC_QUEUE_CAP;
    }
    msg = &queue[head];
    fill_message(msg, type, slot);
    head = next;
    return 1;
}

static int push_system_window_message(uint32_t pid, uint32_t width, uint32_t height,
                                      const char *title, const char *text,
                                      const char *app_path, uint32_t flags)
{
    uint32_t next = (head + 1) % GUI_IPC_QUEUE_CAP;
    struct gui_ipc_window *msg;
    if (!pid || !width || !height) {
        return 0;
    }
    if (next == tail) {
        tail = (tail + 1) % GUI_IPC_QUEUE_CAP;
    }
    msg = &queue[head];
    msg->type = GUI_IPC_WINDOW_MSG_CREATE;
    msg->pid = pid;
    msg->window_id = 0;
    msg->width = width;
    msg->height = height;
    msg->flags = flags;
    copy_kernel_string(msg->title, sizeof(msg->title), title);
    copy_kernel_string(msg->text, sizeof(msg->text), text);
    copy_kernel_string(msg->app_path, sizeof(msg->app_path), app_path);
    head = next;
    return 1;
}

void gui_ipc_init(void)
{
    head = 0;
    tail = 0;
    next_window_id = 1;
    display_request_pending = 0;
    display_state_valid = 0;
    for (uint32_t i = 0; i < GUI_IPC_MAX_WINDOWS; ++i) {
        windows[i].used = 0;
        windows[i].buffer_phys = 0;
        windows[i].buffer_pages = 0;
    }
}

int gui_ipc_display_state(struct gui_ipc_display_state *out)
{
    if (!out || !display_state_valid) {
        return 0;
    }
    *out = display_state;
    return 1;
}

int gui_ipc_publish_display_state(uint32_t caller_pid,
                                  const struct gui_ipc_display_state *state)
{
    if (!caller_is_window_server(caller_pid) || !state) {
        return 0;
    }
    display_state = *state;
    display_state_valid = 1;
    return 1;
}

int gui_ipc_request_display(const struct gui_ipc_display_request *request)
{
    if (!request) {
        return 0;
    }
    display_request = *request;
    display_request_pending = 1;
    return 1;
}

int gui_ipc_pop_display_request(uint32_t caller_pid,
                                struct gui_ipc_display_request *out)
{
    if (!caller_is_window_server(caller_pid) || !out || !display_request_pending) {
        return 0;
    }
    *out = display_request;
    display_request_pending = 0;
    return 1;
}

int gui_ipc_appearance_state(struct gui_ipc_appearance_state *out)
{
    if (!out) {
        return 0;
    }
    *out = appearance_state;
    return 1;
}

int gui_ipc_publish_appearance_state(uint32_t caller_pid,
                                     const struct gui_ipc_appearance_state *state)
{
    if (!caller_is_window_server(caller_pid) || !state || state->theme > 1u) {
        return 0;
    }
    appearance_state = *state;
    return 1;
}

int gui_ipc_request_appearance(const struct gui_ipc_appearance_request *request)
{
    if (!request || request->theme > 1u) {
        return 0;
    }
    appearance_request = *request;
    appearance_request_pending = 1;
    return 1;
}

int gui_ipc_pop_appearance_request(uint32_t caller_pid,
                                   struct gui_ipc_appearance_request *out)
{
    if (!caller_is_window_server(caller_pid) || !out || !appearance_request_pending) {
        return 0;
    }
    *out = appearance_request;
    appearance_request_pending = 0;
    return 1;
}

void gui_ipc_set_boot_theme(uint32_t theme)
{
    appearance_state.theme = theme == 0u ? 0u : 1u;
}

uint32_t gui_ipc_appearance_theme(void)
{
    return appearance_state.theme;
}

int32_t gui_ipc_create_window(uint32_t pid, uint32_t width, uint32_t height,
                              const char *title, const char *text, uint32_t flags)
{
    struct gui_window_slot *slot;
    uint32_t id;
    if (!pid || !width || !height) {
        return 0;
    }
    slot = alloc_window_slot();
    if (!slot) {
        return 0;
    }
    id = next_window_id++;
    if (id == 0) {
        id = next_window_id++;
    }
    slot->id = id;
    slot->owner_pid = pid;
    slot->width = width;
    slot->height = height;
    slot->flags = flags;
    slot->event_head = 0;
    slot->event_tail = 0;
    copy_user_string(slot->title, sizeof(slot->title), title);
    copy_user_string(slot->text, sizeof(slot->text), text);
    {
        struct task *owner = sched_find(pid);
        copy_kernel_string(slot->app_path, sizeof(slot->app_path),
                           owner ? owner->path : "");
    }
    if (!push_message(GUI_IPC_WINDOW_MSG_CREATE, slot)) {
        slot->used = 0;
        return 0;
    }
    return (int32_t)id;
}

int gui_ipc_post_system_window(uint32_t pid, uint32_t width, uint32_t height,
                               const char *title, const char *text,
                               const char *app_path, uint32_t flags)
{
    if (!sched_find(pid)) {
        return 0;
    }
    return push_system_window_message(pid, width, height, title, text, app_path, flags);
}

int gui_ipc_pop_window(uint32_t caller_pid, struct gui_ipc_window *out)
{
    if (!caller_is_window_server(caller_pid) || !out || tail == head) {
        return 0;
    }
    *out = queue[tail];
    tail = (tail + 1) % GUI_IPC_QUEUE_CAP;
    return 1;
}

int gui_ipc_present_window(uint32_t pid, uint32_t window_id, uint32_t width, uint32_t height,
                           uint32_t stride, const uint32_t *pixels)
{
    struct gui_window_slot *slot = find_window(window_id);
    uint32_t *dst;
    if (!slot || slot->owner_pid != pid || !pixels || !width || !height || stride < width) {
        return 0;
    }
    if (!ensure_window_buffer(slot, width, height)) {
        return 0;
    }
    dst = window_pixels(slot);
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            dst[(uint64_t)y * width + x] = pixels[(uint64_t)y * stride + x];
        }
    }
    slot->width = width;
    slot->height = height;
    return push_message(GUI_IPC_WINDOW_MSG_DIRTY, slot);
}

int gui_ipc_destroy_window(uint32_t pid, uint32_t window_id)
{
    struct gui_window_slot *slot = find_window(window_id);
    if (!slot || slot->owner_pid != pid) {
        return 0;
    }
    (void)push_message(GUI_IPC_WINDOW_MSG_CLOSE, slot);
    free_window_buffer(slot);
    slot->used = 0;
    slot->id = 0;
    slot->owner_pid = 0;
    slot->width = 0;
    slot->height = 0;
    slot->flags = 0;
    slot->event_head = 0;
    slot->event_tail = 0;
    slot->title[0] = 0;
    slot->text[0] = 0;
    slot->app_path[0] = 0;
    return 1;
}

int gui_ipc_fetch_window(uint32_t caller_pid, uint32_t window_id,
                         uint32_t capacity_width, uint32_t capacity_height,
                         uint32_t stride, uint32_t *pixels,
                         uint32_t *out_width, uint32_t *out_height)
{
    struct gui_window_slot *slot = find_window(window_id);
    uint32_t copy_w;
    uint32_t copy_h;
    uint32_t *src;
    if (!caller_is_window_server(caller_pid) || !slot || !pixels || !slot->buffer_phys ||
        !capacity_width || !capacity_height || stride < capacity_width) {
        return 0;
    }
    copy_w = min_u32(slot->width, capacity_width);
    copy_h = min_u32(slot->height, capacity_height);
    src = window_pixels(slot);
    for (uint32_t y = 0; y < copy_h; ++y) {
        for (uint32_t x = 0; x < copy_w; ++x) {
            pixels[(uint64_t)y * stride + x] = src[(uint64_t)y * slot->width + x];
        }
    }
    if (out_width) {
        *out_width = copy_w;
    }
    if (out_height) {
        *out_height = copy_h;
    }
    return 1;
}

int gui_ipc_push_event(uint32_t caller_pid, uint32_t window_id,
                       const struct gui_ipc_app_event *event)
{
    struct gui_window_slot *slot = find_window(window_id);
    uint32_t next;
    if (!caller_is_window_server(caller_pid) || !slot || !event) {
        return 0;
    }
    next = (slot->event_head + 1) % GUI_IPC_WINDOW_EVENT_CAP;
    if (next == slot->event_tail) {
        slot->event_tail = (slot->event_tail + 1) % GUI_IPC_WINDOW_EVENT_CAP;
    }
    slot->events[slot->event_head] = *event;
    slot->event_head = next;
    sched_wake_window_event(slot->owner_pid, window_id);
    return 1;
}

int gui_ipc_pop_event(uint32_t pid, uint32_t window_id, struct gui_ipc_app_event *out)
{
    struct gui_window_slot *slot = find_window(window_id);
    if (!slot || slot->owner_pid != pid || !out || slot->event_tail == slot->event_head) {
        return 0;
    }
    *out = slot->events[slot->event_tail];
    slot->event_tail = (slot->event_tail + 1) % GUI_IPC_WINDOW_EVENT_CAP;
    return 1;
}

void gui_ipc_destroy_owner(uint32_t pid)
{
    for (uint32_t i = 0; i < GUI_IPC_MAX_WINDOWS; ++i) {
        struct gui_window_slot *slot = &windows[i];
        if (!slot->used || slot->owner_pid != pid) {
            continue;
        }
        (void)push_message(GUI_IPC_WINDOW_MSG_CLOSE, slot);
        free_window_buffer(slot);
        slot->used = 0;
        slot->id = 0;
        slot->owner_pid = 0;
        slot->width = 0;
        slot->height = 0;
        slot->flags = 0;
        slot->event_head = 0;
        slot->event_tail = 0;
        slot->title[0] = 0;
        slot->text[0] = 0;
        slot->app_path[0] = 0;
    }
}

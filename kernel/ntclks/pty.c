#include <ntclks/pty.h>

#define PTY_MAX 8u
#define PTY_INPUT_CAP 1024u
#define PTY_OUTPUT_CAP 8192u

struct pty_session {
    uint8_t used;
    uint8_t reserved[3];
    uint32_t owner_pid;
    uint8_t input[PTY_INPUT_CAP];
    uint32_t input_head;
    uint32_t input_tail;
    uint8_t output[PTY_OUTPUT_CAP];
    uint32_t output_head;
    uint32_t output_tail;
};

static struct pty_session sessions[PTY_MAX];

static struct pty_session *find_session(uint32_t pty_id)
{
    if (pty_id == 0 || pty_id > PTY_MAX) {
        return 0;
    }
    if (!sessions[pty_id - 1].used) {
        return 0;
    }
    return &sessions[pty_id - 1];
}

static uint32_t ring_push(uint8_t *ring, uint32_t cap, uint32_t *head, uint32_t *tail,
                          const char *buffer, uint32_t length)
{
    uint32_t written = 0;
    for (uint32_t i = 0; i < length; ++i) {
        uint32_t next = (*head + 1) % cap;
        if (next == *tail) {
            *tail = (*tail + 1) % cap;
        }
        ring[*head] = (uint8_t)buffer[i];
        *head = next;
        ++written;
    }
    return written;
}

static uint32_t ring_pop(uint8_t *ring, uint32_t cap, uint32_t *head, uint32_t *tail,
                         char *buffer, uint32_t length)
{
    uint32_t read = 0;
    while (*tail != *head && read < length) {
        buffer[read++] = (char)ring[*tail];
        *tail = (*tail + 1) % cap;
    }
    return read;
}

void pty_init(void)
{
    for (uint32_t i = 0; i < PTY_MAX; ++i) {
        for (uint32_t j = 0; j < sizeof(sessions[i]); ++j) {
            ((uint8_t *)&sessions[i])[j] = 0;
        }
    }
}

int32_t pty_create(uint32_t owner_pid)
{
    for (uint32_t i = 0; i < PTY_MAX; ++i) {
        if (!sessions[i].used) {
            sessions[i].used = 1;
            sessions[i].owner_pid = owner_pid;
            sessions[i].input_head = 0;
            sessions[i].input_tail = 0;
            sessions[i].output_head = 0;
            sessions[i].output_tail = 0;
            return (int32_t)(i + 1);
        }
    }
    return -12;
}

int pty_is_owner(uint32_t pty_id, uint32_t owner_pid)
{
    struct pty_session *session = find_session(pty_id);
    return session && session->owner_pid == owner_pid;
}

int64_t pty_read_output(uint32_t owner_pid, uint32_t pty_id, char *buffer, uint32_t length)
{
    struct pty_session *session = find_session(pty_id);
    if (!session || session->owner_pid != owner_pid) {
        return -22;
    }
    if (!buffer || length == 0) {
        return 0;
    }
    return (int64_t)ring_pop(session->output, PTY_OUTPUT_CAP,
                             &session->output_head, &session->output_tail,
                             buffer, length);
}

int64_t pty_write_input(uint32_t owner_pid, uint32_t pty_id, const char *buffer, uint32_t length)
{
    struct pty_session *session = find_session(pty_id);
    if (!session || session->owner_pid != owner_pid) {
        return -22;
    }
    if (!buffer || length == 0) {
        return 0;
    }
    return (int64_t)ring_push(session->input, PTY_INPUT_CAP,
                              &session->input_head, &session->input_tail,
                              buffer, length);
}

int64_t pty_read_input(uint32_t pty_id, char *buffer, uint32_t length)
{
    struct pty_session *session = find_session(pty_id);
    if (!session) {
        return -5;
    }
    if (!buffer || length == 0) {
        return 0;
    }
    return (int64_t)ring_pop(session->input, PTY_INPUT_CAP,
                             &session->input_head, &session->input_tail,
                             buffer, length);
}

int64_t pty_write_output(uint32_t pty_id, const char *buffer, uint32_t length)
{
    struct pty_session *session = find_session(pty_id);
    if (!session) {
        return -5;
    }
    if (!buffer || length == 0) {
        return 0;
    }
    return (int64_t)ring_push(session->output, PTY_OUTPUT_CAP,
                              &session->output_head, &session->output_tail,
                              buffer, length);
}

void pty_process_exit(uint32_t pid)
{
    for (uint32_t i = 0; i < PTY_MAX; ++i) {
        if (sessions[i].used && sessions[i].owner_pid == pid) {
            for (uint32_t j = 0; j < sizeof(sessions[i]); ++j) {
                ((uint8_t *)&sessions[i])[j] = 0;
            }
        }
    }
}

#include <ntclks/pty.h>
#include <ntclks/sched.h>

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
    /* ICANON input is kept separate until a line delimiter arrives. */
    uint8_t canonical_input[PTY_INPUT_CAP];
    uint32_t canonical_length;
    uint8_t output[PTY_OUTPUT_CAP];
    uint32_t output_head;
    uint32_t output_tail;
    struct leonos_pty_termios termios;
    struct leonos_pty_winsize winsize;
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

static void pty_commit_canonical_input(struct pty_session *session)
{
    if (!session || !session->canonical_length) {
        return;
    }
    (void)ring_push(session->input, PTY_INPUT_CAP,
                    &session->input_head, &session->input_tail,
                    (const char *)session->canonical_input,
                    session->canonical_length);
    session->canonical_length = 0;
}

static int pty_canonical_mode(const struct pty_session *session)
{
    return session &&
           (session->termios.c_lflag & LEONOS_PTY_LFLAG_ICANON) != 0;
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
            sessions[i].termios.c_iflag = LEONOS_PTY_IFLAG_ICRNL;
            sessions[i].termios.c_oflag = 0x0003U; /* OPOST|ONLCR */
            sessions[i].termios.c_cflag = 0x0228U; /* CLOCAL|CREAD|CS8 */
            sessions[i].termios.c_lflag = LEONOS_PTY_LFLAG_ECHO |
                                          LEONOS_PTY_LFLAG_ECHONL |
                                          LEONOS_PTY_LFLAG_ICANON |
                                          LEONOS_PTY_LFLAG_IEXTEN |
                                          LEONOS_PTY_LFLAG_ISIG;
            sessions[i].termios.c_cc[LEONOS_PTY_CC_VEOF] = 4;     /* Ctrl-D */
            sessions[i].termios.c_cc[LEONOS_PTY_CC_VEOL] = 0;
            sessions[i].termios.c_cc[LEONOS_PTY_CC_VERASE] = 127; /* DEL */
            sessions[i].termios.c_cc[LEONOS_PTY_CC_VINTR] = 3;    /* Ctrl-C */
            sessions[i].termios.c_cc[LEONOS_PTY_CC_VKILL] = 21;   /* Ctrl-U */
            sessions[i].termios.c_cc[LEONOS_PTY_CC_VMIN] = 1;
            sessions[i].termios.c_cc[LEONOS_PTY_CC_VQUIT] = 28;
            sessions[i].termios.c_cc[LEONOS_PTY_CC_VSTART] = 17;
            sessions[i].termios.c_cc[LEONOS_PTY_CC_VSTOP] = 19;
            sessions[i].termios.c_cc[LEONOS_PTY_CC_VSUSP] = 26;
            sessions[i].termios.c_cc[LEONOS_PTY_CC_VTIME] = 0;
            sessions[i].termios.c_ispeed = 115200U;
            sessions[i].termios.c_ospeed = 115200U;
            sessions[i].winsize.ws_row = 24;
            sessions[i].winsize.ws_col = 80;
            return (int32_t)(i + 1);
        }
    }
    return -12;
}

int pty_destroy(uint32_t owner_pid, uint32_t pty_id)
{
    struct pty_session *session = find_session(pty_id);
    if (!session || session->owner_pid != owner_pid) {
        return -22;
    }
    (void)sched_kill_user_tasks_for_pty(pty_id, owner_pid, 137);
    for (uint32_t j = 0; j < sizeof(*session); ++j) {
        ((uint8_t *)session)[j] = 0;
    }
    return 0;
}

int pty_is_owner(uint32_t pty_id, uint32_t owner_pid)
{
    struct pty_session *session = find_session(pty_id);
    return session && session->owner_pid == owner_pid;
}

int pty_is_active(uint32_t pty_id)
{
    return find_session(pty_id) != 0;
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
    uint32_t written = 0;
    if (!session || session->owner_pid != owner_pid) {
        return -22;
    }
    if (!buffer || length == 0) {
        return 0;
    }
    for (uint32_t i = 0; i < length; ++i) {
        char input = buffer[i];
        if (input == '\r' &&
            (session->termios.c_iflag & LEONOS_PTY_IFLAG_ICRNL)) {
            input = '\n';
        }
        if (!pty_canonical_mode(session)) {
            written += ring_push(session->input, PTY_INPUT_CAP,
                                 &session->input_head, &session->input_tail,
                                 &input, 1);
            continue;
        }

        if (input == (char)session->termios.c_cc[LEONOS_PTY_CC_VERASE]) {
            if (session->canonical_length) {
                --session->canonical_length;
            }
        } else if (input == (char)session->termios.c_cc[LEONOS_PTY_CC_VKILL]) {
            session->canonical_length = 0;
        } else if (input == (char)session->termios.c_cc[LEONOS_PTY_CC_VEOF]) {
            /* VEOF is a delimiter, not a byte delivered to the reader. */
            pty_commit_canonical_input(session);
        } else {
            if (session->canonical_length + 1U < PTY_INPUT_CAP) {
                session->canonical_input[session->canonical_length++] = (uint8_t)input;
            }
            if (input == '\n' ||
                (session->termios.c_cc[LEONOS_PTY_CC_VEOL] != 0 &&
                 input == (char)session->termios.c_cc[LEONOS_PTY_CC_VEOL])) {
                pty_commit_canonical_input(session);
            }
        }
        ++written;
    }
    return (int64_t)written;
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

uint32_t pty_input_available(uint32_t pty_id)
{
    struct pty_session *session = find_session(pty_id);
    if (!session) {
        return 0;
    }
    if (session->input_head >= session->input_tail) {
        return session->input_head - session->input_tail;
    }
    return PTY_INPUT_CAP - session->input_tail + session->input_head;
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

int pty_get_termios(uint32_t pty_id, struct leonos_pty_termios *termios)
{
    struct pty_session *session = find_session(pty_id);
    if (!session || !termios) {
        return -22;
    }
    *termios = session->termios;
    return 0;
}

int pty_set_termios(uint32_t pty_id, const struct leonos_pty_termios *termios)
{
    struct pty_session *session = find_session(pty_id);
    if (!session || !termios) {
        return -22;
    }
    /* A program switching to raw mode must be able to read a line that was
     * already typed in cooked mode instead of leaving it stranded forever. */
    if (pty_canonical_mode(session) &&
        (termios->c_lflag & LEONOS_PTY_LFLAG_ICANON) == 0) {
        pty_commit_canonical_input(session);
    }
    session->termios = *termios;
    return 0;
}

int pty_get_winsize(uint32_t pty_id, struct leonos_pty_winsize *winsize)
{
    struct pty_session *session = find_session(pty_id);
    if (!session || !winsize) {
        return -22;
    }
    *winsize = session->winsize;
    return 0;
}

int pty_set_winsize(uint32_t pty_id, const struct leonos_pty_winsize *winsize)
{
    struct pty_session *session = find_session(pty_id);
    if (!session || !winsize) {
        return -22;
    }
    if (winsize->ws_row == 0 || winsize->ws_col == 0) {
        return -22;
    }
    session->winsize = *winsize;
    return 0;
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

/*
 * LeonOS kernel pseudo-terminals: implements terminal input and output queues.
 * Connects shells and terminal applications to their controlling sessions.
 */
#include <ntclks/pty.h>
#include <ntclks/console.h>
#include <ntclks/framebuffer.h>
#include <ntclks/sched.h>
#include <leonos/psf_font.h>

#define PTY_MAX 8u
#define PTY_INPUT_CAP 1024u
#define PTY_OUTPUT_CAP 8192u

struct pty_session {
    uint8_t used;
    uint8_t console;
    uint8_t reserved[2];
    uint32_t owner_pid;
    uint32_t process_session;
    uint32_t foreground_pgid;
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
static uint32_t console_pty_id;
static uint8_t console_shift_down;
static uint8_t console_ctrl_down;

/**
 * @brief Return the PTY session for pty_id (1-based), or NULL if invalid/unused.
 */
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

/**
 * @brief Append length bytes into the ring, overwriting the oldest when full; returns bytes written.
 */
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

/**
 * @brief Remove up to length bytes from the ring into buffer; returns bytes read.
 */
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

/**
 * @brief Move the buffered canonical line into the input queue and clear it.
 */
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

/**
 * @brief Return 1 if the session is in ICANON line mode.
 */
static int pty_canonical_mode(const struct pty_session *session)
{
    return session &&
           (session->termios.c_lflag & LEONOS_PTY_LFLAG_ICANON) != 0;
}

/**
 * @brief Zero all PTY sessions.
 */
void pty_init(void)
{
    console_pty_id = 0;
    console_shift_down = 0;
    console_ctrl_down = 0;
    for (uint32_t i = 0; i < PTY_MAX; ++i) {
        for (uint32_t j = 0; j < sizeof(sessions[i]); ++j) {
            ((uint8_t *)&sessions[i])[j] = 0;
        }
    }
}

int pty_bind_console(uint32_t pty_id, uint32_t owner_pid)
{
    struct pty_session *session = find_session(pty_id);
    struct task *owner = owner_pid ? sched_find(owner_pid) : 0;
    if (!session || !owner) {
        return -22;
    }
    session->owner_pid = owner_pid;
    session->process_session = owner_pid;
    session->foreground_pgid = owner_pid;
    session->console = 1;
    console_pty_id = pty_id;
    owner->process_session = owner_pid;
    owner->process_group = owner_pid;
    return 0;
}

static int console_key_to_bytes(uint8_t keycode, char *buffer, uint32_t *length)
{
    char ch = 0;
    if (!buffer || !length) {
        return 0;
    }
    *length = 0;
    switch (keycode) {
    case 1: buffer[0] = '\033'; *length = 1; return 1;
    case 72: buffer[0] = '\033'; buffer[1] = '['; buffer[2] = 'A'; *length = 3; return 1;
    case 80: buffer[0] = '\033'; buffer[1] = '['; buffer[2] = 'B'; *length = 3; return 1;
    case 75: buffer[0] = '\033'; buffer[1] = '['; buffer[2] = 'D'; *length = 3; return 1;
    case 77: buffer[0] = '\033'; buffer[1] = '['; buffer[2] = 'C'; *length = 3; return 1;
    case 71: buffer[0] = '\033'; buffer[1] = '['; buffer[2] = 'H'; *length = 3; return 1;
    case 79: buffer[0] = '\033'; buffer[1] = '['; buffer[2] = 'F'; *length = 3; return 1;
    case 28: ch = '\r'; break;
    case 14: ch = '\177'; break;
    case 15: ch = '\t'; break;
    case 57: ch = ' '; break;
    case 2: ch = '1'; break; case 3: ch = '2'; break; case 4: ch = '3'; break;
    case 5: ch = '4'; break; case 6: ch = '5'; break; case 7: ch = '6'; break;
    case 8: ch = '7'; break; case 9: ch = '8'; break; case 10: ch = '9'; break;
    case 11: ch = '0'; break; case 12: ch = '-'; break; case 13: ch = '='; break;
    case 16: ch = 'q'; break; case 17: ch = 'w'; break; case 18: ch = 'e'; break;
    case 19: ch = 'r'; break; case 20: ch = 't'; break; case 21: ch = 'y'; break;
    case 22: ch = 'u'; break; case 23: ch = 'i'; break; case 24: ch = 'o'; break;
    case 25: ch = 'p'; break; case 26: ch = '['; break; case 27: ch = ']'; break;
    case 30: ch = 'a'; break; case 31: ch = 's'; break; case 32: ch = 'd'; break;
    case 33: ch = 'f'; break; case 34: ch = 'g'; break; case 35: ch = 'h'; break;
    case 36: ch = 'j'; break; case 37: ch = 'k'; break; case 38: ch = 'l'; break;
    case 39: ch = ';'; break; case 40: ch = '\''; break; case 41: ch = '`'; break;
    case 43: ch = '\\'; break; case 44: ch = 'z'; break; case 45: ch = 'x'; break;
    case 46: ch = 'c'; break; case 47: ch = 'v'; break; case 48: ch = 'b'; break;
    case 49: ch = 'n'; break; case 50: ch = 'm'; break; case 51: ch = ','; break;
    case 52: ch = '.'; break; case 53: ch = '/'; break;
    default: return 0;
    }
    if (console_shift_down && ch >= 'a' && ch <= 'z') {
        ch = (char)(ch - 'a' + 'A');
    } else if (console_shift_down) {
        switch (ch) {
        case '1': ch = '!'; break; case '2': ch = '@'; break; case '3': ch = '#'; break;
        case '4': ch = '$'; break; case '5': ch = '%'; break; case '6': ch = '^'; break;
        case '7': ch = '&'; break; case '8': ch = '*'; break; case '9': ch = '('; break;
        case '0': ch = ')'; break; case '-': ch = '_'; break; case '=': ch = '+'; break;
        case '[': ch = '{'; break; case ']': ch = '}'; break; case ';': ch = ':'; break;
        case '\'': ch = '"'; break; case '`': ch = '~'; break; case '\\': ch = '|'; break;
        case ',': ch = '<'; break; case '.': ch = '>'; break; case '/': ch = '?'; break;
        default: break;
        }
    }
    if (console_ctrl_down && ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z'))) {
        ch = (char)(((ch | 0x20) - 'a') + 1);
    }
    buffer[0] = ch;
    *length = 1;
    return 1;
}

void pty_console_key_event(uint8_t keycode, uint8_t pressed)
{
    struct pty_session *session = find_session(console_pty_id);
    char bytes[8];
    uint32_t length;
    if (keycode == 42 || keycode == 54) {
        console_shift_down = pressed ? 1 : 0;
        return;
    }
    if (keycode == 29 || keycode == 116) {
        console_ctrl_down = pressed ? 1 : 0;
        return;
    }
    if (!pressed || !session || !console_key_to_bytes(keycode, bytes, &length)) {
        return;
    }
    /* Feed through the same canonical/ISIG handling used by PTY hosts. */
    (void)pty_write_input(session->owner_pid, console_pty_id, bytes, length);
    if (session->termios.c_lflag & LEONOS_PTY_LFLAG_ECHO) {
        if (keycode == 14) {
            static const char erase[] = "\b \b";
            console_write_tty_len(erase, sizeof(erase) - 1U);
        } else if (keycode == 28) {
            static const char newline[] = "\r\n";
            console_write_tty_len(newline, sizeof(newline) - 1U);
        } else if (length == 1 && (uint8_t)bytes[0] >= 32U) {
            console_write_tty_len(bytes, 1);
        }
    }
}

/**
 * @brief Allocate and initialize a PTY for owner_pid with default termios/winsize; returns its id, or -12 when exhausted.
 */
int32_t pty_create(uint32_t owner_pid)
{
    for (uint32_t i = 0; i < PTY_MAX; ++i) {
        if (!sessions[i].used) {
            sessions[i].used = 1;
            sessions[i].owner_pid = owner_pid;
            {
                struct task *owner = sched_find(owner_pid);
                sessions[i].process_session = owner ? owner->process_session : 0;
                sessions[i].foreground_pgid = owner ? owner->process_group : 0;
            }
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
            {
                const struct framebuffer *fb = framebuffer_get();
                uint32_t cols = fb && fb->available ? fb->width / LEONOS_FONT_W : 80u;
                uint32_t rows = fb && fb->available ? fb->height / LEONOS_FONT_H : 24u;
                sessions[i].winsize.ws_row = (uint16_t)(rows > 0xffffu ? 0xffffu : rows);
                sessions[i].winsize.ws_col = (uint16_t)(cols > 0xffffu ? 0xffffu : cols);
                if (!sessions[i].winsize.ws_row) sessions[i].winsize.ws_row = 24;
                if (!sessions[i].winsize.ws_col) sessions[i].winsize.ws_col = 80;
            }
            return (int32_t)(i + 1);
        }
    }
    return -12;
}

/**
 * @brief Kill the PTY's attached tasks and free the session; returns 0, or -22 if not owned.
 */
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

/**
 * @brief Return 1 if owner_pid owns pty_id.
 */
int pty_is_owner(uint32_t pty_id, uint32_t owner_pid)
{
    struct pty_session *session = find_session(pty_id);
    return session && session->owner_pid == owner_pid;
}

/**
 * @brief Return 1 if pty_id is an allocated session.
 */
int pty_is_active(uint32_t pty_id)
{
    return find_session(pty_id) != 0;
}

/**
 * @brief Drain up to length bytes of terminal output for the owner; returns bytes read, or -22 if not owned.
 */
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

/**
 * @brief Process input bytes through termios (CR->NL, signals, line editing) and queue the results; returns bytes consumed.
 */
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
        if ((session->termios.c_lflag & LEONOS_PTY_LFLAG_ISIG) != 0 &&
            input == (char)session->termios.c_cc[LEONOS_PTY_CC_VINTR]) {
            session->canonical_length = 0;
            if (session->foreground_pgid) {
                (void)sched_signal_process_group(session->owner_pid,
                                                 session->foreground_pgid, 2);
            }
            ++written;
            continue;
        }
        if ((session->termios.c_lflag & LEONOS_PTY_LFLAG_ISIG) != 0 &&
            input == (char)session->termios.c_cc[LEONOS_PTY_CC_VSUSP]) {
            session->canonical_length = 0;
            if (session->foreground_pgid) {
                (void)sched_signal_process_group(session->owner_pid,
                                                 session->foreground_pgid, 18);
            }
            ++written;
            continue;
        }
        if ((session->termios.c_lflag & LEONOS_PTY_LFLAG_ISIG) != 0 &&
            input == (char)session->termios.c_cc[LEONOS_PTY_CC_VQUIT]) {
            session->canonical_length = 0;
            if (session->foreground_pgid) {
                (void)sched_signal_process_group(session->owner_pid,
                                                 session->foreground_pgid, 3);
            }
            ++written;
            continue;
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
            /**
 * @brief VEOF is a delimiter, not a byte delivered to the reader.
 */
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

/**
 * @brief Drain up to length bytes from the input queue; returns bytes read, or -5 if the session is invalid.
 */
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

/**
 * @brief Return the number of unread input bytes.
 */
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

/**
 * @brief Queue up to length bytes of output for the reader; returns bytes written, or -5 if the session is invalid.
 */
int64_t pty_write_output(uint32_t pty_id, const char *buffer, uint32_t length)
{
    struct pty_session *session = find_session(pty_id);
    if (!session) {
        return -5;
    }
    if (!buffer || length == 0) {
        return 0;
    }
    if (session->console) {
        console_write_tty_len(buffer, length);
        return (int64_t)length;
    }
    return (int64_t)ring_push(session->output, PTY_OUTPUT_CAP,
                              &session->output_head, &session->output_tail,
                              buffer, length);
}

/**
 * @brief Copy the session termios into *termios; returns 0, or -22 on a bad id or null pointer.
 */
int pty_get_termios(uint32_t pty_id, struct leonos_pty_termios *termios)
{
    struct pty_session *session = find_session(pty_id);
    if (!session || !termios) {
        return -22;
    }
    *termios = session->termios;
    return 0;
}

/**
 * @brief Replace the session termios, first flushing any pending canonical line when leaving ICANON; returns 0 or -22.
 */
int pty_set_termios(uint32_t pty_id, const struct leonos_pty_termios *termios)
{
    struct pty_session *session = find_session(pty_id);
    if (!session || !termios) {
        return -22;
    }
    /**
 * @brief A program switching to raw mode must be able to read a line that was already typed in cooked mode instead of leaving it stranded forever.
 */
    if (pty_canonical_mode(session) &&
        (termios->c_lflag & LEONOS_PTY_LFLAG_ICANON) == 0) {
        pty_commit_canonical_input(session);
    }
    session->termios = *termios;
    return 0;
}

/**
 * @brief Copy the session window size into *winsize; returns 0, or -22 on a bad id or null pointer.
 */
int pty_get_winsize(uint32_t pty_id, struct leonos_pty_winsize *winsize)
{
    struct pty_session *session = find_session(pty_id);
    if (!session || !winsize) {
        return -22;
    }
    *winsize = session->winsize;
    return 0;
}

/**
 * @brief Update the session window size, rejecting zero dimensions; returns 0 or -22.
 */
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

/**
 * @brief Gets the process group currently receiving terminal-generated signals.
 * @param pty_id PTY identifier.
 * @param process_group Destination for the foreground process-group identifier.
 * @return Zero on success or a negative errno-style failure.
 */
int pty_get_foreground_pgid(uint32_t pty_id, uint32_t *process_group)
{
    struct pty_session *session = find_session(pty_id);
    if (!session || !process_group) {
        return -22;
    }
    *process_group = session->foreground_pgid;
    return 0;
}

/**
 * @brief Changes the group that owns foreground terminal input.
 * @param pty_id PTY identifier.
 * @param caller_pid Attached process requesting the change.
 * @param process_group New foreground process-group identifier.
 * @return Zero on success or a negative errno-style failure.
 */
int pty_set_foreground_pgid(uint32_t pty_id, uint32_t caller_pid,
                            uint32_t process_group)
{
    struct pty_session *session = find_session(pty_id);
    struct task *caller = sched_find(caller_pid);
    if (!session || !caller || !process_group || caller->pty_id != pty_id ||
        caller->process_session != session->process_session ||
        !sched_process_group_has_pty(process_group, pty_id)) {
        return -22;
    }
    session->foreground_pgid = process_group;
    return 0;
}

/**
 * @brief Free any PTY session owned by the exiting pid.
 */
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

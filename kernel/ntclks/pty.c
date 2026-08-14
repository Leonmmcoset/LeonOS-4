/*
 * LeonOS kernel pseudo-terminals: implements terminal input and output queues.
 * Connects shells and terminal applications to their controlling sessions.
 */
#include <ntclks/pty.h>
#include <ntclks/sched.h>

#define PTY_MAX 8u
#define PTY_INPUT_CAP 1024u
#define PTY_OUTPUT_CAP 8192u

struct pty_session {
    uint8_t used;
    uint8_t reserved[3];
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

/**
 * @brief Finds session.
 * @param pty_id Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
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
 * @brief Coordinates the ring push operation.
 * @param ring Input or output value used by this operation.
 * @param cap Capacity, in elements or bytes, of the related output buffer.
 * @param head Input or output value used by this operation.
 * @param tail Input or output value used by this operation.
 * @param buffer Buffer consumed or filled by this operation.
 * @param length Length, size, or element count associated with the operation.
 * @return Result, status, or value defined by this API.
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
 * @brief Coordinates the ring pop operation.
 * @param ring Input or output value used by this operation.
 * @param cap Capacity, in elements or bytes, of the related output buffer.
 * @param head Input or output value used by this operation.
 * @param tail Input or output value used by this operation.
 * @param buffer Buffer consumed or filled by this operation.
 * @param length Length, size, or element count associated with the operation.
 * @return Result, status, or value defined by this API.
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
 * @brief Coordinates the pty commit canonical input operation.
 * @param session Input or output value used by this operation.
 */
static void pty_commit_canonical_input(struct pty_session *session)
{
    if (!session || !session->canonical_length) {
        return;
    }
    /**
 * @brief Coordinates the ring push operation.
 * @param input Input or output value used by this operation.
 * @param PTY_INPUT_CAP Capacity, in elements or bytes, of the related output buffer.
 * @param input_head Input or output value used by this operation.
 * @param input_tail Input or output value used by this operation.
 * @param canonical_input Input or output value used by this operation.
 * @param canonical_length Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
    (void)ring_push(session->input, PTY_INPUT_CAP,
                    &session->input_head, &session->input_tail,
                    (const char *)session->canonical_input,
                    session->canonical_length);
    session->canonical_length = 0;
}

/**
 * @brief Coordinates the pty canonical mode operation.
 * @param session Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
static int pty_canonical_mode(const struct pty_session *session)
{
    return session &&
           (session->termios.c_lflag & LEONOS_PTY_LFLAG_ICANON) != 0;
}

/**
 * @brief Coordinates the pty init operation.
 */
void pty_init(void)
{
    for (uint32_t i = 0; i < PTY_MAX; ++i) {
        for (uint32_t j = 0; j < sizeof(sessions[i]); ++j) {
            ((uint8_t *)&sessions[i])[j] = 0;
        }
    }
}

/**
 * @brief Coordinates the pty create operation.
 * @param owner_pid Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
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
            sessions[i].winsize.ws_row = 24;
            sessions[i].winsize.ws_col = 80;
            return (int32_t)(i + 1);
        }
    }
    return -12;
}

/**
 * @brief Coordinates the pty destroy operation.
 * @param owner_pid Input or output value used by this operation.
 * @param pty_id Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
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
 * @brief Coordinates the pty is owner operation.
 * @param pty_id Input or output value used by this operation.
 * @param owner_pid Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
int pty_is_owner(uint32_t pty_id, uint32_t owner_pid)
{
    struct pty_session *session = find_session(pty_id);
    return session && session->owner_pid == owner_pid;
}

/**
 * @brief Coordinates the pty is active operation.
 * @param pty_id Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
int pty_is_active(uint32_t pty_id)
{
    return find_session(pty_id) != 0;
}

/**
 * @brief Coordinates the pty read output operation.
 * @param owner_pid Input or output value used by this operation.
 * @param pty_id Input or output value used by this operation.
 * @param buffer Buffer consumed or filled by this operation.
 * @param length Length, size, or element count associated with the operation.
 * @return Result, status, or value defined by this API.
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
 * @brief Coordinates the pty write input operation.
 * @param owner_pid Input or output value used by this operation.
 * @param pty_id Input or output value used by this operation.
 * @param buffer Buffer consumed or filled by this operation.
 * @param length Length, size, or element count associated with the operation.
 * @return Result, status, or value defined by this API.
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

/**
 * @brief Coordinates the pty read input operation.
 * @param pty_id Input or output value used by this operation.
 * @param buffer Buffer consumed or filled by this operation.
 * @param length Length, size, or element count associated with the operation.
 * @return Result, status, or value defined by this API.
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
 * @brief Coordinates the pty input available operation.
 * @param pty_id Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
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
 * @brief Coordinates the pty write output operation.
 * @param pty_id Input or output value used by this operation.
 * @param buffer Buffer consumed or filled by this operation.
 * @param length Length, size, or element count associated with the operation.
 * @return Result, status, or value defined by this API.
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
    return (int64_t)ring_push(session->output, PTY_OUTPUT_CAP,
                              &session->output_head, &session->output_tail,
                              buffer, length);
}

/**
 * @brief Coordinates the pty get termios operation.
 * @param pty_id Input or output value used by this operation.
 * @param termios Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
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
 * @brief Coordinates the pty set termios operation.
 * @param pty_id Input or output value used by this operation.
 * @param termios Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
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

/**
 * @brief Coordinates the pty get winsize operation.
 * @param pty_id Input or output value used by this operation.
 * @param winsize Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
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
 * @brief Coordinates the pty set winsize operation.
 * @param pty_id Input or output value used by this operation.
 * @param winsize Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
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
 * @brief Coordinates the pty process exit operation.
 * @param pid Input or output value used by this operation.
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

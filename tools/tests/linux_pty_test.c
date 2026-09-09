#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../kernel/ntclks/pty.c"

static struct task owner;
static unsigned signals;
void sched_set_controlling_pty(uint32_t pid, uint32_t id)
{ assert(pid == owner.pid); owner.controlling_pty_id = id; }
void sched_clear_controlling_pty(uint32_t id)
{ if (owner.controlling_pty_id == id) owner.controlling_pty_id = 0; }
struct task *sched_find(uint32_t pid) { return pid == owner.pid ? &owner : NULL; }
uint32_t sched_pty_reference_count(uint32_t id) { (void)id; return 1; }
int sched_signal_process_group(uint32_t caller, uint32_t group, int signal)
{ (void)caller; (void)group; (void)signal; ++signals; return 0; }
int sched_hangup_user_tasks_for_pty(uint32_t id, uint32_t owner_pid)
{ (void)id; (void)owner_pid; return 0; }
int sched_process_group_has_pty(uint32_t group, uint32_t id)
{ (void)group; (void)id; return 1; }
const struct framebuffer *framebuffer_get(void) { return NULL; }
void console_printf(const char *format, ...) { (void)format; }
void console_write_tty_len(const char *text, size_t count) { (void)text; (void)count; }

int main(void)
{
    owner.pid = 12;
    owner.process_session = 4;
    owner.process_group = 4;
    pty_init();
    int id = pty_create(owner.pid);
    assert(id > 0);
    uint32_t group = 99;
    assert(pty_get_foreground_pgid(id, &group) == 0 && group == 0);
    assert(pty_destroy(owner.pid, id) == 0 && signals == 0);
    pty_init();
    id = pty_create(owner.pid);
    struct leonos_pty_termios mode;
    assert(pty_get_termios(id, &mode) == 0);
    /* Linux native encodings, independent of the private PTY aliases. */
    assert((mode.c_iflag & 0x100) != 0);
    assert((mode.c_lflag & 0xb) == 0xb);
    assert(mode.c_cc[6] == 1 && mode.c_cc[5] == 0);
    mode.c_lflag &= ~0xbU;
    assert(pty_set_termios(id, &mode) == 0);
    assert(pty_write_input(owner.pid, id, "x", 1) == 1);
    char ch = 0;
    assert(pty_read_input(id, &ch, 1) == 1 && ch == 'x');
    owner.process_session = owner.pid;
    owner.process_group = owner.pid;
    owner.pty_id = id;
    owner.euid = 1000;
    assert(pty_acquire_controlling(id, owner.pid, 0, 0) == -1);
    assert(pty_acquire_controlling(id, owner.pid, 0, 1) == 0);
    assert(owner.controlling_pty_id == (uint32_t)id);
    assert(pty_acquire_controlling(id, owner.pid, 0, 1) == 0);
    int other = pty_create(owner.pid);
    assert(pty_acquire_controlling(other, owner.pid, 0, 1) == -1);
    assert(pty_get_foreground_pgid(id, &group) == 0 && group == owner.pid);
    assert(pty_destroy(owner.pid, id) == 0 && signals == 1);
    puts("PASS native PTY modes and controlling-session isolation");
    return 0;
}

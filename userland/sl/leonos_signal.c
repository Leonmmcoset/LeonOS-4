/* sl asks curses to ignore SIGINT. Keep that legacy call local because the
 * current LeonOS shared runtime intentionally does not export signal(). */
#include <signal.h>

_sig_func_ptr signal(int signum, _sig_func_ptr handler)
{
    (void)signum;
    (void)handler;
    return SIG_DFL;
}

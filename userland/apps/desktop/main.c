#include "desktop.h"
#include <leonos/pam_session.h>

int main(void)
{
    if (leonos_session_initialize() < 0) {
        perror("Initialize PAM accounts");
        return 1;
    }
    leonos_launch_use_session(1);
    desktop_run();
    return 0;
}

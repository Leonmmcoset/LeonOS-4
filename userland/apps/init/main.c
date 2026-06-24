#include <leonos/stdio.h>
#include <leonos/syscall.h>

int main(void)
{
    puts("[init.elf] LeonOS 4 userland init started");
    printf("[init.elf] pid=%d\n", getpid());
    chdir("0:/");
    puts("[init.elf] launching desktop.elf as window server");
    int status = 0;
    int reaped = wait4(2, &status, 0, 0);
    printf("[init.elf] initial child reap result=%d status=%d\n", reaped, status);
    return 0;
}

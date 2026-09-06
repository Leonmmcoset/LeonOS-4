/* Phase 0 Unix-IPC self test: blocking socketpair, SCM_RIGHTS, shm mmap,
 * uid syscalls, and a tolerant AF_INET connect probe. Run from the LeonOS
 * shell as /programs/ipctest/ipctest.elf. */
#include <errno.h>
#include <leonos/device.h>
#include <leonos/stdio.h>
#include <leonos/syscall.h>
#include <linux/input.h>
#include <linux/utsname.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>

static int check(int ok, const char *name)
{
    printf("[ipctest] %-24s %s\n", name, ok ? "OK" : "FAIL");
    return ok;
}

int main(void)
{
    int sockets[2] = {-1, -1};
    int failures = 0;
    int shm_fd = -1;
    int received_shm = -1;
    char byte = 0;
    struct utsname uts;

    printf("[ipctest] pid=%d uid=%d euid=%d gid=%d egid=%d\n",
           (int)getpid(), (int)getuid(), (int)geteuid(), (int)getgid(),
           (int)getegid());

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) < 0) {
        failures += !check(0, "socketpair");
        return failures ? 1 : 0;
    }
    failures += !check(sockets[0] >= 4 && sockets[1] >= 4, "socketpair");

    int pid = (int)fork();
    if (pid == 0) {
        close(sockets[0]);
        sleep_ms(40);
        (void)write(sockets[1], "X", 1);
        close(sockets[1]);
        _exit(0);
    }
    close(sockets[1]);
    failures += !check(read(sockets[0], &byte, 1) == 1 && byte == 'X',
                       "blocking socket read");
    close(sockets[0]);
    (void)waitpid(pid, 0, 0);

    shm_fd = open(LEONOS_DEV_SHM0, LEONOS_O_RDWR, 0);
    failures += !check(shm_fd >= 4, "open /dev/shm0");
    failures += !check(ftruncate(shm_fd, 4096) == 0, "shm ftruncate");

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0) {
        char control[64] = {0};
        char data = 'F';
        struct iovec vector = {.iov_base = &data, .iov_len = 1};
        struct msghdr message = {
            .msg_iov = &vector,
            .msg_iovlen = 1,
            .msg_control = control,
            .msg_controllen = sizeof(control),
        };
        struct cmsghdr *header = (struct cmsghdr *)control;
        header->cmsg_len = CMSG_LEN(sizeof(int));
        header->cmsg_level = SOL_SOCKET;
        header->cmsg_type = SCM_RIGHTS;
        *(int *)CMSG_DATA(header) = shm_fd;
        failures += !check(sendmsg(sockets[0], &message, 0) == 1,
                           "sendmsg SCM_RIGHTS");
        char received_data = 0;
        char received_control[64] = {0};
        struct iovec received_vector = {
            .iov_base = &received_data, .iov_len = 1};
        struct msghdr received = {
            .msg_iov = &received_vector,
            .msg_iovlen = 1,
            .msg_control = received_control,
            .msg_controllen = sizeof(received_control),
        };
        failures += !check(recvmsg(sockets[1], &received, 0) == 1 &&
                           received_data == 'F',
                           "recvmsg data");
        header = (struct cmsghdr *)received_control;
        received_shm = header && received.msg_controllen >= sizeof(*header) &&
                               header->cmsg_type == SCM_RIGHTS
                           ? *(int *)CMSG_DATA(header) : -1;
        failures += !check(received_shm >= 4, "recvmsg SCM_RIGHTS fd");
        close(sockets[0]);
        close(sockets[1]);
    } else {
        failures += !check(0, "second socketpair");
    }

    if (shm_fd >= 0) {
        uint32_t *mapping = (uint32_t *)mmap(0, 4096, PROT_READ | PROT_WRITE,
                                             MAP_SHARED, shm_fd, 0);
        failures += !check(mapping != MAP_FAILED && mapping != 0, "shm mmap");
        if (mapping != MAP_FAILED && mapping != 0) {
            mapping[0] = 0x4c4e5855u;
            if (received_shm >= 0) {
                uint32_t *peer = (uint32_t *)mmap(0, 4096,
                                                  PROT_READ | PROT_WRITE,
                                                  MAP_SHARED, received_shm, 0);
                failures += !check(peer != MAP_FAILED && peer != 0 &&
                                   peer[0] == mapping[0],
                                   "SCM_RIGHTS shm shared");
                if (peer != MAP_FAILED && peer != 0) (void)munmap(peer, 4096);
            }
            (void)munmap(mapping, 4096);
        }
    }

    failures += !check(uname(&uts) == 0 && uts.release[0], "uname");
    failures += !check(gettimeofday(0, 0) == -1 && errno == EFAULT,
                       "gettimeofday EFAULT guard");

    {
        int inet = socket(AF_INET, SOCK_STREAM, 0);
        if (inet < 0) {
            printf("[ipctest] AF_INET socket unsupported (%d)\n", errno);
            failures++;
        } else {
            struct sockaddr_in address = {
                .sin_family = AF_INET,
                .sin_port = htons(80),
                .sin_addr = {.s_addr = 0x0100007fU},
            };
            int result = connect(inet, (struct sockaddr *)&address, sizeof(address));
            printf("[ipctest] AF_INET 127.0.0.1:80 connect=%d errno=%d\n",
                   result, result < 0 ? errno : 0);
            close(inet);
        }
    }

    printf("[ipctest] %s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}

#define _GNU_SOURCE
#include <assert.h>
#include <crypt.h>
#include <sys/wait.h>
#include "../../userland/libc/src/pam_session.c"

static int fault = ENOSYS;
static unsigned dialogs;
static unsigned resource_calls;

/* Unprivileged user namespaces forbid setgroups; credentials are not under test. */
int setgroups(size_t count, const gid_t *groups)
{ (void)count; (void)groups; return 0; }
int initgroups(const char *user, gid_t group)
{ assert(!strcmp(user, "root") && group == 0); return 0; }

/* Reproduce the current kernel's missing resources at the real DSO boundary. */
int getrlimit(int resource, struct rlimit *limit)
{
    ++resource_calls;
    if (resource != RLIMIT_NOFILE && resource != RLIMIT_STACK && resource != RLIMIT_AS &&
        resource != RLIMIT_NPROC && resource != RLIMIT_SIGPENDING) {
        errno = resource_calls <= RLIM_NLIMITS ? ENOSYS : fault;
        return -1;
    }
    return syscall(SYS_prlimit64, 0, resource, NULL, limit);
}

int leonos_ui_show_password_dialog(const char *title, const char *label, char *value, uint32_t size)
{ (void)title; (void)label; (void)value; (void)size; ++dialogs; return 0; }
int leonos_ui_show_input_dialog(const char *title, const char *label, char *value, uint32_t size)
{ (void)title; (void)label; (void)value; (void)size; ++dialogs; return 0; }
int leonos_ui_show_message_box(const char *title, const char *message, const char *button)
{ (void)title; (void)message; (void)button; ++dialogs; return 1; }

static void write_file(const char *path, const char *text)
{
    FILE *file = fopen(path, "w");
    assert(file && fputs(text, file) >= 0 && fclose(file) == 0);
}

static void run_case(const char *label, const char *secret, const char *limits, int error, int resource_error)
{
    pid_t child = fork();
    assert(child >= 0);
    if (!child) {
        fault = resource_error;
        resource_calls = 0;
        write_file("/etc/security/limits.conf", limits);
        struct leonos_user_info user;
        char password[64];
        snprintf(password, sizeof(password), "%s", secret);
        int result = leonos_pam_login("root", password, &user);
        int actual = result < 0 ? errno : 0;
        fprintf(stderr, "[pam-login-case] %s result=%d errno=%d expected=%d\n", label, result, actual, error);
        assert(actual == error && password[0] == 0 && dialogs == 0);
        if (!error) {
            assert(user.uid == 0);
            if (*limits) {
                struct rlimit current;
                assert(getrlimit(RLIMIT_NOFILE, &current) == 0 && current.rlim_cur == 64);
            }
            assert(leonos_auth_logout() == 0);
            assert(leonos_pam_session_wait() == 0);
        } else assert(access(SESSION, F_OK) == -1 && errno == ENOENT);
        _exit(0);
    }
    int status;
    assert(waitpid(child, &status, 0) == child);
    if (!WIFEXITED(status) || WEXITSTATUS(status)) fprintf(stderr, "case %s wait status=%d\n", label, status);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
}

int main(void)
{
    struct conversation context = {0};
    struct pam_message prompt = {.msg_style = PAM_PROMPT_ECHO_OFF, .msg = "Password:"};
    const struct pam_message *messages[] = {&prompt};
    struct pam_response *response = NULL;
    assert(conversation(1, messages, &response, &context) == PAM_CONV_ERR && context.cancelled);
    context.cancelled = 0;
    messages[0] = NULL;
    assert(conversation(1, messages, &response, &context) == PAM_CONV_ERR && !context.cancelled);
    dialogs = 0;
    assert(mkdir("/etc/pam.d", 0755) == 0);
    assert(mkdir("/etc/security", 0755) == 0);
    assert(mkdir("/run/leonos", 0755) == 0);
    write_file("/etc/passwd", "root:x:0:0:root:/root:/bin/sh\n");
    write_file("/etc/group", "root:x:0:\n");
    char salt[CRYPT_GENSALT_OUTPUT_SIZE], text[512];
    struct crypt_data data = {0};
    assert(crypt_gensalt_rn("$y$", 0, NULL, 0, salt, sizeof(salt)));
    char *hash = crypt_rn("AbC123!", salt, &data, sizeof(data));
    assert(hash && *hash == '$');
    snprintf(text, sizeof(text), "root:%s:20000:0:99999:7:::\n", hash);
    write_file("/etc/shadow", text);
    write_file("/etc/pam.d/leonos-gui",
               "auth required pam_unix.so\naccount required pam_unix.so\n"
               "session required pam_limits.so\n");
    run_case("wrong lowercase", "abc123!", "", EACCES, ENOSYS);
    run_case("correct mixed case", "AbC123!", "", 0, ENOSYS);
    run_case("supported nofile enforced", "AbC123!", "root soft nofile 64\n", 0, ENOSYS);
    run_case("unsupported cpu denied", "AbC123!", "root hard cpu 1\n", EACCES, ENOSYS);
    run_case("unexpected resource failure", "AbC123!", "", EIO, EIO);
    puts("[pam-login-case] PASS");
    return 0;
}

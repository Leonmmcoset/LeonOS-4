#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <security/pam_appl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static unsigned failures;
#define CHECK(c) do { if (!(c)) { printf("[password-policy] FAIL line=%d errno=%d %s\n", \
    __LINE__, errno, #c); ++failures; } } while (0)

struct answers { const char *value; unsigned prompts; int cancel; };

static int converse(int count, const struct pam_message **messages,
                    struct pam_response **responses, void *context)
{
    struct answers *answers = context;
    if (count <= 0 || count > PAM_MAX_NUM_MSG) return PAM_CONV_ERR;
    struct pam_response *result = calloc((size_t)count, sizeof(*result));
    if (!result) return PAM_BUF_ERR;
    for (int i = 0; i < count; ++i) {
        switch (messages[i]->msg_style) {
        case PAM_PROMPT_ECHO_OFF:
        case PAM_PROMPT_ECHO_ON:
            ++answers->prompts;
            if (answers->cancel) goto failed;
            result[i].resp = strdup(answers->value);
            if (!result[i].resp) goto failed;
            break;
        case PAM_TEXT_INFO:
        case PAM_ERROR_MSG:
            break;
        default:
            goto failed;
        }
    }
    *responses = result;
    return PAM_SUCCESS;
failed:
    for (int i = 0; i < count; ++i) {
        if (result[i].resp) explicit_bzero(result[i].resp, strlen(result[i].resp));
        free(result[i].resp);
    }
    free(result);
    *responses = NULL;
    return PAM_CONV_ERR;
}

static void config(const char *path, const char *value, mode_t mode)
{
    FILE *file = fopen(path, "w");
    CHECK(file != NULL);
    if (!file) return;
    CHECK(fchmod(fileno(file), mode) == 0);
    CHECK(fputs(value, file) >= 0 && fflush(file) == 0 && fsync(fileno(file)) == 0);
    CHECK(fclose(file) == 0);
}

static char *shadow(void)
{
    FILE *file = fopen("/etc/shadow", "r");
    if (!file) return NULL;
    char *data = NULL;
    size_t size = 0;
    FILE *memory = open_memstream(&data, &size);
    if (!memory) { fclose(file); return NULL; }
    char buffer[1024];
    size_t got;
    while ((got = fread(buffer, 1, sizeof(buffer), file)))
        CHECK(fwrite(buffer, 1, got, memory) == got);
    CHECK(!ferror(file));
    CHECK(fclose(file) == 0 && fclose(memory) == 0);
    return data;
}

static int transaction(const char *service, const char *password, int change, int cancel)
{
    struct answers answers = {.value = password, .cancel = cancel};
    struct pam_conv conversation = {converse, &answers};
    pam_handle_t *handle = NULL;
    int result = pam_start(service, "pam_fixture", &conversation, &handle);
    if (result == PAM_SUCCESS)
        result = change ? pam_chauthtok(handle, 0) : pam_authenticate(handle, 0);
    if (handle) CHECK(pam_end(handle, result) == PAM_SUCCESS);
    return result;
}

static void rejected(const char *name, const char *value, int cancel)
{
    char *before = shadow();
    int result = transaction("passwd", value, 1, cancel);
    char *after = shadow();
    printf("[password-policy] case=%s result=%d\n", name, result);
    CHECK(result != PAM_SUCCESS && before && after && !strcmp(before, after));
    if (before) { explicit_bzero(before, strlen(before)); free(before); }
    if (after) { explicit_bzero(after, strlen(after)); free(after); }
}

static void accepted(const char *name, const char *value)
{
    int result = transaction("passwd", value, 1, 0);
    printf("[password-policy] case=%s result=%d\n", name, result);
    CHECK(result == PAM_SUCCESS);
    CHECK(transaction("policy-auth", value, 0, 0) == PAM_SUCCESS);
    CHECK(transaction("policy-auth", "wrong-password", 0, 0) != PAM_SUCCESS);
    char *data = shadow();
    CHECK(data && strstr(data, "pam_fixture:$y$") && !strstr(data, "$pbkdf2-sha256$"));
    if (data) { explicit_bzero(data, strlen(data)); free(data); }
    struct stat st;
    CHECK(stat("/etc/shadow", &st) == 0 && st.st_uid == 0 && (st.st_mode & 0777) == 0600);
}

static void passwd_command(uid_t caller, const char *input, int expected)
{
    int fds[2];
    CHECK(pipe2(fds, O_CLOEXEC) == 0);
    CHECK(write(fds[1], input, strlen(input)) == (ssize_t)strlen(input));
    CHECK(close(fds[1]) == 0);
    pid_t child = fork();
    CHECK(child >= 0);
    if (!child) {
        alarm(45);
        if (setsid() < 0 || dup2(fds[0], 0) < 0) _exit(125);
        close(fds[0]);
        if (caller && (setgroups(0, NULL) || setresgid(caller, caller, caller) ||
                       setresuid(caller, caller, caller))) _exit(124);
        execl("/usr/bin/passwd", "passwd", "pam_fixture", (char *)NULL);
        _exit(126);
    }
    close(fds[0]);
    int status = 0;
    CHECK(waitpid(child, &status, 0) == child);
    printf("[password-policy] passwd caller=%u status=%d\n", caller, status);
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == expected);
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    alarm(300);
    puts("[password-policy] BEGIN real PAM password policy and shadow passwd");
    CHECK(geteuid() == 0);
    /* The legacy broker exports accounts before publishing its listening socket. */
    if (access("/usr/lib/leonos/apps/authd/authd.elf", F_OK) == 0) {
        for (unsigned i = 0; i < 200 && access("/run/leonos/authd.sock", F_OK) < 0; ++i)
            usleep(100000);
        CHECK(access("/run/leonos/authd.sock", F_OK) == 0);
        if (failures) return 1;
    }
    CHECK(mkdir("/etc", 0755) == 0 || errno == EEXIST);
    CHECK(mkdir("/etc/pam.d", 0755) == 0 || errno == EEXIST);
    config("/etc/passwd", "root:x:0:0::/root:/bin/sh\npam_fixture:x:1000:1000::/tmp:/bin/sh\n", 0644);
    config("/etc/group", "root:x:0:\npam_fixture:x:1000:\n", 0644);
    config("/etc/shadow", "root:!:1:0:99999:7:::\npam_fixture:!:1:0:99999:7:::\n", 0600);
    config("/etc/gshadow", "root:!::\npam_fixture:!::\n", 0600);
    config("/etc/login.defs", "ENCRYPT_METHOD YESCRYPT\n", 0644);
    config("/etc/pam.d/passwd", "password requisite /lib/security/pam_leonos_password.so\n"
        "password required /lib/security/pam_unix.so use_authtok yescrypt\n", 0644);
    config("/etc/pam.d/policy-auth", "auth required /lib/security/pam_unix.so nodelay\n", 0644);
    config("/etc/pam.d/other", "auth required /lib/security/pam_deny.so\n"
        "password required /lib/security/pam_deny.so\n", 0644);
    rejected("empty", "", 0);
    rejected("space", "contains space", 0);
    rejected("tab", "has\ttab", 0);
    rejected("unicode-space", "a\xe3\x80\x80z", 0);
    rejected("invalid-utf8", "\xc0\xaf", 0);
    rejected("33-characters", "123456789012345678901234567890123", 0);
    rejected("cancel", "ignored", 1);
    accepted("one-character", "r");
    accepted("32-ascii", "12345678901234567890123456789012");
    char unicode[129];
    for (unsigned i = 0; i < 32; ++i) memcpy(unicode + i * 4, "\xf0\x9f\x94\x91", 4);
    unicode[128] = 0;
    accepted("32-unicode-128-bytes", unicode);
    CHECK(mkdir("/etc/security", 0755) == 0 || errno == EEXIST);
    config("/etc/pam.d/history", "password requisite /lib/security/pam_leonos_password.so\n"
        "password requisite /lib/security/pam_pwhistory.so use_authtok enforce_for_root remember=3 retry=1\n"
        "password required /lib/security/pam_unix.so use_authtok yescrypt\n", 0644);
    CHECK(transaction("history", "history-first", 1, 0) == PAM_SUCCESS);
    CHECK(transaction("history", "history-second", 1, 0) == PAM_SUCCESS);
    CHECK(transaction("history", "history-first", 1, 0) == PAM_MAXTRIES);
    CHECK(transaction("policy-auth", "history-second", 0, 0) == PAM_SUCCESS);
    char *history_before = shadow();
    config("/etc/security/opasswd", "pam_fixture:1000:1:$y$invalid\n", 0600);
    CHECK(transaction("history", "history-third", 1, 0) == PAM_MAXTRIES);
    char *history_after = shadow();
    CHECK(history_before && history_after && !strcmp(history_before, history_after));
    if (history_before) { explicit_bzero(history_before, strlen(history_before)); free(history_before); }
    if (history_after) { explicit_bzero(history_after, strlen(history_after)); free(history_after); }
    passwd_command(0, "q\nq\n", 0);
    CHECK(transaction("policy-auth", "q", 0, 0) == PAM_SUCCESS);
    if (argc == 2 && !strcmp(argv[1], "--root-only")) {
        puts("[password-policy] nonroot-passwd not run: single-ID host namespace");
    } else {
        passwd_command(1000, "q\nx\nx\n", 0);
        CHECK(transaction("policy-auth", "x", 0, 0) == PAM_SUCCESS);
        char *before = shadow();
        passwd_command(1000, "wrong-password\ny\ny\n", 10);
        char *after = shadow();
        CHECK(before && after && !strcmp(before, after));
        if (before) { explicit_bzero(before, strlen(before)); free(before); }
        if (after) { explicit_bzero(after, strlen(after)); free(after); }
    }
    printf("[password-policy] DONE failures=%u\n", failures);
    return failures != 0;
}

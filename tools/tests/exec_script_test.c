#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../kernel/ntclks/syscall.c"

static bool fail_allocation;
void *kernel_malloc(size_t size) { return fail_allocation ? NULL : malloc(size); }
void kernel_free(void *memory) { free(memory); }

int main(void)
{
    char header[256], *name, *argument;
    memset(header, 0, sizeof(header));
    strcpy(header, "#! \t/bin/sh \targ one \t\n");
    assert(script_header(header, &name, &argument) == 1);
    assert(!strcmp(name, "/bin/sh") && !strcmp(argument, "arg one"));
    memset(header, 'a', sizeof(header));
    header[0] = '#'; header[1] = '!';
    assert(script_header(header, &name, &argument) == -LINUX_ENOEXEC);
    memset(header + 2, ' ', sizeof(header) - 2);
    assert(script_header(header, &name, &argument) == -LINUX_ENOEXEC);
    memset(header, 0, sizeof(header));
    strcpy(header, "#!\n");
    assert(script_header(header, &name, &argument) == -LINUX_ENOEXEC);
    strcpy(header, "#!");
    assert(script_header(header, &name, &argument) == 1 && !name[0] && !argument);
    strcpy(header, "#!/bin/sh\r\n");
    assert(script_header(header, &name, &argument) == 1 && !strcmp(name, "/bin/sh\r"));
    memset(header, 'a', sizeof(header));
    memcpy(header, "#!/bin/sh ", 10);
    assert(script_header(header, &name, &argument) == 1 && !strcmp(name, "/bin/sh"));
    assert(strlen(argument) == 245);
    uint32_t random = 871;
    for (unsigned iteration = 0; iteration < 20000; ++iteration) {
        for (unsigned i = 0; i < sizeof(header); ++i) {
            random = random * 1664525u + 1013904223u;
            header[i] = (char)(random >> 24);
        }
        header[0] = '#'; header[1] = '!';
        if (script_header(header, &name, &argument) == 1) {
            assert(name >= header + 2 && name < header + sizeof(header));
            assert(strlen(name) < sizeof(header));
            if (argument) assert(argument > name && strlen(argument) < sizeof(header));
        }
    }
    struct exec_params_kernel params = {0};
    assert(!exec_append_string(&params, "old argv0", false));
    assert(!exec_append_string(&params, "tail", false));
    assert(!exec_append_string(&params, "NAME=value", true));
    struct exec_params_kernel original = params;
    fail_allocation = true;
    assert(exec_script_arguments(&params, "/bin/sh", "-e", "script") == -LEONOS_ENOMEM);
    assert(!memcmp(&params, &original, sizeof(params)));
    fail_allocation = false;
    assert(!exec_script_arguments(&params, "/bin/sh", "arg one", "script"));
    assert(params.argc == 4 && params.envc == 1);
    assert(!strcmp(params.argv[0], "/bin/sh") && !strcmp(params.argv[1], "arg one"));
    assert(!strcmp(params.argv[2], "script") && !strcmp(params.argv[3], "tail"));
    assert(!params.argv[4] && !strcmp(params.envp[0], "NAME=value") && !params.envp[1]);
    while (params.argc < SCHED_EXEC_ARG_MAX) assert(!exec_append_string(&params, "x", false));
    original = params;
    assert(exec_script_arguments(&params, "/bin/sh", "-e", "script") == -LEONOS_E2BIG);
    assert(!memcmp(&params, &original, sizeof(params)));
    puts("PASS production shebang parser, bounded fuzz, argv rewrite and allocation rollback");
}

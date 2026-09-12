#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../userland/apps/installer/installer_setup.h"

int main(int argc, char **argv)
{
    assert(argc == 3);
    struct installer_setup setup = {0};
    assert(installer_setup_load(&setup, argv[1], argv[2]) == 0);
    assert(setup.component_available[0] && setup.component_available[1]);
    char line[300], source[600];
    for (unsigned combination = 0; combination < 4; ++combination) {
        setup.component_selected[0] = !!(combination & 1);
        setup.component_selected[1] = !!(combination & 2);
        FILE *file = fopen(argv[1], "r");
        assert(file);
        while (fgets(line, sizeof(line), file)) {
            char *tab = strchr(line, '\t'), *newline = strchr(line, '\n');
            assert(tab && newline);
            *tab++ = 0;
            *newline = 0;
            int expected = setup.component_selected[!strcmp(line, "musl-gcc")];
            snprintf(source, sizeof(source), "%s%s", argv[2], tab);
            assert(installer_setup_include(&setup, source) == expected);
            strcat(source, "/nested-file");
            assert(installer_setup_include(&setup, source) == expected);
            snprintf(source, sizeof(source), "%s%s-other", argv[2], tab);
            assert(installer_setup_include(&setup, source));
        }
        fclose(file);
        snprintf(source, sizeof(source), "%s/usr/bin/vim", argv[2]);
        assert(installer_setup_include(&setup, source));
        assert(installer_setup_include(&setup, "/install/esp/leonos/kernel.sys"));
    }
    strcpy(setup.username, "alice");
    strcpy(setup.password, "user!");
    strcpy(setup.password_confirm, "user!");
    strcpy(setup.root_password, "root!");
    strcpy(setup.root_password_confirm, "root!");
    assert(installer_setup_valid(&setup));
    strcpy(setup.username, "root");
    assert(!installer_setup_valid(&setup));
    strcpy(setup.username, "../alice");
    assert(!installer_setup_valid(&setup));
    strcpy(setup.username, "alice");
    setup.root_password[0] = 0;
    setup.root_password_confirm[0] = 0;
    assert(!installer_setup_valid(&setup));
    strcpy(setup.root_password, "a b");
    strcpy(setup.root_password_confirm, "a b");
    assert(!installer_setup_valid(&setup));
    puts("installer components: all four selections, owned paths and account validation PASS");
}

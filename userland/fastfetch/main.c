#include "leonos_fastfetch.h"

#include "common/option.h"
#include "logo/logo.h"
#include "modules/break/break.h"
#include "modules/colors/colors.h"
#include "modules/datetime/datetime.h"
#include "modules/kernel/kernel.h"
#include "modules/memory/memory.h"
#include "modules/os/os.h"
#include "modules/processes/processes.h"
#include "modules/separator/separator.h"
#include "modules/title/title.h"
#include "modules/uptime/uptime.h"
#include "modules/version/version.h"
#include "options/display.h"

#include <stdio.h>
#include <string.h>

#define MODULE_OPTION(name) \
    [[gnu::cleanup(ffDestroy##name##Options)]] FF##name##Options options; \
    ffInit##name##Options(&options)

static const char default_structure[] =
    "Title:Separator:OS:Kernel:CPU:Uptime:Processes:Memory:Shell:Terminal:Colors";

static void print_help(void)
{
    puts("Fastfetch 2.67.0 for LeonOS");
    puts("");
    puts("Usage: fastfetch [options]");
    puts("");
    puts("  -h, --help                       Show this help");
    puts("  -v, --version                    Show the upstream Fastfetch version");
    puts("      --pipe [bool]                Disable ANSI colors and the logo");
    puts("  -l, --logo <name|small|none>     Select an upstream ASCII logo");
    puts("      --list-logos                 List the built-in ASCII logos");
    puts("      --print-logos                Print every built-in ASCII logo");
    puts("      --logo-position <left|top|right>");
    puts("      --logo-color-1 .. --logo-color-9 <color>");
    puts("      --logo-width/--logo-height <n>");
    puts("      --logo-padding[-top|-bottom|-left|-right] <n>");
    puts("      --color[-keys|-title|-output|-separator] <color>");
    puts("      --separator <text>, --key-width <n>, --key-padding-left <n>");
    puts("      --key-type <none|string|icon|both>, --bright-color [bool]");
    puts("      --size-*, --duration-*, --percent-*, --bar-* <value>");
    puts("  -s, --structure <modules>        Colon-separated output modules");
    puts("      --structure-disabled <modules>  Omit colon-separated modules");
    puts("      --print-structure             Print the active structure");
    puts("      --list-modules                List modules available on LeonOS");
    puts("");
    puts("Available modules: Title, Separator, OS, Kernel, Uptime, Processes,");
    puts("Memory, CPU, DateTime, Break, Shell, Terminal, Colors, Version.");
    puts("Only upstream ASCII logos are available; files, images, JSON/config,");
    puts("dynamic output, and modules requiring Linux host interfaces are disabled.");
}

static void print_modules(void)
{
    puts("Title\nSeparator\nOS\nKernel\nCPU\nUptime\nProcesses\nMemory\nDateTime\nBreak\nShell\nTerminal\nColors\nVersion");
}

static bool equals_ign_case_n(const char* value, size_t length, const char* expected)
{
    return strlen(expected) == length && strncasecmp(value, expected, length) == 0;
}

static bool token_in_list(const char* list, const char* value, size_t value_length)
{
    if (!list || !list[0]) {
        return false;
    }
    for (const char* token = list;;) {
        const char* end = strchr(token, ':');
        size_t length = end ? (size_t)(end - token) : strlen(token);
        if (equals_ign_case_n(token, length, value)) {
            return true;
        }
        if (!end) {
            return false;
        }
        token = end + 1;
    }
}

static bool parse_logo_option(const char* key, const char* value)
{
    FFOptionsLogo* logo = &instance.config.logo;
    if (strcmp(key, "-l") == 0 || strcmp(key, "--logo") == 0) {
        if (!value) {
            fprintf(stderr, "fastfetch: usage: %s <none|small|logo-name>\n", key);
            return false;
        }
        if (strcasecmp(value, "none") == 0) {
            logo->type = FF_LOGO_TYPE_NONE;
        } else if (strcasecmp(value, "small") == 0) {
            logo->type = FF_LOGO_TYPE_SMALL;
        } else {
            ffStrbufSetS(&logo->source, value);
            if (logo->type == FF_LOGO_TYPE_NONE) {
                logo->type = FF_LOGO_TYPE_AUTO;
            }
        }
        return true;
    }
    if (strcmp(key, "--logo-type") == 0) {
        if (!value || (strcasecmp(value, "auto") != 0 && strcasecmp(value, "builtin") != 0 &&
                       strcasecmp(value, "small") != 0 && strcasecmp(value, "none") != 0)) {
            fprintf(stderr, "fastfetch: %s only supports auto, builtin, small, or none\n", key);
            return false;
        }
        logo->type = strcasecmp(value, "small") == 0 ? FF_LOGO_TYPE_SMALL :
            strcasecmp(value, "none") == 0 ? FF_LOGO_TYPE_NONE : FF_LOGO_TYPE_AUTO;
        return true;
    }
    if (strncmp(key, "--logo-color-", strlen("--logo-color-")) == 0 &&
        key[strlen("--logo-color-")] >= '1' && key[strlen("--logo-color-")] <= '9' &&
        key[strlen("--logo-color-") + 1] == '\0') {
        if (!value) {
            fprintf(stderr, "fastfetch: usage: %s <color>\n", key);
            return false;
        }
        ffOptionParseColor(value, &logo->colors[key[strlen("--logo-color-")] - '1']);
        return true;
    }
    if (strcmp(key, "--logo-width") == 0) {
        logo->width = ffOptionParseUInt32(key, value);
        return true;
    }
    if (strcmp(key, "--logo-height") == 0) {
        logo->height = ffOptionParseUInt32(key, value);
        return true;
    }
    if (strcmp(key, "--logo-padding") == 0) {
        logo->paddingLeft = logo->paddingRight = ffOptionParseUInt32(key, value);
        return true;
    }
    if (strcmp(key, "--logo-padding-top") == 0) {
        logo->paddingTop = ffOptionParseUInt32(key, value);
        return true;
    }
    if (strcmp(key, "--logo-padding-bottom") == 0) {
        logo->paddingBottom = ffOptionParseUInt32(key, value);
        return true;
    }
    if (strcmp(key, "--logo-padding-left") == 0) {
        logo->paddingLeft = ffOptionParseUInt32(key, value);
        return true;
    }
    if (strcmp(key, "--logo-padding-right") == 0) {
        logo->paddingRight = ffOptionParseUInt32(key, value);
        return true;
    }
    if (strcmp(key, "--logo-position") == 0) {
        if (!value) {
            fprintf(stderr, "fastfetch: usage: %s <left|top|right>\n", key);
            return false;
        }
        if (strcasecmp(value, "left") == 0) {
            logo->position = FF_LOGO_POSITION_LEFT;
        } else if (strcasecmp(value, "top") == 0) {
            logo->position = FF_LOGO_POSITION_TOP;
        } else if (strcasecmp(value, "right") == 0) {
            logo->position = FF_LOGO_POSITION_RIGHT;
        } else {
            fprintf(stderr, "fastfetch: unknown logo position: %s\n", value);
            return false;
        }
        return true;
    }
    return false;
}

static void print_module(const char* name, size_t length)
{
    if (equals_ign_case_n(name, length, "Title")) {
        MODULE_OPTION(Title); ffPrintTitle(&options);
    } else if (equals_ign_case_n(name, length, "Separator")) {
        MODULE_OPTION(Separator); ffPrintSeparator(&options);
    } else if (equals_ign_case_n(name, length, "OS")) {
        MODULE_OPTION(OS); ffPrintOS(&options);
    } else if (equals_ign_case_n(name, length, "Kernel")) {
        MODULE_OPTION(Kernel); ffPrintKernel(&options);
    } else if (equals_ign_case_n(name, length, "CPU")) {
        ffLeonOSPrintCPU();
    } else if (equals_ign_case_n(name, length, "Uptime")) {
        MODULE_OPTION(Uptime); ffPrintUptime(&options);
    } else if (equals_ign_case_n(name, length, "Processes")) {
        MODULE_OPTION(Processes); ffPrintProcesses(&options);
    } else if (equals_ign_case_n(name, length, "Memory")) {
        MODULE_OPTION(Memory); ffPrintMemory(&options);
    } else if (equals_ign_case_n(name, length, "DateTime")) {
        MODULE_OPTION(DateTime); ffPrintDateTime(&options);
    } else if (equals_ign_case_n(name, length, "Break")) {
        MODULE_OPTION(Break); ffPrintBreak(&options);
    } else if (equals_ign_case_n(name, length, "Shell")) {
        ffLeonOSPrintStatic("Shell", "LeonOS shell");
    } else if (equals_ign_case_n(name, length, "Terminal")) {
        ffLeonOSPrintStatic("Terminal", "LeonOS PTY");
    } else if (equals_ign_case_n(name, length, "Colors")) {
        if (!instance.config.display.pipe) {
            MODULE_OPTION(Colors); ffPrintColors(&options);
        }
    } else if (equals_ign_case_n(name, length, "Version")) {
        MODULE_OPTION(Version); ffPrintVersion(&options);
    } else {
        fprintf(stderr, "fastfetch: LeonOS does not provide module: %.*s\n", (int)length, name);
    }
}

static void print_structure(const char* structure, const char* disabled)
{
    for (const char* token = structure; token && *token;) {
        const char* end = strchr(token, ':');
        size_t length = end ? (size_t)(end - token) : strlen(token);
        if (length > 0 && !token_in_list(disabled, token, length)) {
            print_module(token, length);
        }
        if (!end) {
            break;
        }
        token = end + 1;
    }
}

int main(int argc, char** argv)
{
    const char* structure = default_structure;
    const char* disabled = "";
    bool print_structure_only = false;

    ffLeonOSInit();
    for (int index = 1; index < argc; ++index) {
        const char* argument = argv[index];
        const char* value = index + 1 < argc && argv[index + 1][0] != '-' ? argv[index + 1] : nullptr;
        bool consumes_value = false;

        if (strcmp(argument, "-h") == 0 || strcmp(argument, "--help") == 0) {
            print_help();
            ffLeonOSDestroy();
            return 0;
        }
        if (strcmp(argument, "-v") == 0 || strcmp(argument, "--version") == 0) {
            puts("fastfetch 2.67.0 (LeonOS port)");
            ffLeonOSDestroy();
            return 0;
        }
        if (strcmp(argument, "--list-logos") == 0) {
            ffLeonOSLogoList();
            ffLeonOSDestroy();
            return 0;
        }
        if (strcmp(argument, "--print-logos") == 0) {
            ffLeonOSLogoPrintAll();
            ffLeonOSDestroy();
            return 0;
        }
        if (strcmp(argument, "--list-modules") == 0) {
            print_modules();
            ffLeonOSDestroy();
            return 0;
        }
        if (strcmp(argument, "--print-structure") == 0) {
            print_structure_only = true;
            continue;
        }
        if (strcmp(argument, "-s") == 0 || strcmp(argument, "--structure") == 0) {
            if (!value) {
                fprintf(stderr, "fastfetch: usage: %s <modules>\n", argument);
                ffLeonOSDestroy();
                return 2;
            }
            structure = value;
            consumes_value = true;
        } else if (strcmp(argument, "--structure-disabled") == 0) {
            if (!value) {
                fprintf(stderr, "fastfetch: usage: %s <modules>\n", argument);
                ffLeonOSDestroy();
                return 2;
            }
            disabled = value;
            consumes_value = true;
        } else if (parse_logo_option(argument, value)) {
            consumes_value = value != nullptr;
        } else if (ffOptionsParseDisplayCommandLine(&instance.config.display, argument, value)) {
            consumes_value = value != nullptr;
        } else {
            fprintf(stderr, "fastfetch: unsupported option: %s\n", argument);
            ffLeonOSDestroy();
            return 2;
        }
        if (consumes_value) {
            ++index;
        }
    }

    if (print_structure_only) {
        puts(structure);
        ffLeonOSDestroy();
        return 0;
    }
    if (instance.config.display.pipe) {
        instance.config.logo.type = FF_LOGO_TYPE_NONE;
    }
    if (!ffLeonOSLogoPrint()) {
        ffLeonOSDestroy();
        return 2;
    }
    print_structure(structure, disabled);
    ffLogoPrintRemaining();
    ffLeonOSDestroy();
    return 0;
}

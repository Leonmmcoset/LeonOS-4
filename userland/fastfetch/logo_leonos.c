#include "leonos_fastfetch.h"

#include "common/color.h"
#include "common/printing.h"
#include "common/textModifier.h"
#include "common/strutil.h"
#include "logo/logo.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

typedef struct FFLeonOSLogoLine {
    FFstrbuf chars;
    uint32_t width;
} FFLeonOSLogoLine;

static const char* const leonos_logo =
    "      .--------.\n"
    "     /  LeonOS  \\\n"
    "    /------------\\\n"
    "    |     4      |\n"
    "    '------------'";

static void clear_logo_cache(void)
{
    FF_LIST_FOR_EACH (FFLeonOSLogoLine, line, instance.state.logoLineCache.lines) {
        ffStrbufDestroy(&line->chars);
    }
    ffListDestroy(&instance.state.logoLineCache.lines);
    instance.state.logoLineCache.nextLine = 0;
    instance.state.logoLineCache.rightOffset = 0;
}

static void push_logo_line(const FFstrbuf* chars, uint32_t width)
{
    FFLeonOSLogoLine* line = FF_LIST_ADD(FFLeonOSLogoLine, instance.state.logoLineCache.lines);
    if (chars) {
        ffStrbufInitCopy(&line->chars, chars);
        if (!instance.config.display.pipe) {
            ffStrbufAppendS(&line->chars, FASTFETCH_TEXT_MODIFIER_RESET);
        }
    } else {
        ffStrbufInit(&line->chars);
    }
    line->width = width;
}

static void apply_logo_colors(const FFlogo* logo, bool replace_colors)
{
    if (instance.config.display.colorTitle.length == 0) {
        ffStrbufAppendS(&instance.config.display.colorTitle, logo->colorTitle ? logo->colorTitle : logo->colors[0]);
    }
    if (instance.config.display.colorKeys.length == 0) {
        ffStrbufAppendS(&instance.config.display.colorKeys, logo->colorKeys ? logo->colorKeys : logo->colors[1]);
    }
    if (!replace_colors) {
        return;
    }

    for (uint32_t index = 0; index < FASTFETCH_LOGO_MAX_COLORS && logo->colors[index]; ++index) {
        if (instance.config.logo.colors[index].length == 0) {
            ffStrbufAppendS(&instance.config.logo.colors[index], logo->colors[index]);
        }
    }
}

static void apply_leonos_colors(void)
{
    if (instance.config.display.colorTitle.length == 0) {
        ffStrbufAppendS(&instance.config.display.colorTitle, FF_COLOR_FG_CYAN);
    }
    if (instance.config.display.colorKeys.length == 0) {
        ffStrbufAppendS(&instance.config.display.colorKeys, FF_COLOR_FG_CYAN);
    }
    if (instance.config.logo.colors[0].length == 0) {
        ffStrbufAppendS(&instance.config.logo.colors[0], FF_COLOR_FG_CYAN);
    }
}

static void build_logo_cache(const char* data, bool replace_colors)
{
    FFOptionsLogo* options = &instance.config.logo;
    uint32_t max_width = options->width;
    uint32_t parsed_height = 0;
    FF_STRBUF_AUTO_DESTROY carry_color = ffStrbufCreate();

    clear_logo_cache();
    if (replace_colors && !instance.config.display.pipe) {
        ffStrbufSetF(&carry_color, "\e[%sm", options->colors[0].chars);
    }
    for (uint32_t index = 0; index < options->paddingTop; ++index) {
        push_logo_line(nullptr, 0);
    }

    while (*data) {
        FF_STRBUF_AUTO_DESTROY line = ffStrbufCreateA(128);
        uint32_t width = 0;
        if (!instance.config.display.pipe && instance.config.display.brightColor) {
            ffStrbufAppendS(&line, FASTFETCH_TEXT_MODIFIER_BOLT);
        }
        if (carry_color.length > 0) {
            ffStrbufAppend(&line, &carry_color);
        }
        if (options->paddingLeft) {
            ffStrbufAppendNC(&line, options->paddingLeft, ' ');
            width += options->paddingLeft;
        }

        while (*data && *data != '\n') {
            if (replace_colors && *data == '$') {
                ++data;
                if (*data >= '1' && *data <= '9') {
                    if (!instance.config.display.pipe) {
                        ffStrbufSetF(&carry_color, "\e[%sm", instance.config.logo.colors[*data - '1'].chars);
                        ffStrbufAppend(&line, &carry_color);
                    }
                    ++data;
                    continue;
                }
                if (*data == '$') {
                    ++data;
                }
                ffStrbufAppendC(&line, '$');
                ++width;
                continue;
            }

            uint8_t char_width = 0;
            uint8_t bytes = ffUtf8CharLenWidth(data, UINT32_MAX, &char_width);
            if (bytes == 0) {
                break;
            }
            ffStrbufAppendNS(&line, bytes, data);
            data += bytes;
            width += char_width;
        }
        push_logo_line(&line, width);
        if (width > max_width) {
            max_width = width;
        }
        ++parsed_height;
        if (*data == '\n') {
            ++data;
        }
    }

    if (options->height > parsed_height) {
        parsed_height = options->height;
    }
    instance.state.logoHeight = options->paddingTop + parsed_height;
    instance.state.logoWidth = options->position == FF_LOGO_POSITION_LEFT
        ? max_width + options->paddingRight
        : 0;
    instance.state.logoLineCache.rightOffset = max_width + options->paddingRight - 1;
    while (instance.state.logoLineCache.lines.length < instance.state.logoHeight + 1) {
        push_logo_line(nullptr, 0);
    }
}

static bool logo_has_name(const FFlogo* logo, const FFstrbuf* name, bool small)
{
    for (const char* const* logo_name = logo->names; *logo_name; ++logo_name) {
        if (small) {
            size_t suffix = strlen("_small");
            size_t length = strlen(*logo_name);
            if (length > suffix && strcmp(*logo_name + length - suffix, "_small") == 0 &&
                name->length == length - suffix && strncasecmp(*logo_name, name->chars, length - suffix) == 0) {
                return true;
            }
        }
        if (ffStrbufIgnCaseEqualS(name, *logo_name)) {
            return true;
        }
    }
    return false;
}

static const FFlogo* get_builtin_logo(const FFstrbuf* name, FFLogoSize size)
{
    if (name->length == 0 || !isalpha((unsigned char)name->chars[0])) {
        return nullptr;
    }
    const FFlogo* logos = ffLogoBuiltins[toupper((unsigned char)name->chars[0]) - 'A'];
    for (const FFlogo* logo = logos; logo && logo->names[0]; ++logo) {
        if ((size == FF_LOGO_SIZE_NORMAL && logo->type != FF_LOGO_LINE_TYPE_NORMAL) ||
            (size == FF_LOGO_SIZE_SMALL && logo->type != FF_LOGO_LINE_TYPE_SMALL_BIT)) {
            continue;
        }
        if (logo_has_name(logo, name, size == FF_LOGO_SIZE_SMALL)) {
            return logo;
        }
    }
    return nullptr;
}

static const FFlogo* selected_builtin_logo(FFLogoSize size)
{
    if (instance.config.logo.source.length == 0) {
        return nullptr;
    }
    return get_builtin_logo(&instance.config.logo.source, size);
}

bool ffLeonOSLogoPrint(void)
{
    FFOptionsLogo* options = &instance.config.logo;
    clear_logo_cache();
    instance.state.logoHeight = 0;
    instance.state.logoWidth = 0;
    instance.state.keysHeight = 0;

    if (options->type == FF_LOGO_TYPE_NONE) {
        apply_leonos_colors();
        return true;
    }

    FFLogoSize size = options->type == FF_LOGO_TYPE_SMALL ? FF_LOGO_SIZE_SMALL : FF_LOGO_SIZE_NORMAL;
    const FFlogo* logo = selected_builtin_logo(size);
    if (options->source.length > 0 && !logo) {
        fprintf(stderr, "fastfetch: unknown built-in logo: %s\n", options->source.chars);
        apply_leonos_colors();
        return false;
    }
    if (logo) {
        apply_logo_colors(logo, true);
        build_logo_cache(logo->lines, true);
    } else {
        apply_leonos_colors();
        build_logo_cache(leonos_logo, true);
    }

    if (options->position == FF_LOGO_POSITION_TOP) {
        FF_LIST_FOR_EACH (FFLeonOSLogoLine, line, instance.state.logoLineCache.lines) {
            ffStrbufPutTo(&line->chars, stdout);
        }
        ffPrintCharTimes('\n', options->paddingBottom);
        clear_logo_cache();
        instance.state.logoHeight = 0;
        instance.state.logoWidth = 0;
    }
    return true;
}

void ffLogoPrintDetected([[maybe_unused]] FFLogoSize size)
{
    (void)ffLeonOSLogoPrint();
}

void ffLogoPrint(void)
{
    (void)ffLeonOSLogoPrint();
}

void ffLogoPrintLine(void)
{
    FFLogoLineCacheState* cache = &instance.state.logoLineCache;
    if (cache->nextLine < cache->lines.length) {
        FFLeonOSLogoLine* line = FF_LIST_GET(FFLeonOSLogoLine, cache->lines, cache->nextLine++);
        if (instance.config.logo.position == FF_LOGO_POSITION_RIGHT) {
            printf("\033[9999999C\033[%uD", cache->rightOffset);
            ffStrbufWriteTo(&line->chars, stdout);
            fputs("\033[G", stdout);
        } else {
            ffStrbufWriteTo(&line->chars, stdout);
            uint32_t remaining = instance.state.logoWidth;
            if (line->width < remaining) {
                remaining -= line->width;
            } else {
                remaining = 0;
            }
            ffPrintCharTimes(' ', remaining);
        }
    } else if (instance.config.logo.position == FF_LOGO_POSITION_LEFT) {
        ffPrintCharTimes(' ', instance.state.logoWidth);
    }
    ++instance.state.keysHeight;
}

void ffLogoPrintRemaining(void)
{
    FFLogoLineCacheState* cache = &instance.state.logoLineCache;
    if (cache->lines.length > 0 &&
        (instance.config.logo.position == FF_LOGO_POSITION_LEFT || instance.config.logo.position == FF_LOGO_POSITION_RIGHT)) {
        while (cache->nextLine < cache->lines.length) {
            FFLeonOSLogoLine* line = FF_LIST_GET(FFLeonOSLogoLine, cache->lines, cache->nextLine++);
            if (instance.config.logo.position == FF_LOGO_POSITION_RIGHT) {
                printf("\033[9999999C\033[%uD", cache->rightOffset);
            }
            ffStrbufPutTo(&line->chars, stdout);
        }
        if (!instance.config.display.pipe) {
            fputs(FASTFETCH_TEXT_MODIFIER_RESET, stdout);
        }
        instance.state.keysHeight = instance.state.logoHeight + 1;
        clear_logo_cache();
        return;
    }

    if (instance.state.keysHeight <= instance.state.logoHeight) {
        ffPrintCharTimes('\n', instance.state.logoHeight - instance.state.keysHeight + 1);
    }
    instance.state.keysHeight = instance.state.logoHeight + 1;
}

void ffLeonOSLogoList(void)
{
    uint32_t counter = 0;
    for (uint32_t letter = 0; letter < 26; ++letter) {
        for (const FFlogo* logo = ffLogoBuiltins[letter]; logo && logo->names[0]; ++logo) {
            printf("%u)", ++counter);
            for (const char* const* name = logo->names; *name; ++name) {
                printf(" %s", *name);
            }
            putchar('\n');
        }
    }
}

void ffLeonOSLogoPrintAll(void)
{
    FFOptionsLogo* options = &instance.config.logo;
    options->position = FF_LOGO_POSITION_TOP;
    options->paddingBottom = 1;

    for (uint32_t letter = 0; letter < 26; ++letter) {
        for (const FFlogo* logo = ffLogoBuiltins[letter]; logo && logo->names[0]; ++logo) {
            printf("%s:\n", logo->names[0]);
            for (uint32_t color = 0; color < FASTFETCH_LOGO_MAX_COLORS; ++color) {
                ffStrbufClear(&options->colors[color]);
            }
            apply_logo_colors(logo, true);
            build_logo_cache(logo->lines, true);
            FF_LIST_FOR_EACH (FFLeonOSLogoLine, line, instance.state.logoLineCache.lines) {
                ffStrbufPutTo(&line->chars, stdout);
            }
            putchar('\n');
        }
    }
    clear_logo_cache();
}

void ffLogoBuiltinPrint(void)
{
    ffLeonOSLogoPrintAll();
}

void ffLeonOSLogoDestroy(void)
{
    clear_logo_cache();
}

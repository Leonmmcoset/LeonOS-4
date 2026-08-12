#include "leonos_fastfetch.h"

#include "common/impl/FFPlatform_private.h"
#include "common/option.h"
#include "common/printing.h"
#include "detection/version/version.h"
#include "detection/libc/libc.h"

#include <leonos/system.h>
#include <time.h>
#include <unistd.h>

FFinstance instance;

FFVersionResult ffVersionResult = {
    .projectName = FASTFETCH_PROJECT_NAME,
    .sysName = "LeonOS",
    .architecture = "x86_64",
    .version = FASTFETCH_PROJECT_VERSION,
    .versionTweak = FASTFETCH_PROJECT_VERSION_TWEAK,
    .versionGit = FASTFETCH_PROJECT_VERSION_GIT,
    .cmakeBuiltType = FASTFETCH_PROJECT_CMAKE_BUILD_TYPE,
    .compileTime = __DATE__ ", " __TIME__,
    .compiler = "clang",
    .debugMode = false,
};

const char* ffDetectLibc(FFLibcResult* result)
{
    if (result) {
        result->name = "Picolibc";
        result->version = nullptr;
    }
    return nullptr;
}

static void init_display(void)
{
    FFOptionsDisplay* display = &instance.config.display;

    // Keep these empty until the active logo supplies its default palette.
    // Command-line color options are applied before that default is chosen.
    ffStrbufInit(&display->colorKeys);
    ffStrbufInit(&display->colorTitle);
    ffStrbufInit(&display->colorOutput);
    ffStrbufInit(&display->colorSeparator);
    display->brightColor = true;
    ffStrbufInitStatic(&display->keyValueSeparator, ": ");
    display->stat = -1;
    display->pipe = !isatty(1);
    display->showErrors = true;
    display->disableLinewrap = false;
    display->durationAbbreviation = false;
    display->durationSpaceBeforeUnit = FF_SPACE_BEFORE_UNIT_DEFAULT;
    display->hideCursor = false;
    display->sizeBinaryPrefix = FF_SIZE_BINARY_PREFIX_TYPE_IEC;
    display->sizeNdigits = 2;
    display->sizeMaxPrefix = 8;
    display->sizeSpaceBeforeUnit = FF_SPACE_BEFORE_UNIT_DEFAULT;
    display->tempUnit = FF_TEMPERATURE_UNIT_DEFAULT;
    display->tempNdigits = 1;
    ffStrbufInitStatic(&display->tempColorGreen, "32");
    ffStrbufInitStatic(&display->tempColorYellow, "93");
    ffStrbufInitStatic(&display->tempColorRed, "91");
    display->tempSpaceBeforeUnit = FF_SPACE_BEFORE_UNIT_DEFAULT;
    ffStrbufInitStatic(&display->barCharElapsed, "#");
    ffStrbufInitStatic(&display->barCharTotal, "-");
    ffStrbufInitStatic(&display->barBorderLeft, "[ ");
    ffStrbufInitStatic(&display->barBorderRight, " ]");
    ffStrbufInit(&display->barBorderLeftElapsed);
    ffStrbufInit(&display->barBorderRightElapsed);
    ffStrbufInitStatic(&display->barColorElapsed, "auto");
    ffStrbufInitStatic(&display->barColorTotal, "97");
    ffStrbufInitStatic(&display->barColorBorder, "97");
    display->barWidth = 10;
    display->percentType = FF_PERCENTAGE_TYPE_NUM_BIT;
    display->percentNdigits = 0;
    ffStrbufInitStatic(&display->percentColorGreen, "32");
    ffStrbufInitStatic(&display->percentColorYellow, "93");
    ffStrbufInitStatic(&display->percentColorRed, "91");
    display->percentSpaceBeforeUnit = FF_SPACE_BEFORE_UNIT_DEFAULT;
    display->percentWidth = 0;
    display->noBuffer = false;
    display->keyType = FF_MODULE_KEY_TYPE_STRING;
    display->keyWidth = 0;
    display->keyPaddingLeft = 0;
    display->freqNdigits = 2;
    display->freqSpaceBeforeUnit = FF_SPACE_BEFORE_UNIT_DEFAULT;
    display->fractionNdigits = 2;
    display->fractionTrailingZeros = FF_FRACTION_TRAILING_ZEROS_TYPE_DEFAULT;
    ffListInit(&display->constants);
}

static void destroy_display(void)
{
    FFOptionsDisplay* display = &instance.config.display;
    FFstrbuf* buffers[] = {
        &display->colorKeys, &display->colorTitle, &display->colorOutput,
        &display->colorSeparator, &display->keyValueSeparator,
        &display->tempColorGreen, &display->tempColorYellow, &display->tempColorRed,
        &display->barCharElapsed, &display->barCharTotal, &display->barBorderLeft,
        &display->barBorderRight, &display->barBorderLeftElapsed,
        &display->barBorderRightElapsed, &display->barColorElapsed,
        &display->barColorTotal, &display->barColorBorder,
        &display->percentColorGreen, &display->percentColorYellow,
        &display->percentColorRed,
    };

    for (uint32_t index = 0; index < sizeof(buffers) / sizeof(buffers[0]); ++index) {
        ffStrbufDestroy(buffers[index]);
    }
    ffListDestroy(&display->constants);
}

void ffLeonOSInit(void)
{
    ffStrbufInit(&instance.config.logo.source);
    for (uint32_t index = 0; index < FASTFETCH_LOGO_MAX_COLORS; ++index) {
        ffStrbufInit(&instance.config.logo.colors[index]);
    }
    instance.config.logo.type = FF_LOGO_TYPE_AUTO;
    instance.config.logo.position = FF_LOGO_POSITION_LEFT;
    instance.config.logo.paddingTop = 0;
    instance.config.logo.paddingBottom = 0;
    instance.config.logo.paddingLeft = 0;
    instance.config.logo.paddingRight = 3;
    instance.config.logo.width = 0;
    instance.config.logo.height = 0;
    instance.config.logo.printRemaining = true;
    instance.config.logo.preserveAspectRatio = false;
    instance.config.logo.recache = false;
    instance.config.general.multithreading = false;
    instance.config.general.processingTimeout = 0;
    instance.config.general.detectVersion = false;
    ffStrbufInit(&instance.config.general.playerName);
    init_display();
    ffPlatformInit(&instance.state.platform);
    ffListInit(&instance.state.logoLineCache.lines);
    instance.state.logoLineCache.nextLine = 0;
    instance.state.logoLineCache.rightOffset = 0;
    instance.state.logoWidth = 0;
    instance.state.logoHeight = 0;
    instance.state.keysHeight = 0;
    instance.state.titleFqdn = false;
}

void ffLeonOSDestroy(void)
{
    ffLeonOSLogoDestroy();
    ffPlatformDestroy(&instance.state.platform);
    ffStrbufDestroy(&instance.config.logo.source);
    for (uint32_t index = 0; index < FASTFETCH_LOGO_MAX_COLORS; ++index) {
        ffStrbufDestroy(&instance.config.logo.colors[index]);
    }
    ffStrbufDestroy(&instance.config.general.playerName);
    destroy_display();
}

void ffLeonOSPrintStatic(const char* key, const char* value)
{
    ffPrintLogoAndKey(key, 0, nullptr, FF_PRINT_TYPE_DEFAULT);
    fputs(value, stdout);
    putchar('\n');
}

void ffLeonOSPrintCPU(void)
{
    ffLeonOSPrintStatic("CPU", ffLeonOSCPUName());
}

int clock_gettime([[maybe_unused]] clockid_t clock_id, struct timespec* time)
{
    struct leonos_time_info info = {0};

    if (!time || leonos_time_info(&info) < 0) {
        return -1;
    }
    time->tv_sec = (time_t)info.unix_seconds;
    time->tv_nsec = 0;
    return 0;
}

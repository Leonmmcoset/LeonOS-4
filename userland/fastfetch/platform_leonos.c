#include "common/impl/FFPlatform_private.h"

#include <leonos/auth.h>
#include <leonos/system.h>

void ffPlatformInitImpl(FFPlatform* platform)
{
    struct leonos_system_info system_info = {0};
    struct leonos_user_info user = {0};

    platform->pid = 0;
    platform->uid = 0;
    ffStrbufSetS(&platform->cwd, "0:/");
    ffStrbufSetS(&platform->cacheDir, "0:/system/cache/");
    ffStrbufSetS(&platform->userShell, "LeonOS shell");
    ffStrbufSetS(&platform->hostName, "LeonOS");

    if (leonos_auth_current(&user) == 0 && user.uid != 0) {
        platform->uid = user.uid;
        ffStrbufSetS(&platform->userName, user.username);
        ffStrbufSetS(&platform->fullUserName, user.username);
        ffStrbufSetS(&platform->homeDir, user.home);
    } else {
        ffStrbufSetS(&platform->userName, "user");
        ffStrbufSetS(&platform->fullUserName, "user");
        ffStrbufSetS(&platform->homeDir, "0:/users/");
    }
    ffStrbufEnsureEndsWithC(&platform->homeDir, '/');

    if (leonos_system_info(&system_info) == 0) {
        ffStrbufSetS(&platform->sysinfo.name,
                      system_info.kernel_name[0] ? system_info.kernel_name : "LeonOS Kernel");
        ffStrbufSetS(&platform->sysinfo.release,
                      system_info.kernel_version[0] ? system_info.kernel_version : "unknown");
        ffStrbufSetS(&platform->sysinfo.version,
                      system_info.build_time[0] ? system_info.build_time : "LeonOS");
    } else {
        ffStrbufSetS(&platform->sysinfo.name, "LeonOS Kernel");
        ffStrbufSetS(&platform->sysinfo.release, "unknown");
        ffStrbufSetS(&platform->sysinfo.version, "LeonOS");
    }
    ffStrbufSetS(&platform->sysinfo.architecture, "x86_64");
    platform->sysinfo.pageSize = 4096;
}

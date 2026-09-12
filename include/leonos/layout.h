#ifndef LEONOS_LAYOUT_H
#define LEONOS_LAYOUT_H

#include <leonos/rootfs.h>

/*
 * LeonOS 4 guest root filesystem layout contract.
 *
 * The installed root follows the Alpine Linux FHS shape: /bin, /sbin, /lib,
 * /usr/bin, /usr/sbin and /usr/lib are real directories.  There is no
 * usr-merge and no /lib64. Standard runtime links include:
 *
 *     /var/run -> ../run
 *     /var/lock -> ../run/lock
 *
 * The musl ELF PT_INTERP contract is unchanged: executables request
 * "/lib/ld-musl-x86_64.so.1", and the real interpreter is stored at that
 * path.  Do not point ELF PT_INTERP at a glibc loader.
 *
 * LeonOS-owned files live below explicitly named subdirectories:
 *
 *     /etc/leonos                 persistent configuration
 *     /var/lib/leonos             persistent mutable state
 *     /var/cache/leonos           cache data
 *     /run/leonos                 volatile per-boot IPC and session state
 *     /usr/lib/leonos             private libraries and loader payload
 *     /usr/lib/leonos/apps        application packages (manifest + ELF)
 *     /usr/lib/leonos/drivers     Ring-0 driver modules
 *     /usr/lib/leonos/tests       diagnostic guest probes
 *     /usr/share/leonos           desktop resources
 *     /usr/share/fonts/leonos     LeonOS UI fonts
 *     /usr/share/doc/leonos       bundled help and vendor notices
 *
 * tools/leonos_layout.py is the build-side mirror of this header.  Keep the
 * two files synchronized and do not add competing path literal tables.
 */

/* Directories (no trailing slash so they concatenate with "/name"). */
#define LEONOS_LAYOUT_BIN "/bin"
#define LEONOS_LAYOUT_SBIN "/sbin"
#define LEONOS_LAYOUT_LIB "/lib"
#define LEONOS_LAYOUT_BOOT "/boot"
#define LEONOS_LAYOUT_SRV "/srv"
#define LEONOS_LAYOUT_USR_BIN "/usr/bin"
#define LEONOS_LAYOUT_USR_SBIN "/usr/sbin"
#define LEONOS_LAYOUT_USR_LIB "/usr/lib"
#define LEONOS_LAYOUT_USR_SHARE "/usr/share"
#define LEONOS_LAYOUT_ETC_LEONOS "/etc/leonos"
#define LEONOS_LAYOUT_ETC_SSL_CERTS "/etc/ssl/certs"
#define LEONOS_LAYOUT_VAR_LIB_LEONOS "/var/lib/leonos"
#define LEONOS_LAYOUT_VAR_CACHE_LEONOS "/var/cache/leonos"
#define LEONOS_LAYOUT_VAR_LOG "/var/log"
#define LEONOS_LAYOUT_VAR_TMP "/var/tmp"
#define LEONOS_LAYOUT_RUN_LEONOS "/run/leonos"
#define LEONOS_LAYOUT_LEONOS_LIB "/usr/lib/leonos"
#define LEONOS_LAYOUT_LEONOS_APPS "/usr/lib/leonos/apps"
#define LEONOS_LAYOUT_LEONOS_DRIVERS "/usr/lib/leonos/drivers"
#define LEONOS_LAYOUT_LEONOS_TESTS "/usr/lib/leonos/tests"
#define LEONOS_LAYOUT_LEONOS_SHARE "/usr/share/leonos"
#define LEONOS_LAYOUT_LEONOS_RESOURCES "/usr/share/leonos/resources"
#define LEONOS_LAYOUT_LEONOS_FONTS "/usr/share/fonts/leonos"
#define LEONOS_LAYOUT_LEONOS_DOC "/usr/share/doc/leonos"
#define LEONOS_LAYOUT_LICENSES "/usr/share/licenses"
#define LEONOS_LAYOUT_MISC "/usr/share/misc"
#define LEONOS_LAYOUT_TERMINFO "/usr/share/terminfo"
#define LEONOS_LAYOUT_LOCALE "/usr/share/locale"

/* Third-party suites retained under /opt with /usr/bin command entries. */
#define LEONOS_LAYOUT_OPT_CMD "/opt/cmd"
#define LEONOS_LAYOUT_OPT_DYNE "/opt/dyne"
#define LEONOS_LAYOUT_OPT_LUA "/opt/lua"
#define LEONOS_LAYOUT_OPT_PYTHON "/opt/python"
#define LEONOS_LAYOUT_OPT_TCC "/opt/tcc"

/* Runtime configuration. */
#define LEONOS_PATH_LEONOS_CONF LEONOS_LAYOUT_ETC_LEONOS "/leonos.conf"
#define LEONOS_PATH_DISPLAY_CONF LEONOS_LAYOUT_ETC_LEONOS "/display.conf"
#define LEONOS_PATH_DRIVERS_CONF LEONOS_LAYOUT_ETC_LEONOS "/drivers.conf"
#define LEONOS_PATH_SERVICES_CFG LEONOS_LAYOUT_ETC_LEONOS "/services.cfg"
#define LEONOS_PATH_NETWORK_CONF LEONOS_LAYOUT_ETC_LEONOS "/network.conf"
#define LEONOS_PATH_NETWORK_BAK LEONOS_LAYOUT_ETC_LEONOS "/network.conf.bak"
#define LEONOS_PATH_NETWORK_TMP LEONOS_LAYOUT_ETC_LEONOS "/network.conf.tmp"
#define LEONOS_PATH_LOCALE_CONF LEONOS_LAYOUT_ETC_LEONOS "/locale.conf"
#define LEONOS_PATH_ENVIRONMENT_CONF LEONOS_LAYOUT_ETC_LEONOS "/environment.conf"
#define LEONOS_PATH_FILEASSOC_CFG LEONOS_LAYOUT_ETC_LEONOS "/fileassoc.cfg"
#define LEONOS_PATH_DESKTOP_ENTRIES LEONOS_LAYOUT_ETC_LEONOS "/desktop-entries.conf"
#define LEONOS_PATH_LESSKEY LEONOS_LAYOUT_ETC_LEONOS "/lesskey"

/* Persistent and volatile state. */
#define LEONOS_PATH_USERS_DB LEONOS_LAYOUT_VAR_LIB_LEONOS "/users.db"
#define LEONOS_PATH_ACCOUNTS_DB LEONOS_LAYOUT_VAR_LIB_LEONOS "/accounts.db"
#define LEONOS_PATH_STARTUP_DB LEONOS_LAYOUT_VAR_LIB_LEONOS "/startup.db"
#define LEONOS_PATH_STARTUP_DENIALS_DB LEONOS_LAYOUT_VAR_LIB_LEONOS "/startup-denials.db"
#define LEONOS_PATH_OOBE_DONE LEONOS_LAYOUT_VAR_LIB_LEONOS "/oobe.done"
#define LEONOS_PATH_LICENSE LEONOS_LAYOUT_VAR_LIB_LEONOS "/license.dat"
#define LEONOS_PATH_KERNELDEBUG_ENABLED LEONOS_LAYOUT_VAR_LIB_LEONOS "/kerneldebug.enabled"
#define LEONOS_PATH_KERNELDEBUG_CONTROL LEONOS_LAYOUT_VAR_LIB_LEONOS "/kernel-debug"
#define LEONOS_PATH_SESSION_USER LEONOS_LAYOUT_RUN_LEONOS "/session-user"
#define LEONOS_PATH_SERVICES_STATE LEONOS_LAYOUT_RUN_LEONOS "/services.state"
#define LEONOS_PATH_SERVICES_CMD LEONOS_LAYOUT_RUN_LEONOS "/services.cmd"

/* Shared resources. */
#define LEONOS_PATH_CACERT LEONOS_LAYOUT_ETC_SSL_CERTS "/ca-certificates.crt"
#define LEONOS_PATH_MAGIC LEONOS_LAYOUT_MISC "/magic.mgc"
#define LEONOS_PATH_SYSTEM_FONT LEONOS_LAYOUT_LEONOS_FONTS "/system.psf"
#define LEONOS_PATH_UI_METRO_FONT LEONOS_LAYOUT_LEONOS_FONTS "/leonos-metro.ttf"
#define LEONOS_PATH_UI_WIN95_FONT LEONOS_LAYOUT_LEONOS_FONTS "/leonos-win95.ttf"
#define LEONOS_PATH_BROWSER_FONT LEONOS_LAYOUT_LEONOS_FONTS "/times-new-roman.ttf"
#define LEONOS_PATH_BROWSER_CJK_FONT LEONOS_LAYOUT_LEONOS_FONTS "/simsun.ttc"
#define LEONOS_PATH_MOUSE_BMP LEONOS_LAYOUT_LEONOS_RESOURCES "/mouse.bmp"
#define LEONOS_PATH_WALLPAPER_BMP LEONOS_LAYOUT_LEONOS_RESOURCES "/wallpaper-metro.bmp"
#define LEONOS_PATH_LOGO_PNG LEONOS_LAYOUT_LEONOS_RESOURCES "/logo.png"
#define LEONOS_PATH_WINDOW_MINIMIZE_BMP LEONOS_LAYOUT_LEONOS_RESOURCES "/window-button-minimize.bmp"
#define LEONOS_PATH_WINDOW_MAXIMIZE_BMP LEONOS_LAYOUT_LEONOS_RESOURCES "/window-button-maximize.bmp"
#define LEONOS_PATH_WINDOW_RESTORE_BMP LEONOS_LAYOUT_LEONOS_RESOURCES "/window-button-restore.bmp"
#define LEONOS_PATH_WINDOW_CLOSE_BMP LEONOS_LAYOUT_LEONOS_RESOURCES "/window-button-close.bmp"
#define LEONOS_PATH_MINESWEEPER_MINE_BMP LEONOS_LAYOUT_LEONOS_RESOURCES "/minesweeper-mine.bmp"
#define LEONOS_PATH_MINESWEEPER_FLAG_BMP LEONOS_LAYOUT_LEONOS_RESOURCES "/minesweeper-flag.bmp"
#define LEONOS_PATH_HELP LEONOS_LAYOUT_LEONOS_DOC "/leonos.hlp"

/* Shared libraries and interpreter payload. */
#define LEONOS_PATH_MUSL_INTERP "/lib/ld-musl-x86_64.so.1"
#define LEONOS_PATH_LIBC LEONOS_LAYOUT_LIB "/libc.so"
#define LEONOS_PATH_LIBMIMALLOC LEONOS_LAYOUT_LIB "/libmimalloc.so.3"
/* Diagnostic-only probe constant.  LeonOS does not ship or claim a glibc
 * loader; the musl interpreter above is the only supported PT_INTERP. */
#define LEONOS_PATH_GLIBC_INTERP "/lib64/ld-linux-x86-64.so.2"
#define LEONOS_PATH_LIBLEONOS LEONOS_LAYOUT_LEONOS_LIB "/libleonos.so.2"
#define LEONOS_PATH_LIBLEONOS_COMPAT LEONOS_LAYOUT_LEONOS_LIB "/libleonos.so.1"
#define LEONOS_PATH_OLD_NATIVE_INTERP LEONOS_LAYOUT_LEONOS_LIB "/ld-leonos.elf"
#define LEONOS_PATH_KERNELDEBUG_MODULE LEONOS_LAYOUT_LEONOS_LIB "/kerneldebug.sys"
#define LEONOS_PATH_OSMLAYER_MANIFEST LEONOS_LAYOUT_LEONOS_LIB "/osmlayer.manifest"

/* Runtime view of the boot partition; the ESP itself normally mounts /boot. */
#define LEONOS_PATH_BOOT_KERNEL "/boot/leonos/kernel.sys"
#define LEONOS_PATH_BOOT_MIDDLELAYER "/boot/leonos/middlelayer.sys"
#define LEONOS_PATH_BOOT_KERNELDEBUG_MARKER "/boot/leonos/state/kerneldebug.next"
#define LEONOS_PATH_BOOT_DISPLAY_CONF "/boot/leonos/config/display.conf"

#endif /* LEONOS_LAYOUT_H */

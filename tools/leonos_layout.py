#!/usr/bin/env python3
"""Single source of truth for the LeonOS-4 guest rootfs layout.

The installed root follows the Alpine Linux FHS shape: /bin, /sbin, /lib,
/usr/bin, /usr/sbin and /usr/lib are real directories.  There is no usr-merge
and no /lib64; the musl ELF PT_INTERP path stays /lib/ld-musl-x86_64.so.1.
Standard runtime links include:

    /var/run -> ../run
    /var/lock -> ../run/lock

LeonOS-owned state and resources get explicitly named subdirectories so they
do not collide with third-party Linux software:

    /etc/leonos                 persistent configuration
    /var/lib/leonos             persistent mutable state
    /var/cache/leonos           cache data
    /run/leonos                 volatile per-boot IPC and session state
    /usr/lib/leonos             private libraries, drivers and loader payload
    /usr/lib/leonos/apps        application packages (manifest + executable)
    /usr/lib/leonos/drivers     LeonOS Ring-0 driver modules
    /usr/lib/leonos/tests       diagnostic guest probes
    /usr/share/leonos           desktop resources that are not icon-theme data
    /usr/share/fonts/leonos     LeonOS UI fonts
    /usr/share/doc/leonos       bundled help and vendor notices

The C header include/leonos/layout.h mirrors these names.  Keep the two in
sync; consumers should use the header macros or this module instead of adding
new path literals.
"""
from __future__ import annotations

import os
import re
from pathlib import Path, PurePosixPath

# ---------------------------------------------------------------------------
# Guest-relative directory contract (no leading slash).
# ---------------------------------------------------------------------------
BIN = "bin"
SBIN = "sbin"
LIB = "lib"
BOOT = "boot"
HOME = "home"
SRV = "srv"
USR = "usr"
USR_BIN = "usr/bin"
USR_LIB = "usr/lib"
USR_SBIN = "usr/sbin"
USR_SHARE = "usr/share"

ETC = "etc"
ETC_LEONOS = "etc/leonos"
ETC_SSL_CERTS = "etc/ssl/certs"

VAR = "var"
VAR_LIB = "var/lib"
VAR_LIB_LEONOS = "var/lib/leonos"
VAR_CACHE_LEONOS = "var/cache/leonos"
VAR_LOG = "var/log"
VAR_TMP = "var/tmp"

RUN_LEONOS = "run/leonos"

LEONOS_LIB = "usr/lib/leonos"
LEONOS_APPS = "usr/lib/leonos/apps"
LEONOS_DRIVERS = "usr/lib/leonos/drivers"
LEONOS_TESTS = "usr/lib/leonos/tests"
LEONOS_SHARE = "usr/share/leonos"
LEONOS_RESOURCES = "usr/share/leonos/resources"
LEONOS_FONTS = "usr/share/fonts/leonos"
LEONOS_DOC = "usr/share/doc/leonos"
LICENSES = "usr/share/licenses"
MISC = "usr/share/misc"
TERMINFO = "usr/share/terminfo"
LOCALE = "usr/share/locale"
EXAMPLES = "usr/share/examples"

OPT = "opt"
OPT_CMD = "opt/cmd"
OPT_DYNE = "opt/dyne"
OPT_LUA = "opt/lua"
OPT_PYTHON = "opt/python"
OPT_TCC = "opt/tcc"

# One directory/mode/link table for all images, the installer and early boot.
ROOTFS_CONTRACT = Path(__file__).resolve().parents[1] / "include/uapi/leonos/rootfs.h"
_contract = ROOTFS_CONTRACT.read_text(encoding="ascii")
ROOT_DIRECTORIES = {path.lstrip("/"): int(mode, 8) for path, mode in
                    re.findall(r'X\("(/[^"\n]+)", (0[0-7]+)\)', _contract)}
ROOT_SYMLINKS = dict((path.lstrip("/"), target) for path, target in
                    re.findall(r'X\("(/[^"\n]+)", "([^"\n]+)"\)', _contract))
if not ROOT_DIRECTORIES or not ROOT_SYMLINKS:
    raise ValueError("invalid rootfs contract: missing directory or link table")


def layout_directories(root: Path) -> None:
    """Create only real directories; do not traverse staging symlinks."""
    if root.is_symlink():
        raise ValueError(f"rootfs root is a symlink: {root}")
    root.mkdir(parents=True, exist_ok=True)
    root.chmod(0o755)
    for directory, mode in ROOT_DIRECTORIES.items():
        path = root
        for component in PurePosixPath(directory).parts:
            path /= component
            if path.is_symlink():
                raise ValueError(f"rootfs directory is a symlink: {path}")
            path.mkdir(exist_ok=True)
        path.chmod(mode)


def apply_root_symlinks(root: Path) -> None:
    """Publish the standard links, refusing conflicting files/directories."""
    for link, target in ROOT_SYMLINKS.items():
        path = root / link
        if path.is_symlink() and os.readlink(path) == target:
            continue
        if path.exists() or path.is_symlink():
            raise ValueError(f"conflicting rootfs link: {path}")
        path.symlink_to(target)


# Absolute runtime paths used by C code and generated configuration.
P_BIN = "/bin"
P_SBIN = "/sbin"
P_LIB = "/lib"
P_USR_BIN = "/usr/bin"
P_USR_LIB = "/usr/lib"
P_ETC = "/etc"
P_ETC_LEONOS = "/etc/leonos"
P_ETC_SSL_CERTS = "/etc/ssl/certs"
P_VAR_LIB_LEONOS = "/var/lib/leonos"
P_VAR_CACHE_LEONOS = "/var/cache/leonos"
P_RUN_LEONOS = "/run/leonos"
P_LEONOS_LIB = "/usr/lib/leonos"
P_LEONOS_APPS = "/usr/lib/leonos/apps"
P_LEONOS_DRIVERS = "/usr/lib/leonos/drivers"
P_LEONOS_TESTS = "/usr/lib/leonos/tests"
P_LEONOS_SHARE = "/usr/share/leonos"
P_LEONOS_RESOURCES = "/usr/share/leonos/resources"
P_LEONOS_FONTS = "/usr/share/fonts/leonos"
P_LEONOS_DOC = "/usr/share/doc/leonos"
P_LICENSES = "/usr/share/licenses"
P_MISC = "/usr/share/misc"
P_TERMINFO = "/usr/share/terminfo"
P_LOCALE = "/usr/share/locale"

P_MUSL_INTERP = "/lib/ld-musl-x86_64.so.1"
P_CACERT = "/etc/ssl/certs/ca-certificates.crt"
P_LIBLEONOS = "/usr/lib/leonos/libleonos.so.2"

# Configuration files.
P_LEONOS_CONF = P_ETC_LEONOS + "/leonos.conf"
P_DISPLAY_CONF = P_ETC_LEONOS + "/display.conf"
P_DRIVERS_CONF = P_ETC_LEONOS + "/drivers.conf"
P_SERVICES_CFG = P_ETC_LEONOS + "/services.cfg"
P_NETWORK_CONF = P_ETC_LEONOS + "/network.conf"
P_NETWORK_BAK = P_ETC_LEONOS + "/network.conf.bak"
P_NETWORK_TMP = P_ETC_LEONOS + "/network.conf.tmp"
P_LOCALE_CONF = P_ETC_LEONOS + "/locale.conf"
P_ENVIRONMENT_CONF = P_ETC_LEONOS + "/environment.conf"
P_FILEASSOC_CFG = P_ETC_LEONOS + "/fileassoc.cfg"
P_DESKTOP_ENTRIES_CONF = P_ETC_LEONOS + "/desktop-entries.conf"
P_LESSKEY = P_ETC_LEONOS + "/lesskey"

# Persistent state and volatile runtime state.
P_USERS_DB = P_VAR_LIB_LEONOS + "/users.db"
P_ACCOUNTS_DB = P_VAR_LIB_LEONOS + "/accounts.db"
P_STARTUP_DB = P_VAR_LIB_LEONOS + "/startup.db"
P_STARTUP_DENIALS_DB = P_VAR_LIB_LEONOS + "/startup-denials.db"
P_OOBE_DONE = P_VAR_LIB_LEONOS + "/oobe.done"
P_LICENSE = P_VAR_LIB_LEONOS + "/license.dat"
P_KERNELDEBUG_ENABLED = P_VAR_LIB_LEONOS + "/kerneldebug.enabled"
P_KERNELDEBUG_CONTROL = P_VAR_LIB_LEONOS + "/kernel-debug"
P_SESSION_USER = P_RUN_LEONOS + "/session-user"
P_SERVICES_STATE = P_RUN_LEONOS + "/services.state"
P_SERVICES_CMD = P_RUN_LEONOS + "/services.cmd"

# Runtime boot payload paths (the ESP is normally mounted at /boot).
P_BOOT_KERNEL = "/boot/leonos/kernel.sys"
P_BOOT_MIDDLELAYER = "/boot/leonos/middlelayer.sys"
P_BOOT_KERNELDEBUG_MARKER = "/boot/leonos/state/kerneldebug.next"
P_BOOT_DISPLAY_CONF = "/boot/leonos/config/display.conf"
P_KERNELDEBUG_MODULE = P_LEONOS_LIB + "/kerneldebug.sys"
P_OSMLAYER_MANIFEST = P_LEONOS_LIB + "/osmlayer.manifest"

# ESP-internal (EFI FAT) paths.  GRUB and loader.elf use these before any
# root filesystem exists.  The loader is at the ESP root so GRUB's
# ``search --file /loader.elf`` convention is preserved.
ESP_LOADER = "/loader.elf"
ESP_KERNEL = "/leonos/kernel.sys"
ESP_MIDDLELAYER = "/leonos/middlelayer.sys"
ESP_KERNELDEBUG_MARKER = "/leonos/state/kerneldebug.next"
ESP_DISPLAY_CONF = "/leonos/config/display.conf"

# Python and GCC keep a relocatable launcher in /usr/bin; the real trees live
# under /opt.  Python exposes python/python3/python3.14 as symlinks to the one
# launcher so there is still a single executable image.
PYTHON_LAUNCHER = "python3.14"
PYTHON_ALIASES = ("python", "python3")
GCC_LAUNCHER = "leonos-musl-cc"
# Symbolic command aliases for the GCC suite; the launcher derives the real
# compiler name from argv[0].
GCC_ALIASES = (
    "addr2line", "ar", "as", "c++", "c++filt", "cc", "cpp", "elfedit", "g++",
    "gcc", "gcc-15.1.0", "gcc-ar", "gcc-nm", "gcc-ranlib", "gcov", "gcov-dump",
    "gcov-tool", "gprof", "ld", "ld.bfd", "nm", "objcopy", "objdump", "ranlib",
    "readelf", "size", "strings", "strip", "musl-gcc", "musl-g++",
)


def app_package_dir(app: str) -> PurePosixPath:
    """Return the guest-relative package directory for an application id."""
    return PurePosixPath(LEONOS_APPS) / app


def app_exec_path(app: str, extension: str = "elf") -> PurePosixPath:
    """Return the guest-relative executable path for an application id."""
    return app_package_dir(app) / f"{app}.{extension}"


def app_package_dir_abs(app: str) -> str:
    return str(PurePosixPath("/") / app_package_dir(app))


def app_exec_path_abs(app: str, extension: str = "elf") -> str:
    return str(PurePosixPath("/") / app_exec_path(app, extension))


def relative_symlink_target(link: str, target: str) -> str:
    """Return a relative symlink target for guest-relative ``link``.

    ``target`` is the guest-relative path of the link destination.  The result
    is valid when both paths are interpreted from the guest root.
    """
    link_path = PurePosixPath(link)
    target_path = PurePosixPath(target)
    # os.path.relpath semantics for pure POSIX paths.
    link_parts = list(link_path.parent.parts)
    target_parts = list(target_path.parts)
    common = 0
    while (common < len(link_parts) and common < len(target_parts)
           and link_parts[common] == target_parts[common]):
        common += 1
    parts = [".."] * (len(link_parts) - common) + target_parts[common:]
    return "/".join(parts) or "."


def command_symlink(command: str, target: str) -> tuple[str, str]:
    """Return ``(guest-relative link, relative target)`` for a /usr/bin entry."""
    link = f"{USR_BIN}/{command}"
    return link, relative_symlink_target(link, target)


def root_symlink_entries() -> list[tuple[str, str]]:
    """Return all rootfs symlinks as ``(guest-relative link, target)``."""
    return sorted(ROOT_SYMLINKS.items())


# ---------------------------------------------------------------------------
# Build-side payload ownership.  These are guest-relative paths whose content
# is generated by a component; the staging prune and layout-link stages use
# them so disabled components and root symlinks cannot leave stale entries.
# ---------------------------------------------------------------------------
NCURSES_COMMANDS = (
    "clear", "infocmp", "infotocap", "captoinfo", "reset", "tabs", "tic",
    "toe", "tput", "tset", "ncursesw6-config",
)

_PAYLOAD_PATHS: dict[str, tuple[str, ...]] = {
    "fastfetch": (f"{USR_BIN}/fastfetch", f"{LICENSES}/fastfetch", "etc/fastfetch"),
    "nano": (f"{USR_BIN}/nano", f"{LICENSES}/nano"),
    "pleditor": (f"{LICENSES}/pleditor",),
    "busybox": (f"{BIN}/busybox", f"{BIN}/sh", f"{LICENSES}/busybox"),
    "cmd": (OPT_CMD, f"{USR_BIN}/cmd", f"{LICENSES}/cmd"),
    "file": (f"{USR_BIN}/file", f"{USR_LIB}/libmagic.so.1",
             f"{MISC}/magic.mgc", f"{LICENSES}/file"),
    "less": (f"{USR_BIN}/less", f"{LICENSES}/less"),
    "lua": (OPT_LUA, f"{USR_BIN}/lua", f"{USR_LIB}/liblua.so.5",
            f"{LICENSES}/lua"),
    "musl-gcc": (OPT_DYNE, *tuple(f"{USR_BIN}/{name}" for name in GCC_ALIASES),
                 f"{LICENSES}/musl-gcc", f"{EXAMPLES}/musl-gcc"),
    "ncurses": (f"{TERMINFO}", *tuple(f"{USR_BIN}/{name}" for name in NCURSES_COMMANDS),
                f"{LICENSES}/ncurses"),
    "python": (OPT_PYTHON,
               *tuple(f"{USR_BIN}/{name}" for name in ("python", "python3", "python3.14")),
               f"{LICENSES}/python", f"{EXAMPLES}/python"),
    "sl": (f"{USR_BIN}/sl", f"{LICENSES}/sl"),
    "tcc": (OPT_TCC, f"{USR_BIN}/tcc", f"{LICENSES}/tcc"),
    "vim": (f"{USR_BIN}/vim", f"{USR_SHARE}/vim", f"{LICENSES}/vim"),
}


def tool_payload_paths(package: str) -> tuple[str, ...]:
    """Return guest-relative staging roots owned by a tool component."""
    paths = _PAYLOAD_PATHS.get(package, ())
    if not isinstance(paths, tuple):
        raise TypeError(f"payload paths must be a tuple: {package}")
    for path in paths:
        value = PurePosixPath(path)
        if not path or value.is_absolute() or ".." in value.parts or value == PurePosixPath("."):
            raise ValueError(f"invalid guest-relative payload path: {package}: {path}")
    return paths


def all_staging_payload_paths() -> tuple[str, ...]:
    """Return every guest-relative path owned by a staged component."""
    seen: set[str] = set()
    ordered: list[str] = []
    for package in _PAYLOAD_PATHS:
        for path in tool_payload_paths(package):
            if path not in seen:
                seen.add(path)
                ordered.append(path)
    return tuple(ordered)


def builtin_command_links(enabled=None) -> list[tuple[str, str]]:
    """Return non-application command symlinks as ``(link, relative target)``.

    ``enabled`` is an optional ``callable(package_id) -> bool`` so disabled
    components do not leave dangling command links behind.
    """
    if enabled is None:
        enabled = lambda _package: True  # noqa: E731
    entries: list[tuple[str, str]] = []

    def add(command: str, target: str) -> None:
        link, relative = command_symlink(command, target)
        entries.append((link, relative))

    # Alpine keeps the Bourne shell and BusyBox applets in /bin.
    if enabled("busybox"):
        link = f"{BIN}/sh"
        entries.append((link, relative_symlink_target(link, f"{BIN}/busybox")))
    if enabled("cmd"):
        add("cmd", OPT_CMD + "/cmd.elf")
    if enabled("tcc"):
        add("tcc", OPT_TCC + "/tcc.elf")
    if enabled("lua"):
        add("lua", OPT_LUA + "/lua.elf")
    if enabled("python"):
        for alias in PYTHON_ALIASES:
            # python/python3 live in the same directory as the launcher.
            entries.append((f"{USR_BIN}/{alias}", PYTHON_LAUNCHER))
    if enabled("musl-gcc"):
        for alias in GCC_ALIASES:
            add(alias, OPT_DYNE + "/bin/" + GCC_LAUNCHER)
    return entries


def app_command_links(app: str) -> list[tuple[str, str]]:
    """Return ``(link, relative target)`` for an application package entry."""
    link = f"{USR_BIN}/{app}"
    return [(link, relative_symlink_target(link, str(app_exec_path(app))))]

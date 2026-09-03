# Application Registry

LeonOS 4 discovers runnable applications from package metadata instead of a
compiled-in application path table. At boot image creation, the build system
writes `manifest.ini` next to each staged executable:

```ini
[app]
id=notepad
name=Notepad
version=system
category=Desktop applications
exec=notepad.elf
icon=notepad.bmp
entry=1
terminal=0
hidden=0
open_with=1
extensions=.txt,.md,.log
commands=notepad
```

`exec` and `icon` are package-relative paths. The runtime rejects paths that
escape the package directory. Older `<package>.app.ini` files remain accepted
as a compatibility fallback, but new images use `manifest.ini`.

The registry scans `/system/apps` and `/programs`. Shells, the desktop start
menu, file associations, icons, GUI launch, and API installation all consume
the same records. API packages write their manifest after extraction, so a
newly installed application becomes discoverable without rebuilding the
system.

Applications and SDK clients can include `<leonos/app.h>` and use:

```c
leonos_app_registry_refresh();
leonos_app_registry_resolve("notepad", path, sizeof(path));
leonos_app_registry_default_for_extension(".txt", path, sizeof(path));
```

The public SDK header is installed at `devtools/include/leonos/app.h`.

# LeonOS DoomGeneric port

The LeonOS target builds DOOM and its launcher into the downloadable
`0:/api/doom.api` package. The default image does not install DOOM under
`0:/programs`; open the API package from the file manager to install it.
It uses a fullscreen GUI window, forwards keyboard scancodes from the desktop
server, and hides the mouse cursor while the window owns the hide request.

The repository includes the legally redistributable Freedoom 1 IWAD from
Freedoom 0.13.0:

```text
third_party/doomgeneric/freedoom1.wad
```

The package installs it as `0:/programs/doom/freedoom1.wad`. It replaces the
proprietary Doom IWAD for this port.

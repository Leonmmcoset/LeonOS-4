# LeonOS DoomGeneric port

The LeonOS target builds the DoomGeneric core as `programs/doom/doom.elf`.
It uses a fullscreen GUI window, forwards keyboard scancodes from the desktop
server, and hides the mouse cursor while the window owns the hide request.

The repository includes the legally redistributable Freedoom 1 IWAD from
Freedoom 0.13.0:

```text
third_party/doomgeneric/freedoom1.wad
```

The build copies it to `0:/programs/doom/freedoom1.wad`. It is used as a
runtime test asset and replaces the proprietary Doom IWAD for this port.

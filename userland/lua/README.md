# Lua on LeonOS 4

LeonOS packages the official Lua 5.4.8 interpreter as
`0:/programs/lua/lua.elf`. It is a static x86_64 user program linked against
Picolibc and the LeonOS user-space adapter archive.

The port deliberately uses Lua's portable `LUA_USE_C89` configuration. Lua
is compiled with the normal x86-64 SSE floating-point ABI. LeonOS saves and
restores the x87/SSE state for every user task, and this is required to match
Picolibc's `double` calling convention; compiling Lua with
`-mgeneral-regs-only` would make `lua_version()` and the math library receive
corrupted values. Lua scripts can be loaded from the current directory and from
`0:/programs/lua/lua/`. Dynamic C modules, `package.loadlib`, POSIX-only
features, and Readline support are not available until LeonOS gains a dynamic
linker and the related runtime ABI. LeonOS also has no process signal ABI, so
interactive `SIGINT` interruption is not available in this initial port.
`os.time` and `os.clock` use the LeonOS system-time service directly.

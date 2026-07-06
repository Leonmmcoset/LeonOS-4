# Third-Party Code

LeonOS keeps third-party source code in `third_party/` and records Git-backed
dependencies as submodules.

## litehtml

- Path: `third_party/litehtml`
- Upstream: `https://github.com/litehtml/litehtml.git`
- Recorded commit: `932439c91afb04dbce30903673292e3bf2da01dc`
- License: New BSD License / BSD-3-Clause, see `third_party/litehtml/LICENSE`

`browser.elf` does not yet link upstream litehtml directly because LeonOS
userland is still freestanding C without a C++ runtime or STL. The browser now
uses `userland/apps/browser/litehtml_core.c` as the staged C document layout
core. That keeps the browser shell, network loading, history, and GUI wiring
ready for a later full litehtml container.

Upstream litehtml documents Gumbo parser usage. Gumbo is Apache-2.0 licensed;
when LeonOS starts building the upstream litehtml parser path, include the
corresponding Gumbo license text in this document and in any generated release
notes.

# Browser and litehtml Porting Notes

LeonOS now ships `browser.elf`, a classic IE-style browser shell. It is a
staged path toward a full litehtml port, not a completed upstream litehtml
integration.

## Current Browser

`userland/apps/browser/main.c` is a freestanding C application that uses the
existing GUI, libc, launch, filesystem, and network ABIs.

Current features:

- IE-style menu/toolbar/address/status layout.
- `about:leonos` start page.
- `http://` navigation through `leonos_net_http_get`.
- Local `.html` and `.htm` file loading.
- Basic HTML text flow for headings, paragraphs, lists, breaks, tables rows,
  entities, and links.
- Link hit testing, relative URL resolution, Back, Forward, Refresh, Home, and
  mouse-wheel scrolling.
- Resize-aware reflow.

Current limits:

- No HTTPS/TLS.
- No JavaScript.
- No image loading.
- No CSS cascade/layout.
- No generic socket or streaming HTTP API; HTTP responses pass through the
  fixed `LEONOS_NET_HTTP_RESPONSE_MAX` ABI buffer.

## litehtml Status

Upstream litehtml is the intended rendering engine target. The current upstream
tree is C++ and depends heavily on STL types and library facilities such as
strings, vectors, maps, smart pointers, variants, algorithms, and bundled Gumbo
parser support. LeonOS userland is still freestanding C with a small libc and no
C++ runtime or STL build path.

Because of that, the browser app currently owns a small internal renderer with a
deliberately narrow surface. The replacement point is the document layout layer:
URL loading, navigation history, window chrome, status reporting, and launch
integration should remain useful when litehtml becomes available.

## Next Porting Steps

To integrate real litehtml, do these in order:

1. Add a userland C++ build mode in `tools/gen_ninja.py`.
2. Provide a minimal C++ runtime surface for constructors, destructors,
   allocation, exceptions-disabled builds, and required ABI helpers.
3. Port or provide an STL subset/libc++ profile that satisfies litehtml.
4. Build Gumbo and litehtml as userland libraries.
5. Implement a LeonOS litehtml container that maps text measurement, drawing,
   colors, clipping, mouse hit testing, and file/network resource fetches to
   the existing GUI and network APIs.
6. Replace the internal browser renderer while keeping the browser shell and
   launch integration.

## User Integration

The browser is registered as:

- `0:/userland/browser.elf`
- launch alias: `browser`
- default app for `.html` and `.htm`

The generated icon is `0:/userland/browser.bmp`, following the existing
same-directory/same-basename application icon convention.

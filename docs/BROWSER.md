# Browser and litehtml Porting Notes

LeonOS now ships `browser.elf`, a classic IE-style browser shell. Upstream
litehtml is present as a Git submodule under `third_party/litehtml`; the browser
currently links a freestanding C `litehtml_core` adapter while the full C++
litehtml runtime prerequisites are still being built.

## Current Browser

`userland/apps/browser/main.c` is a freestanding C application that uses the
existing GUI, libc, launch, filesystem, and network ABIs.

Current features:

- IE-style menu/toolbar/address/status layout.
- `about:leonos` start page.
- `http://` navigation through `leonos_net_http_get`.
- Local `.html` and `.htm` file loading.
- A `userland/apps/browser/litehtml_core.c` document layout layer for headings,
  paragraphs and block sections, lists, blockquotes, table rows/cells, breaks,
  horizontal rules, image placeholders with alt text, entities, links, and
  basic inline styles (`strong`/`b`, `em`/`i`, `code`).
- A small CSS v1 parser for `<style>` blocks and inline `style=""`, covering
  simple tag/class/id selectors plus `color`, `background-color`, `font-weight`,
  `font-style`, `text-decoration`, `text-align`, left indent properties, and
  basic borders.
- Link hit testing, relative URL resolution, Back, Forward, Refresh, Home, and
  mouse-wheel scrolling.
- Resize-aware reflow.

Current limits:

- No HTTPS/TLS.
- No JavaScript.
- No image loading.
- No full CSS cascade, box model, floats, flex/grid, media queries, or external
  stylesheets.
- No generic socket or streaming HTTP API; HTTP responses pass through the
  fixed `LEONOS_NET_HTTP_RESPONSE_MAX` ABI buffer.

## litehtml Status

Upstream litehtml is checked out as:

- submodule path: `third_party/litehtml`
- upstream URL: `https://github.com/litehtml/litehtml.git`
- current recorded commit: `932439c91afb04dbce30903673292e3bf2da01dc`

The current upstream tree is C++ and depends heavily on STL types and library
facilities such as strings, vectors, maps, smart pointers, variants, algorithms,
and bundled Gumbo parser support. LeonOS userland is still freestanding C with a
small libc and no C++ runtime or STL build path.

Because of that, the browser app currently owns a small C core with a deliberately
narrow surface. The replacement point is the document layout layer: URL loading,
navigation history, window chrome, status reporting, and launch integration
should remain useful when the full C++ litehtml container becomes available.

## Next Porting Steps

To integrate real litehtml, do these in order:

1. Add a userland C++ build mode in `tools/gen_ninja.py`.
2. Provide a minimal C++ runtime surface for constructors, destructors,
   allocation, exceptions-disabled builds, and required ABI helpers.
3. Port or provide an STL subset/libc++ profile that satisfies litehtml.
4. Build Gumbo and upstream litehtml as userland libraries from
   `third_party/litehtml`.
5. Replace `litehtml_core.c` with a LeonOS litehtml container that maps text measurement, drawing,
   colors, clipping, mouse hit testing, and file/network resource fetches to
   the existing GUI and network APIs.
6. Keep the browser shell, navigation, address bar, history, and app-launch
   integration stable while swapping the document engine.

## User Integration

The browser is registered as:

- `0:/userland/browser.elf`
- launch alias: `browser`
- default app for `.html` and `.htm`

The generated icon is `0:/userland/browser.bmp`, following the existing
same-directory/same-basename application icon convention.

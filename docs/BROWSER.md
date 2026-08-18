# Browser and litehtml Porting Notes

LeonOS now ships `browser.elf`, a classic IE-style browser shell. Upstream
litehtml is present as a Git submodule under `third_party/litehtml` and is the
browser's document parser, layout engine, and renderer.

## Current Browser

`userland/apps/browser/main.c` is a freestanding C application that uses the
existing GUI, libc, launch, filesystem, and network ABIs.

Current features:

- IE-style menu/toolbar/address layout with toast status messages.
- Times New Roman as the browser's Latin font, with SimSun as the Chinese
  fallback; both are loaded at runtime from external font files rather than
  embedded in `browser.elf`.
- Bilingual `about:leonos` start page.
- `http://` and `https://` navigation through the libc HTTP client on top of
  TCP sockets. HTTPS uses TLS 1.2 with certificate, hostname, and system-clock
  validation.
- Chrome-compatible desktop `User-Agent`, `Accept`, and language headers for
  sites that reject unknown clients.
- Local `.html` and `.htm` file loading.
- Download-link handoff: links ending in common binary/media/archive suffixes
  launch `downloadmgr.elf`, which saves the response to the current user's
  `Downloads` directory.
- Upstream litehtml layout and painting, backed by a LeonOS
  `document_container` in `userland/apps/browser/browser_litehtml.cpp`.
- Basic font metrics, text drawing, colors, borders, gradients, overflow
  clipping, CSS background repetition, local CSS imports, and local/remote PNG
  image loading through the LeonOS
  UI/libc APIs.
- HTTP/HTTPS stylesheet and PNG image resources are fetched through the
  existing cookie-aware browser HTTP client and resolved relative to the
  document URL.
- HTTP toast reporting, response content-type handling, bounded redirects,
  chunked transfer decoding, final URL reporting, and truncation indicators.
- HTML, CSS, and local text sources are normalized through the shared text
  decoder, including UTF-8, UTF-16, GBK, and GB2312 input.
- Link hit testing, relative URL resolution, Back, Forward, Refresh, Home, and
  mouse-wheel scrolling.
- Resize-aware reflow.

Current limits:

- No JavaScript.
- Form controls are native LiteHTML element adapters: text/password fields,
  textarea, checkbox/radio, keyboard Tab/Enter/Space navigation, select
  cycling, reset, and GET/POST submission are supported. JavaScript-driven
  form behavior is not available.
- HTTP uses `HTTP/1.1` over TCP sockets with `Connection: close`; HTTPS uses a
  TLS 1.2 client profile backed by Mbed TLS and the bundled CA roots. Cookies,
  cache, compression, and true streaming downloads are not implemented yet.

The repository owns the font build inputs: `system/fonts/times.ttf` is copied
to `0:/system/fonts/times-new-roman.ttf`, and `system/fonts/simsun.ttc` is
packaged as `0:/system/fonts/simsun.ttc`. Local and GitHub Actions builds use
these same repository files.

## litehtml Status

Upstream litehtml is checked out as:

- submodule path: `third_party/litehtml`
- upstream URL: `https://github.com/litehtml/litehtml.git`
- current recorded commit: `b9e89f0b9494ff9a5f008800af35503efabddf59`

The upstream tree is built into `build/userland/liblitehtml.a` and its bundled
Gumbo parser into `build/userland/libgumbo.a`. Browser and OOBE link both
archives with the host libc++ headers and LeonOS single-thread C++ compatibility
shims in `userland/apps/browser/cxx_compat.c`. The shims intentionally provide
only the no-thread/no-exception hooks needed by the freestanding build; they do
not provide Linux binary compatibility.

## Remaining Browser Work

1. Add JavaScript support only after a sandbox and resource policy are
   available.
2. Expand CSS/media coverage and add a bounded resource cache.

## User Integration

The browser is registered as:

- `0:/programs/browser/browser.elf`
- launch alias: `browser`
- default app for `.html` and `.htm`

Download and image companion apps are registered as:

- `0:/programs/downloadmgr/downloadmgr.elf`, launch alias `downloadmgr`
- `0:/programs/imageview/imageview.elf`, launch alias `imageview`
- default app for `.bmp` and `.dib`

The generated icon is `0:/programs/browser/browser.bmp`, following the existing
same-directory/same-basename application icon convention.

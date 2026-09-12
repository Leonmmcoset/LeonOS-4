# Python Runtime

The default `python` component ships the user-supplied CPython 3.14.7 build:
`cpython-3.14.7+20260901-x86_64-unknown-linux-musl-lto+static-full.tar.zst`.
SHA256: `e5a76e5893c39c89ed268ad71c3d6794c3be6b7236ec613449140c57686fc17f`.

The original static executable and complete `python/install` tree are packaged
in `/opt/python`; the image carries real relative symlinks for the aliases.
Archive licenses and `PYTHON.json` metadata are in `/usr/share/licenses/python`.
No interpreter or standard library source is patched. This does not certify
all Python modules against LeonOS's incomplete Linux ABI.

`/usr/bin/python3.14` is the static musl launcher; `/usr/bin/python` and
`/usr/bin/python3` are relative symlinks to it.
They locate `/opt/python` relative to the image root, set `PYTHONHOME` unless
the caller supplied it, and exec the original interpreter. No shell script
wrapper or `/install` alias is required. Bundled pip is available through
`python3 -m pip`; upstream script shebangs are preserved.

## Build

Supply the archive once (it is cached outside Git), then normal builds are offline:

```sh
LEONOS_PYTHON_ARCHIVE=/path/to/cpython-3.14.7+20260901-x86_64-unknown-linux-musl-lto+static-full.tar.zst python3 build.py run python
python3 build.py test python-package
python3 build.py run image-iso
python3 build.py run installer-image
```

Cache: `buildsystem/deps/python/` with the exact archive filename above.
Disable using `CONFIG_LEON_COMPONENT_TOOL_PYTHON_BUILD=n` or omit only the
image payload with `CONFIG_LEON_COMPONENT_TOOL_PYTHON_IMAGE=n`.
All standard images consume the same package staging tree, including the
installer's live environment and installed root payload.

Inside LeonOS:

```sh
python3 --version
python3 /usr/share/examples/python/hello.py
```

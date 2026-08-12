# Fastfetch for LeonOS

This is a LeonOS port of upstream [Fastfetch](https://github.com/fastfetch-cli/fastfetch)
v2.67.0. The upstream tree is kept unmodified in `third_party/fastfetch` at
commit `56da8f811068289f6352db8881418aa6e0f994e8` under the MIT license.

The port compiles upstream Fastfetch string, formatting, printing, size,
duration, percentage, display-option, ASCII-logo data, and module sources. The
small adapter in this directory supplies platform and detection interfaces from
the LeonOS public ABI rather than attempting to read Linux `/proc` or `/sys`.

Supported information modules are Title, Separator, OS, Kernel, CPU, Uptime,
Processes, Memory, DateTime, Break, Colors and Version. `Shell` and `Terminal`
are LeonOS-specific static rows. The default concise summary uses the standard
system rows and Colors; DateTime, Break and Version remain opt-in through
`--structure`. Use `--list-modules`, `--structure` (or `-s`), and
`--structure-disabled` to choose their order.

All 527 upstream built-in ASCII logos are included. `--logo <name>` (`-l`),
`--logo small`, `--logo none`, `--list-logos`, and `--print-logos` work with
the usual logo colors, dimensions, padding and left/top/right positioning. The
default remains the LeonOS logo. The upstream display options that affect the
available modules are supported, including `--pipe`, `--color*`, `--separator`,
`--key-*`, `--size-*`, `--duration-*`, `--percent-*`, and `--bar-*`.

The CPU row reads the x86 CPUID brand string exposed by the processor (with a
generic `x86_64 processor` fallback when the brand leaves are unavailable).

Configuration files, JSON output, image/file/command logos, dynamic refresh,
dynamic libraries, and modules requiring host POSIX or Linux interfaces remain
intentionally disabled.

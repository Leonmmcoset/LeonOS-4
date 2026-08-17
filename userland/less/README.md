# GNU less for LeonOS

LeonOS builds upstream `gwsw/less` at commit
`b8bbf4297169e20d35e1cc3e015180e8a011bcf2`. The executable is installed as
`0:/programs/less/less.elf` and the Ash/cmd command resolvers prefer it over
BusyBox's bounded fallback applet.

The port uses the shared LeonOS PTY, `poll()` and POSIX regular-expression
runtime, with a small ANSI termcap adapter in `leonos_termcap.c`. Normal
viewing, search, paging, `-N`, `-S`, `-R`, `-F` and window-size changes are
available. Shell escapes, external editor invocation, tags, user lesskey
files, logfile output and shell pipes are disabled in the build configuration.

`third_party/less/COPYING` and `third_party/less/LICENSE` are staged next to
the executable. The upstream source is available under the GNU GPL version 3
or the Less License; the LeonOS adapter is part of this repository.

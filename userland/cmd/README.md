# ChenPi11/cmd on LeonOS

LeonOS builds the upstream `cmd` interpreter as `0:/programs/cmd/cmd.elf`.
BusyBox Hush starts it with the `cmd` command.

The port retains the interpreter, built-in commands, batch files, variables,
redirection and the LeonOS terminal's canonical input mode. LeonOS currently
has no fork or pipe ABI, so `|`, command-backed `FOR /F`, and background
`START` features report that they are unsupported instead of attempting a
POSIX execution path. External commands resolve to the enabled BusyBox
applets and to `nano`, `pleditor`, `tcc`, `lua`, and `file` when staged.

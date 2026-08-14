# ChenPi11/cmd on LeonOS

LeonOS builds the upstream `cmd` interpreter as `0:/programs/cmd/cmd.elf`.
BusyBox Hush starts it with the `cmd` command.

The port retains the interpreter, built-in commands, batch files, variables,
redirection and the LeonOS terminal's canonical input mode. External-command
pipelines (`command1 | command2`, including longer chains) use LeonOS's
controlled spawn ABI and anonymous pipes. Pipeline stages must be external
commands without per-stage redirection. `command &` and external pipelines
ending in `&` run as tracked controlled-spawn jobs; `jobs`, `fg [%%job]`, and
`bg [%%job]` inspect or resume them. A job table holds up to 16 jobs and a job
may contain up to 64 pipeline stages. Built-ins, groups, conditional chains,
and per-stage redirection remain unsupported in background jobs because they
would require copying shell state. `SIGSTOP` and `SIGCONT` pause and resume
controlled child tasks. External commands resolve to the enabled BusyBox
applets and to `nano`, `pleditor`, `tcc`, `lua`, and `file` when staged.

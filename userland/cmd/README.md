# ChenPi11/cmd on LeonOS

LeonOS builds the upstream `cmd` interpreter as `0:/programs/cmd/cmd.elf`.
BusyBox Ash starts it with the `cmd` command.

The port retains the interpreter, built-in commands, batch files, variables,
redirection and the LeonOS terminal's canonical input mode. External commands
and foreground pipelines use the common COW `fork`/`pipe`/`dup2`/`execve`/
`waitpid` path, so pipeline stages can run built-ins and use their own
redirections. `command &` and external pipelines ending in `&` run as tracked
jobs; `jobs`, `fg [%%job]`, and `bg [%%job]` inspect or resume them. A job table
holds up to 16 jobs and a job may contain up to 64 pipeline stages. Built-ins,
groups, conditional chains, and per-stage redirection remain unsupported in
background jobs because they would require copying shell state into a separate
job process. Each foreground command or pipeline receives a dedicated process
group while it owns the PTY; background jobs keep their own group without
detaching the terminal session. `SIGSTOP` and `SIGCONT` pause and resume child
tasks. External
commands resolve to the enabled BusyBox applets and to `nano`, `pleditor`,
`tcc`, `lua`, `file`, `fastfetch`, and `sl` when staged.

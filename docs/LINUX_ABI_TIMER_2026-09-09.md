# POSIX timer ABI checkpoint

## Scope

This checkpoint covers native x86-64 Linux syscall numbers 222-226:

- `timer_create`
- `timer_settime`
- `timer_gettime`
- `timer_getoverrun`
- `timer_delete`

It also covers the `rt_sigtimedwait` path used by musl's `SIGEV_THREAD`
implementation.

## Implementation

The kernel now keeps timer objects in the creating task, validates clock ids,
notification modes, signal numbers, timespec ranges, timer flags, and user
pointers, and supports one-shot and periodic relative or absolute timers.
Timer expiry queues `SIGEV_SIGNAL` notifications in the shared process-pending
queue, while `SIGEV_THREAD_ID` is delivered to the requested thread. Both paths
wake a task parked in `rt_sigtimedwait` and account repeated expirations as
overrun. Timer
queries and deletion use the same task-owned object and return Linux-shaped
timespec values.

musl's `SIGEV_THREAD` sequence is supported without an application port:
musl's helper pthread creates a kernel `SIGEV_THREAD_ID` timer for signal 32,
blocks that signal, and waits for it through `rt_sigtimedwait`. The kernel
queues and wakes that exact thread.

## Evidence

`tools/tests/musl_guest_test.c` includes a raw musl regression covering
`SIGEV_NONE`, set/get/delete, and an actual `SIGEV_THREAD` callback. The test
was built in both static and dynamic forms and run from
`build/images/leonos4-musl-probes.iso` under QEMU/OVMF with 2 GiB RAM:

```
[musl-abi:static] END failed=0
[musl-abi:dynamic] END failed=0
```

The desktop Vim run used the unchanged static Linux Vim binary. Its serial log
contains no `E1286` or `Could not set timeout` message, and the test saved and
read back `/tmp/vimtest.txt`.

## Remaining boundary

The targeted behavior is implemented and QEMU-tested. Full Linux POSIX timer
coverage is still pending for complete `siginfo.si_timerid/si_overrun` data,
all clock sources, timer resource limits, and stress under SMP. VMware has not
been run in this environment.

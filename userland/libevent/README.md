# libevent for LeonOS

LeonOS builds libevent 2.1.12-stable as a static, poll-only dependency of
tmux. Threads, epoll, kqueue, OpenSSL, DNS, HTTP and RPC are not built.
The configuration headers in `include/` are target-owned and do not modify
the upstream submodule.

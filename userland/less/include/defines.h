/* LeonOS build-time configuration for upstream less. */
#ifndef LEONOS_LESS_DEFINES_H
#define LEONOS_LESS_DEFINES_H

#define MSDOS_COMPILER 0
#define PATHNAME_SEP "/"
#define TGETENT_OK 1
#define RETSIGTYPE void

/* Keep less a viewer: no shell, editor, file replacement or user config. */
#define SECURE_COMPILE 1
#define SECURE 1
#define SHELL_ESCAPE 0
#define EXAMINE 0
#define TAB_COMPLETE_FILENAME 0
/* The command editor unconditionally builds history-list helpers. Keep the
 * history in memory; SECURE prevents it from reading or writing .lesshst. */
#define CMD_HISTORY 1
#define HILITE_SEARCH 1
#define EDITOR 0
#define EDIT_PGM "vi"
#define TAGS 0
#define USERFILE 0
#define GLOB 0
#define PIPEC 0
#define LOGFILE 0
#define OSC8_LINK 0
#define GNU_OPTIONS 1
#define ONLY_RETURN 0
#define LESSKEYFILE ".less"
#define LESSKEYFILE_SYS "/system/config/lesskey"
#define DEF_LESSKEYINFILE ".lesskey"
#define LESSKEYINFILE_SYS "/system/config/lesskey"
#define LESSHISTFILE ".lesshst"

#define DEF_METACHARS "; *?\t\n'\"()<>|&"
#define DEF_METAESCAPE ""

#define HAVE_ANSI_PROTOS 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_PERROR 1
#define HAVE_TIME 1
#define HAVE_SHELL 0
#define HAVE_DUP 1
#define HAVE_LESSKEYSRC 0

#define CMDBUF_SIZE 2048
#define UNGOT_SIZE 200
#define LINEBUF_SIZE 1024
#define OUTBUF_SIZE 1024
#define PROMPT_SIZE 2048
#define TERMBUF_SIZE 2048
#define TERMSBUF_SIZE 1024
#define TAGLINE_SIZE 1024
#define TABSTOP_MAX 128
#define LINENUM_POOL 1024

#define HAVE_POSIX_REGCOMP 1
#define HAVE_V8_REGCOMP 0
#define HAVE_REGEXEC2 0
#define HAVE_GNU_REGEX 0
#define HAVE_PCRE 0
#define HAVE_PCRE2 0
#define HAVE_RE_COMP 0
#define HAVE_REGCMP 0
#define NO_REGEX 0

#define HAVE_VOID 1
#define HAVE_CONST 1
#define HAVE_STAT_INO 0
#define HAVE_TIME_T 1
#define HAVE_STRERROR 1
#define HAVE_FILENO 1
#define HAVE_ERRNO 1
#define MUST_DEFINE_ERRNO 0
#define HAVE_SETTABLE_ERRNO 1
#define HAVE_SYS_ERRLIST 0
#define HAVE_OSPEED 0
#define MUST_DEFINE_OSPEED 0
#define HAVE_LOCALE 0
#define HAVE_TERMIOS_FUNCS 1
#define HAVE_UPPER_LOWER 1
#define HAVE_SIGSET_T 0
#define HAVE_SIGEMPTYSET 0
#define HAVE_SIGSETJMP 0
#define HAVE_SIGPROCMASK 0
#define HAVE_SIGSETMASK 0
#define HAVE_MEMCPY 1
#define HAVE_POPEN 0
#define HAVE_STAT 1
#define HAVE_STRCHR 1
#define HAVE_STRSTR 1
#define HAVE_SYSTEM 0
#define HAVE_SNPRINTF 1
#define HAVE_CTYPE_H 1
#define HAVE_WCTYPE_H 0
#define HAVE_ERRNO_H 1
#define HAVE_FCNTL_H 1
#define HAVE_LIMITS_H 1
#define HAVE_STDIO_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRING_H 1
#define HAVE_STDINT_H 1
#define HAVE_STDCKDINT_H 0
#define HAVE_SYS_IOCTL_H 1
#define HAVE_SYS_PTEM_H 0
#define HAVE_SYS_STREAM_H 0
#define HAVE_TERMCAP_H 1
#define HAVE_NCURSES_TERMCAP_H 0
#define HAVE_NCURSESW_TERMCAP_H 0
#define HAVE_TERMINFO 0
#define HAVE_TPARM2 0
#define HAVE_TPARM8 0
#define HAVE_TPARM9 0
#define HAVE_TERMIO_H 0
#define HAVE_TERMIOS_H 1
#define HAVE_TIME_H 1
#define HAVE_UNISTD_H 1
#define HAVE_VALUES_H 0
#define HAVE_POLL 1
#define HAVE_FCHMOD 0
#define HAVE_FSYNC 0
#define HAVE_NANOSLEEP 1
#define HAVE_REALPATH 0
#define HAVE_STRSIGNAL 0
#define HAVE_TTYNAME 0
#define HAVE_USLEEP 1
#define HAVE_SYS_WAIT_H 0

/* Picolibc's freestanding limits.h omits ULONG_MAX for this target. */
#ifndef ULONG_MAX
#define ULONG_MAX (LONG_MAX * 2UL + 1UL)
#endif

#endif

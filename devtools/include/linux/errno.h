#ifndef LEONOS_UAPI_LINUX_ERRNO_H
#define LEONOS_UAPI_LINUX_ERRNO_H

/* Linux v6.12 asm-generic errno-base.h and errno.h, native x86-64.
 * Public E* names also support libc headers that include linux/errno.h. */
#define LINUX_EPERM 1
#ifndef EPERM
#define EPERM 1
#endif
#define LINUX_ENOENT 2
#ifndef ENOENT
#define ENOENT 2
#endif
#define LINUX_ESRCH 3
#ifndef ESRCH
#define ESRCH 3
#endif
#define LINUX_EINTR 4
#ifndef EINTR
#define EINTR 4
#endif
#define LINUX_EIO 5
#ifndef EIO
#define EIO 5
#endif
#define LINUX_ENXIO 6
#ifndef ENXIO
#define ENXIO 6
#endif
#define LINUX_E2BIG 7
#ifndef E2BIG
#define E2BIG 7
#endif
#define LINUX_ENOEXEC 8
#ifndef ENOEXEC
#define ENOEXEC 8
#endif
#define LINUX_EBADF 9
#ifndef EBADF
#define EBADF 9
#endif
#define LINUX_ECHILD 10
#ifndef ECHILD
#define ECHILD 10
#endif
#define LINUX_EAGAIN 11
#ifndef EAGAIN
#define EAGAIN 11
#endif
#define LINUX_ENOMEM 12
#ifndef ENOMEM
#define ENOMEM 12
#endif
#define LINUX_EACCES 13
#ifndef EACCES
#define EACCES 13
#endif
#define LINUX_EFAULT 14
#ifndef EFAULT
#define EFAULT 14
#endif
#define LINUX_ENOTBLK 15
#ifndef ENOTBLK
#define ENOTBLK 15
#endif
#define LINUX_EBUSY 16
#ifndef EBUSY
#define EBUSY 16
#endif
#define LINUX_EEXIST 17
#ifndef EEXIST
#define EEXIST 17
#endif
#define LINUX_EXDEV 18
#ifndef EXDEV
#define EXDEV 18
#endif
#define LINUX_ENODEV 19
#ifndef ENODEV
#define ENODEV 19
#endif
#define LINUX_ENOTDIR 20
#ifndef ENOTDIR
#define ENOTDIR 20
#endif
#define LINUX_EISDIR 21
#ifndef EISDIR
#define EISDIR 21
#endif
#define LINUX_EINVAL 22
#ifndef EINVAL
#define EINVAL 22
#endif
#define LINUX_ENFILE 23
#ifndef ENFILE
#define ENFILE 23
#endif
#define LINUX_EMFILE 24
#ifndef EMFILE
#define EMFILE 24
#endif
#define LINUX_ENOTTY 25
#ifndef ENOTTY
#define ENOTTY 25
#endif
#define LINUX_ETXTBSY 26
#ifndef ETXTBSY
#define ETXTBSY 26
#endif
#define LINUX_EFBIG 27
#ifndef EFBIG
#define EFBIG 27
#endif
#define LINUX_ENOSPC 28
#ifndef ENOSPC
#define ENOSPC 28
#endif
#define LINUX_ESPIPE 29
#ifndef ESPIPE
#define ESPIPE 29
#endif
#define LINUX_EROFS 30
#ifndef EROFS
#define EROFS 30
#endif
#define LINUX_EMLINK 31
#ifndef EMLINK
#define EMLINK 31
#endif
#define LINUX_EPIPE 32
#ifndef EPIPE
#define EPIPE 32
#endif
#define LINUX_EDOM 33
#ifndef EDOM
#define EDOM 33
#endif
#define LINUX_ERANGE 34
#ifndef ERANGE
#define ERANGE 34
#endif
#define LINUX_EDEADLK 35
#ifndef EDEADLK
#define EDEADLK 35
#endif
#define LINUX_ENAMETOOLONG 36
#ifndef ENAMETOOLONG
#define ENAMETOOLONG 36
#endif
#define LINUX_ENOLCK 37
#ifndef ENOLCK
#define ENOLCK 37
#endif
#define LINUX_ENOSYS 38
#ifndef ENOSYS
#define ENOSYS 38
#endif
#define LINUX_ENOTEMPTY 39
#ifndef ENOTEMPTY
#define ENOTEMPTY 39
#endif
#define LINUX_ELOOP 40
#ifndef ELOOP
#define ELOOP 40
#endif
#define LINUX_EWOULDBLOCK LINUX_EAGAIN
#ifndef EWOULDBLOCK
#define EWOULDBLOCK EAGAIN
#endif
#define LINUX_ENOMSG 42
#ifndef ENOMSG
#define ENOMSG 42
#endif
#define LINUX_EIDRM 43
#ifndef EIDRM
#define EIDRM 43
#endif
#define LINUX_ECHRNG 44
#ifndef ECHRNG
#define ECHRNG 44
#endif
#define LINUX_EL2NSYNC 45
#ifndef EL2NSYNC
#define EL2NSYNC 45
#endif
#define LINUX_EL3HLT 46
#ifndef EL3HLT
#define EL3HLT 46
#endif
#define LINUX_EL3RST 47
#ifndef EL3RST
#define EL3RST 47
#endif
#define LINUX_ELNRNG 48
#ifndef ELNRNG
#define ELNRNG 48
#endif
#define LINUX_EUNATCH 49
#ifndef EUNATCH
#define EUNATCH 49
#endif
#define LINUX_ENOCSI 50
#ifndef ENOCSI
#define ENOCSI 50
#endif
#define LINUX_EL2HLT 51
#ifndef EL2HLT
#define EL2HLT 51
#endif
#define LINUX_EBADE 52
#ifndef EBADE
#define EBADE 52
#endif
#define LINUX_EBADR 53
#ifndef EBADR
#define EBADR 53
#endif
#define LINUX_EXFULL 54
#ifndef EXFULL
#define EXFULL 54
#endif
#define LINUX_ENOANO 55
#ifndef ENOANO
#define ENOANO 55
#endif
#define LINUX_EBADRQC 56
#ifndef EBADRQC
#define EBADRQC 56
#endif
#define LINUX_EBADSLT 57
#ifndef EBADSLT
#define EBADSLT 57
#endif
#define LINUX_EDEADLOCK LINUX_EDEADLK
#ifndef EDEADLOCK
#define EDEADLOCK EDEADLK
#endif
#define LINUX_EBFONT 59
#ifndef EBFONT
#define EBFONT 59
#endif
#define LINUX_ENOSTR 60
#ifndef ENOSTR
#define ENOSTR 60
#endif
#define LINUX_ENODATA 61
#ifndef ENODATA
#define ENODATA 61
#endif
#define LINUX_ETIME 62
#ifndef ETIME
#define ETIME 62
#endif
#define LINUX_ENOSR 63
#ifndef ENOSR
#define ENOSR 63
#endif
#define LINUX_ENONET 64
#ifndef ENONET
#define ENONET 64
#endif
#define LINUX_ENOPKG 65
#ifndef ENOPKG
#define ENOPKG 65
#endif
#define LINUX_EREMOTE 66
#ifndef EREMOTE
#define EREMOTE 66
#endif
#define LINUX_ENOLINK 67
#ifndef ENOLINK
#define ENOLINK 67
#endif
#define LINUX_EADV 68
#ifndef EADV
#define EADV 68
#endif
#define LINUX_ESRMNT 69
#ifndef ESRMNT
#define ESRMNT 69
#endif
#define LINUX_ECOMM 70
#ifndef ECOMM
#define ECOMM 70
#endif
#define LINUX_EPROTO 71
#ifndef EPROTO
#define EPROTO 71
#endif
#define LINUX_EMULTIHOP 72
#ifndef EMULTIHOP
#define EMULTIHOP 72
#endif
#define LINUX_EDOTDOT 73
#ifndef EDOTDOT
#define EDOTDOT 73
#endif
#define LINUX_EBADMSG 74
#ifndef EBADMSG
#define EBADMSG 74
#endif
#define LINUX_EOVERFLOW 75
#ifndef EOVERFLOW
#define EOVERFLOW 75
#endif
#define LINUX_ENOTUNIQ 76
#ifndef ENOTUNIQ
#define ENOTUNIQ 76
#endif
#define LINUX_EBADFD 77
#ifndef EBADFD
#define EBADFD 77
#endif
#define LINUX_EREMCHG 78
#ifndef EREMCHG
#define EREMCHG 78
#endif
#define LINUX_ELIBACC 79
#ifndef ELIBACC
#define ELIBACC 79
#endif
#define LINUX_ELIBBAD 80
#ifndef ELIBBAD
#define ELIBBAD 80
#endif
#define LINUX_ELIBSCN 81
#ifndef ELIBSCN
#define ELIBSCN 81
#endif
#define LINUX_ELIBMAX 82
#ifndef ELIBMAX
#define ELIBMAX 82
#endif
#define LINUX_ELIBEXEC 83
#ifndef ELIBEXEC
#define ELIBEXEC 83
#endif
#define LINUX_EILSEQ 84
#ifndef EILSEQ
#define EILSEQ 84
#endif
#define LINUX_ERESTART 85
#ifndef ERESTART
#define ERESTART 85
#endif
#define LINUX_ESTRPIPE 86
#ifndef ESTRPIPE
#define ESTRPIPE 86
#endif
#define LINUX_EUSERS 87
#ifndef EUSERS
#define EUSERS 87
#endif
#define LINUX_ENOTSOCK 88
#ifndef ENOTSOCK
#define ENOTSOCK 88
#endif
#define LINUX_EDESTADDRREQ 89
#ifndef EDESTADDRREQ
#define EDESTADDRREQ 89
#endif
#define LINUX_EMSGSIZE 90
#ifndef EMSGSIZE
#define EMSGSIZE 90
#endif
#define LINUX_EPROTOTYPE 91
#ifndef EPROTOTYPE
#define EPROTOTYPE 91
#endif
#define LINUX_ENOPROTOOPT 92
#ifndef ENOPROTOOPT
#define ENOPROTOOPT 92
#endif
#define LINUX_EPROTONOSUPPORT 93
#ifndef EPROTONOSUPPORT
#define EPROTONOSUPPORT 93
#endif
#define LINUX_ESOCKTNOSUPPORT 94
#ifndef ESOCKTNOSUPPORT
#define ESOCKTNOSUPPORT 94
#endif
#define LINUX_EOPNOTSUPP 95
#ifndef EOPNOTSUPP
#define EOPNOTSUPP 95
#endif
#define LINUX_EPFNOSUPPORT 96
#ifndef EPFNOSUPPORT
#define EPFNOSUPPORT 96
#endif
#define LINUX_EAFNOSUPPORT 97
#ifndef EAFNOSUPPORT
#define EAFNOSUPPORT 97
#endif
#define LINUX_EADDRINUSE 98
#ifndef EADDRINUSE
#define EADDRINUSE 98
#endif
#define LINUX_EADDRNOTAVAIL 99
#ifndef EADDRNOTAVAIL
#define EADDRNOTAVAIL 99
#endif
#define LINUX_ENETDOWN 100
#ifndef ENETDOWN
#define ENETDOWN 100
#endif
#define LINUX_ENETUNREACH 101
#ifndef ENETUNREACH
#define ENETUNREACH 101
#endif
#define LINUX_ENETRESET 102
#ifndef ENETRESET
#define ENETRESET 102
#endif
#define LINUX_ECONNABORTED 103
#ifndef ECONNABORTED
#define ECONNABORTED 103
#endif
#define LINUX_ECONNRESET 104
#ifndef ECONNRESET
#define ECONNRESET 104
#endif
#define LINUX_ENOBUFS 105
#ifndef ENOBUFS
#define ENOBUFS 105
#endif
#define LINUX_EISCONN 106
#ifndef EISCONN
#define EISCONN 106
#endif
#define LINUX_ENOTCONN 107
#ifndef ENOTCONN
#define ENOTCONN 107
#endif
#define LINUX_ESHUTDOWN 108
#ifndef ESHUTDOWN
#define ESHUTDOWN 108
#endif
#define LINUX_ETOOMANYREFS 109
#ifndef ETOOMANYREFS
#define ETOOMANYREFS 109
#endif
#define LINUX_ETIMEDOUT 110
#ifndef ETIMEDOUT
#define ETIMEDOUT 110
#endif
#define LINUX_ECONNREFUSED 111
#ifndef ECONNREFUSED
#define ECONNREFUSED 111
#endif
#define LINUX_EHOSTDOWN 112
#ifndef EHOSTDOWN
#define EHOSTDOWN 112
#endif
#define LINUX_EHOSTUNREACH 113
#ifndef EHOSTUNREACH
#define EHOSTUNREACH 113
#endif
#define LINUX_EALREADY 114
#ifndef EALREADY
#define EALREADY 114
#endif
#define LINUX_EINPROGRESS 115
#ifndef EINPROGRESS
#define EINPROGRESS 115
#endif
#define LINUX_ESTALE 116
#ifndef ESTALE
#define ESTALE 116
#endif
#define LINUX_EUCLEAN 117
#ifndef EUCLEAN
#define EUCLEAN 117
#endif
#define LINUX_ENOTNAM 118
#ifndef ENOTNAM
#define ENOTNAM 118
#endif
#define LINUX_ENAVAIL 119
#ifndef ENAVAIL
#define ENAVAIL 119
#endif
#define LINUX_EISNAM 120
#ifndef EISNAM
#define EISNAM 120
#endif
#define LINUX_EREMOTEIO 121
#ifndef EREMOTEIO
#define EREMOTEIO 121
#endif
#define LINUX_EDQUOT 122
#ifndef EDQUOT
#define EDQUOT 122
#endif
#define LINUX_ENOMEDIUM 123
#ifndef ENOMEDIUM
#define ENOMEDIUM 123
#endif
#define LINUX_EMEDIUMTYPE 124
#ifndef EMEDIUMTYPE
#define EMEDIUMTYPE 124
#endif
#define LINUX_ECANCELED 125
#ifndef ECANCELED
#define ECANCELED 125
#endif
#define LINUX_ENOKEY 126
#ifndef ENOKEY
#define ENOKEY 126
#endif
#define LINUX_EKEYEXPIRED 127
#ifndef EKEYEXPIRED
#define EKEYEXPIRED 127
#endif
#define LINUX_EKEYREVOKED 128
#ifndef EKEYREVOKED
#define EKEYREVOKED 128
#endif
#define LINUX_EKEYREJECTED 129
#ifndef EKEYREJECTED
#define EKEYREJECTED 129
#endif
#define LINUX_EOWNERDEAD 130
#ifndef EOWNERDEAD
#define EOWNERDEAD 130
#endif
#define LINUX_ENOTRECOVERABLE 131
#ifndef ENOTRECOVERABLE
#define ENOTRECOVERABLE 131
#endif
#define LINUX_ERFKILL 132
#ifndef ERFKILL
#define ERFKILL 132
#endif
#define LINUX_EHWPOISON 133
#ifndef EHWPOISON
#define EHWPOISON 133
#endif

#endif

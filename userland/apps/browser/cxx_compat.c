#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* The browser is single-threaded.  These symbols satisfy libc++'s optional
 * host-thread hooks without pretending that LeonOS has a pthread ABI. */
int pthread_once(void *control, void (*init_routine)(void))
{
    uint32_t *state = (uint32_t *)control;
    if (state && !*state) {
        *state = 1;
        if (init_routine) {
            init_routine();
        }
    }
    return 0;
}

int pthread_mutex_lock(void *mutex)
{
    (void)mutex;
    return 0;
}

int pthread_mutex_unlock(void *mutex)
{
    (void)mutex;
    return 0;
}

int pthread_rwlock_rdlock(void *lock)
{
    (void)lock;
    return 0;
}

int pthread_rwlock_wrlock(void *lock)
{
    (void)lock;
    return 0;
}

int pthread_rwlock_unlock(void *lock)
{
    (void)lock;
    return 0;
}

static const void *leonos_pthread_values[32];

int pthread_key_create(unsigned long *key, void (*destructor)(void *))
{
    static unsigned long next_key = 1;
    (void)destructor;
    if (!key) {
        return -1;
    }
    *key = next_key++;
    return 0;
}

int pthread_setspecific(unsigned long key, const void *value)
{
    if (key >= 32) {
        return -1;
    }
    leonos_pthread_values[key] = value;
    return 0;
}

void *pthread_getspecific(unsigned long key)
{
    if (key >= 32) {
        return 0;
    }
    return (void *)leonos_pthread_values[key];
}

int __vfprintf_chk(FILE *stream, int flags, const char *format, va_list args)
{
    (void)flags;
    return vfprintf(stream, format, args);
}

int __fprintf_chk(FILE *stream, int flags, const char *format, ...)
{
    va_list args;
    int result;
    (void)flags;
    va_start(args, format);
    result = vfprintf(stream, format, args);
    va_end(args);
    return result;
}

void __assert_fail(const char *expression, const char *file,
                   unsigned int line, const char *function)
{
    (void)expression;
    (void)file;
    (void)line;
    (void)function;
    abort();
}

int dl_iterate_phdr(void *callback, void *data)
{
    (void)callback;
    (void)data;
    return 0;
}

/* Exceptions are disabled for the renderer.  These are only referenced by
 * unused libc++ ABI fallbacks pulled from the host archive. */
int _Unwind_RaiseException(void *exception)
{
    (void)exception;
    return -1;
}

void _Unwind_Resume(void *exception)
{
    (void)exception;
    abort();
}

void _Unwind_DeleteException(void *exception)
{
    (void)exception;
}

void _Unwind_SetGR(void *context, int reg, uintptr_t value)
{
    (void)context;
    (void)reg;
    (void)value;
}

void _Unwind_SetIP(void *context, uintptr_t value)
{
    (void)context;
    (void)value;
}

uintptr_t _Unwind_GetIP(void *context)
{
    (void)context;
    return 0;
}

uintptr_t _Unwind_GetGR(void *context, int reg)
{
    (void)context;
    (void)reg;
    return 0;
}

void *_Unwind_GetLanguageSpecificData(void *context)
{
    (void)context;
    return 0;
}

uintptr_t _Unwind_GetRegionStart(void *context)
{
    (void)context;
    return 0;
}

const unsigned short **__ctype_b_loc(void)
{
    static const unsigned short table[384];
    static const unsigned short *value = table + 128;
    return &value;
}

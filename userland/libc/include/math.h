#ifndef LEONOS_MATH_H
#define LEONOS_MATH_H

static inline double fabs(double value)
{
    return value < 0.0 ? -value : value;
}

#ifndef isgreater
#define isgreater(left, right) __builtin_isgreater((left), (right))
#endif
#ifndef isless
#define isless(left, right) __builtin_isless((left), (right))
#endif
#ifndef isunordered
#define isunordered(left, right) __builtin_isunordered((left), (right))
#endif

#endif

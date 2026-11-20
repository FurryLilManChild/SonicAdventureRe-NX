/* SonicAdventureRe-nx: AArch64 glibc's bits/math-vector.h declares NEON vector
 * versions of libm's functions, which an x86-64 front end can't parse. The
 * native slice calls none, so this keeps the scalar declarations alone */
#ifndef _MATH_H
#error "Never include <bits/math-vector.h> directly; include <math.h> instead."
#endif
#include <bits/libm-simd-decl-stubs.h>

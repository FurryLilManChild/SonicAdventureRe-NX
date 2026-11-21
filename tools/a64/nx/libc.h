/* SonicAdventureRe-nx: included first in the Switch build's host files (those that use the C
 * library, not the game's freestanding ones). newlib declares strupr, which the game's SDK
 * declares too, with another type; newlib's is kept out of the way under another name */
#ifndef SAREN_NX_LIBC_H
#define SAREN_NX_LIBC_H
#define strupr newlib_strupr
#define strlwr newlib_strlwr
#include <string.h>
#undef strupr
#undef strlwr
#endif

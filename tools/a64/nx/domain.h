/* SonicAdventureRe-nx: the Switch build's domain is wherever the loader puts it, a 4 GB window
 * Horizon leaves free that launch. The link places native_domain_base at the window's start, and
 * code reaches it relative to itself, so the address follows the image when the loader moves it */
#ifndef SAREN_NX_DOMAIN_H
#define SAREN_NX_DOMAIN_H
extern char native_domain_base[];
#endif

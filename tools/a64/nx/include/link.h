/* SonicAdventureRe-nx: ElfW, which symbols.c takes from link.h on Linux */
#ifndef SAREN_NX_LINK_H
#define SAREN_NX_LINK_H
#include <elf.h>
#define ElfW(type) Elf64_##type
#endif

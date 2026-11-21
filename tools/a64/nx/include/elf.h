/* SonicAdventureRe-nx: the ELF types symbols.c reads, which newlib lacks (64-bit only) */
#ifndef SAREN_NX_ELF_H
#define SAREN_NX_ELF_H
#include <stdint.h>
typedef struct {
    unsigned char e_ident[16];
    uint16_t e_type, e_machine;
    uint32_t e_version;
    uint64_t e_entry, e_phoff, e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx;
} Elf64_Ehdr;
typedef struct {
    uint32_t sh_name, sh_type;
    uint64_t sh_flags, sh_addr, sh_offset, sh_size;
    uint32_t sh_link, sh_info;
    uint64_t sh_addralign, sh_entsize;
} Elf64_Shdr;
typedef struct {
    uint32_t st_name;
    unsigned char st_info, st_other;
    uint16_t st_shndx;
    uint64_t st_value, st_size;
} Elf64_Sym;
#define SHT_SYMTAB 2
#define STT_OBJECT 1
#define STT_FUNC 2
#define STT_FILE 4
#endif

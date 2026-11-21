// SonicAdventureRe-nx: the domain's marker. The image links for window 5; code reaches
// native_domain_base relative to itself, and a 64-bit word holding it is an address the loader
// moves (switch/tools/mkimage.py), so at run time it is the window the loader chose.
    .globl native_domain_base
    .set native_domain_base, 0x500000000

// libnx's start (crt0) reads _DYNAMIC through its MOD0 header; a fixed-address image has no
// dynamic relocations, so it is an empty table
    .section .rodata.nx_dynamic, "a"
    .balign 8
    .globl _DYNAMIC
_DYNAMIC:
    .quad 0, 0

// the window the image links for, as a number the loader doesn't move (no relocation), for the log
    .section .rodata.nx_link_window, "a"
    .balign 8
    .globl nx_link_window_number
nx_link_window_number:
    .quad 0x500000000

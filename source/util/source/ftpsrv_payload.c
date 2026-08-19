#ifndef ONIONHEN_FTPSRV_ELF
#define ONIONHEN_FTPSRV_ELF "../../../.cache/dependencies/ftpsrv-ps5.elf"
#endif

__asm__(".intel_syntax noprefix\n"
        ".section .rodata\n"
        ".global ftpsrv_start\n"
        ".type ftpsrv_start, @object\n"
        ".align 16\n"
        "ftpsrv_start:\n"
        ".incbin \"" ONIONHEN_FTPSRV_ELF "\"\n"
        "ftpsrv_end:\n"
        ".global ftpsrv_size\n"
        ".type ftpsrv_size, @object\n"
        ".align 4\n"
        "ftpsrv_size:\n"
        ".int ftpsrv_end - ftpsrv_start\n");
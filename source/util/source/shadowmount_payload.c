#ifndef ONIONHEN_SHADOWMOUNT_ELF
#define ONIONHEN_SHADOWMOUNT_ELF "../../../.cache/dependencies/shadowmountplus.elf"
#endif

__asm__(".intel_syntax noprefix\n"
        ".section .rodata\n"
        ".global shadowmount_start\n"
        ".type shadowmount_start, @object\n"
        ".align 16\n"
        "shadowmount_start:\n"
        ".incbin \"" ONIONHEN_SHADOWMOUNT_ELF "\"\n"
        "shadowmount_end:\n"
        ".global shadowmount_size\n"
        ".type shadowmount_size, @object\n"
        ".align 4\n"
        "shadowmount_size:\n"
        ".int shadowmount_end - shadowmount_start\n");

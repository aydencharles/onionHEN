/* Embeds the overlay ShellUI module (shellui built with
 * -DONIONHEN_FPS_OVERLAY_ONLY=ON). FPS_OVERLAY_ELF is the absolute path. */
#ifndef FPS_OVERLAY_ELF
#error "FPS_OVERLAY_ELF must point to the overlay shellui.elf"
#endif

__asm__(
	".section .rodata\n"
	".global fps_overlay_elf_start\n"
	".type   fps_overlay_elf_start, @object\n"
	".align  16\n"
	"fps_overlay_elf_start:\n"
		".incbin \"" FPS_OVERLAY_ELF "\"\n"
	"fps_overlay_elf_end:\n"
	".text\n"
);

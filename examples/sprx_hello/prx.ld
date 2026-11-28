PHDRS
{
  text    PT_LOAD    FLAGS(1); /* PF_X (PS5 execute-only xotext) */
  rodata  PT_LOAD    FLAGS(4); /* PF_R */
  data    PT_LOAD    FLAGS(6); /* PF_R | PF_W */
  dyn     PT_LOAD    FLAGS(0); /* temporary holder replaced by elf2ps5prx.py */
  dynamic PT_DYNAMIC FLAGS(6);
}

SECTIONS
{
  . = 0x0;

  .text : ALIGN(0x4000) {
    /* Keep _init at a non-zero VA so PS5 dynlib never treats DT_INIT as null. */
    BYTE(0x90); . = 0x10;
    *(.text .text.*)
  } :text

  .plt : ALIGN(16) {
    *(.plt .plt.*)
  } :text

  . = ALIGN(0x4000);

  .rodata : ALIGN(0x4000) {
    *(.rodata .rodata.*)
  } :rodata

  .eh_frame : ALIGN(8) {
    *(.eh_frame)
  } :rodata

  . = ALIGN(0x4000);

  .data.rel.ro : ALIGN(0x4000) {
    *(.data.rel.ro .data.rel.ro.*)
  } :data

  .init_array : ALIGN(8) {
    PROVIDE_HIDDEN(__init_array_start = .);
    KEEP(*(.init_array .init_array.*))
    PROVIDE_HIDDEN(__init_array_end = .);
  } :data

  .fini_array : ALIGN(8) {
    PROVIDE_HIDDEN(__fini_array_start = .);
    KEEP(*(.fini_array .fini_array.*))
    PROVIDE_HIDDEN(__fini_array_end = .);
  } :data

  .got : ALIGN(8) {
    *(.got)
  } :data

  .got.plt : ALIGN(8) {
    *(.got.plt)
  } :data

  .data.sce_module_param : ALIGN(8) {
    KEEP(*(.data.sce_module_param))
  } :data

  .data : ALIGN(8) {
    *(.data .data.*)
  } :data

  .bss : ALIGN(8) {
    *(.bss .bss.* COMMON)
  } :data

  . = ALIGN(0x4000);

  .dynsym   : { *(.dynsym) }   :dyn
  .dynstr   : { *(.dynstr) }   :dyn
  .hash     : { *(.hash) }     :dyn
  .rela.dyn : { *(.rela.dyn) } :dyn
  .rela.plt : { *(.rela.plt) } :dyn
  .dynamic  : { *(.dynamic) }  :dyn :dynamic

  /DISCARD/ : {
    *(.comment)
    *(.note.*)
    *(.gnu.hash)
  }
}

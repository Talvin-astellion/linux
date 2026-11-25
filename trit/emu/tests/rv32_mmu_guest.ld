OUTPUT_ARCH(riscv)
ENTRY(_start)

SECTIONS
{
	. = 0x80400000;
	.text : { *(.text.start) *(.text*) }
	.rodata : { *(.rodata*) }
	.data : { *(.data*) *(.sdata*) }
	.bss : { *(.bss*) *(.sbss*) }
	. = ALIGN(16);
	__stack_top = . + 4096;
	. = __stack_top;
}

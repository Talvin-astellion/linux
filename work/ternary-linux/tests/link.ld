/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Link layout shared by all ternary Linux guest ISA tests.  The image is
 * loaded by tern-linux at KERNEL_ADDR (0x80400000) and runs in S-mode.
 */

OUTPUT_ARCH(riscv)
ENTRY(_start)

SECTIONS
{
	. = 0x80400000;
	.text : {
		*(.text.start)
		*(.text*)
	}

	.rodata : ALIGN(4) {
		*(.rodata*)
	}

	.data : ALIGN(4) {
		*(.data*)
	}

	.bss : ALIGN(4) {
		__bss_start = .;
		*(.bss*)
		*(COMMON)
		. = ALIGN(4);
		__bss_end = .;
	}

	. = ALIGN(16);
	__global_pointer$ = . + 0x800;
	__stack_top = . + 0x4000;
}

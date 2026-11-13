/* SPDX-License-Identifier: GPL-2.0-only */
OUTPUT_ARCH(riscv)
ENTRY(_start)

SECTIONS
{
	. = 0;
	.text : {
		*(.text.start)
		*(.text*)
		*(.rodata*)
	}
	/DISCARD/ : {
		*(.comment)
		*(.note*)
		*(.eh_frame*)
	}
}

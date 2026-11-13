/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Minimal flat-format init for the ternary Linux guest.
 */

	.section .text.start, "ax"
	.globl _start
_start:
	li	a0, 1
	la	a1, msg
	li	a2, 17
	li	a7, 64
	ecall

	li	a0, 0
	li	a7, 93
	ecall

1:
	j	1b

	.section .rodata, "a"
msg:
	.ascii "TERNARY_LINUX_OK\n"
msg_end:

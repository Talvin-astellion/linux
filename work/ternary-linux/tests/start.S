/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Freestanding entry point for the ternary Linux guest ISA tests.
 *
 * The interpreter starts in S-mode with no stack, so the entry code sets up
 * gp/sp, clears BSS, calls test_main(), and reports an unexpected return as a
 * failure through the test device.
 */

	.section .text.start, "ax"
	.globl _start
_start:
	.option push
	.option norelax
	la	gp, __global_pointer$
	.option pop
	la	sp, __stack_top

	la	t0, __bss_start
	la	t1, __bss_end
1:
	bgeu	t0, t1, 2f
	sw	zero, 0(t0)
	addi	t0, t0, 4
	j	1b
2:
	call	test_main

	li	t0, 0x3333
	li	t1, 0x00100000
	sw	t0, 0(t1)
3:
	j	3b

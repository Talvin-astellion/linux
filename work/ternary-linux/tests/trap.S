/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Trap entry points used by test_sbi.c.
 *
 * u_trap_entry validates an ecall from U-mode (scause 8), advances sepc,
 * returns to S-mode, and records that the trap was observed.
 *
 * timer_trap_entry records a timer interrupt and returns to the interrupted
 * instruction.
 */

	.section .text, "ax"
	.align 2
	.globl u_trap_entry
u_trap_entry:
	csrr	t0, scause
	li	t1, 8
	bne	t0, t1, 3f

	csrr	t0, sepc
	addi	t0, t0, 4
	csrw	sepc, t0

	csrr	t0, sstatus
	li	t1, 0x100		/* SSTATUS.SPP */
	or	t0, t0, t1
	csrw	sstatus, t0

	la	t0, u_trap_seen
	li	t1, 1
	sw	t1, 0(t0)
	sret

3:
	li	t0, 0x3333
	li	t1, 0x00100000
	sw	t0, 0(t1)
4:
	j	4b

	.align 2
	.globl timer_trap_entry
timer_trap_entry:
	la	t0, timer_trap_seen
	li	t1, 1
	sw	t1, 0(t0)
	sret

	.section .bss, "aw", @nobits
	.align 2
	.globl u_trap_seen
u_trap_seen:
	.word	0
	.globl timer_trap_seen
timer_trap_seen:
	.word	0

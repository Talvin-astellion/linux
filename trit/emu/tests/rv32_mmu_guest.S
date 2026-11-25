	.option norelax
	.section .text.start, "ax"
	.globl _start
_start:
	la	sp, __stack_top

	la	t0, pg_root
	la	t1, pg_l2
	srli	t1, t1, 12
	slli	t1, t1, 10
	ori	t1, t1, 0x1
	li	t2, 0x804
	add	t2, t0, t2
	sw	t1, 0(t2)

	li	t1, 0x0000000f
	sw	t1, 0(t0)

	la	t0, pg_l2
	li	t2, 0x80400000
	li	t3, 1024
	li	t4, 0x1000
1:
	srli	a0, t2, 12
	slli	a0, a0, 10
	ori	a0, a0, 0xf
	sw	a0, 0(t0)
	addi	t0, t0, 4
	add	t2, t2, t4
	addi	t3, t3, -1
	bnez	t3, 1b

	la	t0, pg_root
	srli	t0, t0, 12
	li	t1, 0x80000000
	or	t0, t0, t1
	csrw	satp, t0
	sfence.vma

	la	t0, test_data
	li	t1, 0x12345678
	sw	t1, 0(t0)
	lw	t2, 0(t0)
	bne	t1, t2, fail

	la	t0, pg_root
	li	t1, 0x20300019
	li	t2, 0x80c
	add	t2, t0, t2
	sw	t1, 0(t2)
	sfence.vma

	la	t0, u_fetch_trap
	csrw	stvec, t0
	li	t0, 0x80c00000
	jr	t0
	j	fail

u_fetch_trap:
	csrr	t0, scause
	li	t1, 12
	bne	t0, t1, fail
	li	t0, 0x5555
	li	t1, 0x00100000
	sw	t0, 0(t1)
2:
	j	2b

fail:
	li	t0, 0x3333
	li	t1, 0x00100000
	sw	t0, 0(t1)
3:
	j	3b

	.section .data
	.align 4
test_data:
	.word 0

	.section .bss
	.balign 4096
pg_root:
	.zero 4096
	.balign 4096
pg_l2:
	.zero 4096
	.balign 16
__stack_top:
	.zero 4096

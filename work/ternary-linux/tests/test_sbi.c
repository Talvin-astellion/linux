/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * SBI, CSR and trap regression tests for the tern-linux interpreter.
 *
 * Covers the SBI base/time/DBCN extensions, CSR access, a timer interrupt and
 * an ecall trap from U-mode.
 */

#include "test.h"

extern void u_trap_entry(void);
extern void timer_trap_entry(void);
extern volatile uint32_t u_trap_seen;
extern volatile uint32_t timer_trap_seen;

#define SBI_EXT_BASE  0x10u
#define SBI_EXT_TIME  0x54494d45u
#define SBI_EXT_IPI   0x00735049u
#define SBI_EXT_RFENCE 0x52464e43u
#define SBI_EXT_SRST  0x53525354u
#define SBI_EXT_DBCN  0x4442434eu

#define SSTATUS_SIE   0x00000002u
#define SSTATUS_SPP   0x00000100u
#define SIE_STIE      0x00000020u

static inline long sbi_ecall(unsigned long eid, unsigned long fid,
			     unsigned long a0, unsigned long a1,
			     unsigned long a2, unsigned long *value)
{
	register unsigned long r_a0 asm("a0") = a0;
	register unsigned long r_a1 asm("a1") = a1;
	register unsigned long r_a2 asm("a2") = a2;
	register unsigned long r_a6 asm("a6") = fid;
	register unsigned long r_a7 asm("a7") = eid;

	asm volatile("ecall"
		     : "+r"(r_a0), "+r"(r_a1)
		     : "r"(r_a2), "r"(r_a6), "r"(r_a7)
		     : "memory");
	if (value)
		*value = r_a1;
	return (long)r_a0;
}

static inline uint32_t csr_misa(void)
{
	uint32_t value;

	asm volatile("csrr %0, misa" : "=r"(value));
	return value;
}

static inline uint32_t csr_time(void)
{
	uint32_t value;

	asm volatile("csrr %0, time" : "=r"(value));
	return value;
}

static inline uint32_t csr_instret(void)
{
	uint32_t value;

	asm volatile("csrr %0, instret" : "=r"(value));
	return value;
}

static inline uint32_t csr_cycle(void)
{
	uint32_t value;

	asm volatile("csrr %0, cycle" : "=r"(value));
	return value;
}

static inline uint32_t csr_sscratch(void)
{
	uint32_t value;

	asm volatile("csrr %0, sscratch" : "=r"(value));
	return value;
}

static inline void csr_set_sscratch(uint32_t value)
{
	asm volatile("csrw sscratch, %0" : : "r"(value) : "memory");
}

static inline uint32_t csr_sstatus(void)
{
	uint32_t value;

	asm volatile("csrr %0, sstatus" : "=r"(value));
	return value;
}

static inline void csr_set_sstatus(uint32_t value)
{
	asm volatile("csrw sstatus, %0" : : "r"(value) : "memory");
}

static inline void csr_set_stvec(uint32_t value)
{
	asm volatile("csrw stvec, %0" : : "r"(value) : "memory");
}

static inline void csr_set_sepc(uint32_t value)
{
	asm volatile("csrw sepc, %0" : : "r"(value) : "memory");
}

static inline void csr_set_sie(uint32_t value)
{
	asm volatile("csrw sie, %0" : : "r"(value) : "memory");
}

static void user_ecall(void)
{
	asm volatile("ecall" : : : "memory");

	/*
	 * The trap handler advances sepc and returns to S-mode, so execution
	 * continues here with u_trap_seen already set.
	 */
	CHECK_EQ(u_trap_seen, 1u);
	test_pass();
}

void test_main(void)
{
	unsigned long value = 0;
	uint32_t before;
	uint32_t after;
	uint32_t i;

	test_puts("sbi\n");

	/* SBI base extension. */
	CHECK_EQ(sbi_ecall(SBI_EXT_BASE, 0, 0, 0, 0, &value), 0);
	CHECK_EQ(value, 0x02000000u);
	CHECK_EQ(sbi_ecall(SBI_EXT_BASE, 1, 0, 0, 0, &value), 0);
	CHECK_EQ(value, 0u);

	CHECK_EQ(sbi_ecall(SBI_EXT_BASE, 3, SBI_EXT_TIME, 0, 0, &value), 0);
	CHECK_EQ(value, 1u);
	CHECK_EQ(sbi_ecall(SBI_EXT_BASE, 3, SBI_EXT_IPI, 0, 0, &value), 0);
	CHECK_EQ(value, 1u);
	CHECK_EQ(sbi_ecall(SBI_EXT_BASE, 3, SBI_EXT_RFENCE, 0, 0, &value), 0);
	CHECK_EQ(value, 1u);
	CHECK_EQ(sbi_ecall(SBI_EXT_BASE, 3, SBI_EXT_SRST, 0, 0, &value), 0);
	CHECK_EQ(value, 1u);
	CHECK_EQ(sbi_ecall(SBI_EXT_BASE, 3, SBI_EXT_DBCN, 0, 0, &value), 0);
	CHECK_EQ(value, 1u);
	CHECK_EQ(sbi_ecall(SBI_EXT_BASE, 3, 0x12345678u, 0, 0, &value), 0);
	CHECK_EQ(value, 0u);

	/* SBI time: program a deadline far in the future. */
	CHECK_EQ(sbi_ecall(SBI_EXT_TIME, 0, 0xffffffffu, 0xffffffffu, 0, &value),
		 0);

	/* SBI DBCN: putchar and a buffer write. */
	CHECK_EQ(sbi_ecall(SBI_EXT_DBCN, 2, 'S', 0, 0, &value), 0);
	{
		static const char msg[] = "BI\n";

		CHECK_EQ(sbi_ecall(SBI_EXT_DBCN, 0, sizeof(msg) - 1,
				   (unsigned long)msg, 0, &value), 0);
		CHECK_EQ(value, sizeof(msg) - 1);
	}

	/* CSR access. */
	CHECK_EQ(csr_misa(), 0x40001101u);
	csr_set_sscratch(0x5a5a5a5au);
	CHECK_EQ(csr_sscratch(), 0x5a5a5a5au);

	before = csr_instret();
	after = csr_cycle();
	CHECK(after >= before);
	before = csr_time();
	for (i = 0; i < 100; i++)
		asm volatile("nop");
	after = csr_time();
	CHECK(after > before);

	/* Timer interrupt. */
	csr_set_sstatus(csr_sstatus() & ~SSTATUS_SIE);
	csr_set_sie(0);
	csr_set_stvec((uint32_t)&timer_trap_entry);
	timer_trap_seen = 0;
	before = csr_time();
	CHECK_EQ(sbi_ecall(SBI_EXT_TIME, 0, before + 1000u, 0, 0, &value), 0);
	csr_set_sie(SIE_STIE);
	csr_set_sstatus(csr_sstatus() | SSTATUS_SIE);
	for (i = 0; i < 1000000u && timer_trap_seen == 0; i++)
		asm volatile("nop");
	csr_set_sstatus(csr_sstatus() & ~SSTATUS_SIE);
	csr_set_sie(0);
	CHECK_EQ(timer_trap_seen, 1u);

	/* U-mode ecall must trap with scause 8 and return to S-mode. */
	u_trap_seen = 0;
	csr_set_stvec((uint32_t)&u_trap_entry);
	csr_set_sstatus(csr_sstatus() & ~(SSTATUS_SIE | SSTATUS_SPP));
	csr_set_sepc((uint32_t)&user_ecall);
	asm volatile("sret" : : : "memory");

	test_fail(__LINE__);
}

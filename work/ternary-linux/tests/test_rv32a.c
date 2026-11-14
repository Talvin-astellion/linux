/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * RV32A regression tests for the tern-linux interpreter.
 *
 * Covers LR/SC success and failure paths plus every AMO operation exposed by
 * the interpreter.
 */

#include "test.h"

static volatile uint32_t cell;

static inline uint32_t rv_lr(volatile uint32_t *p)
{
	uint32_t result;

	asm volatile("lr.w %0, (%1)" : "=r"(result) : "r"(p) : "memory");
	return result;
}

static inline uint32_t rv_sc(volatile uint32_t *p, uint32_t value)
{
	uint32_t result;

	asm volatile("sc.w %0, %2, (%1)"
		     : "=&r"(result) : "r"(p), "r"(value) : "memory");
	return result;
}

#define RV_AMO(name)							\
	static inline uint32_t rv_##name(volatile uint32_t *p,		\
					 uint32_t value)			\
	{								\
		uint32_t result;					\
									\
		asm volatile(#name ".w %0, %2, (%1)"			\
			     : "=r"(result) : "r"(p), "r"(value)	\
			     : "memory");				\
		return result;						\
	}

RV_AMO(amoswap)
RV_AMO(amoadd)
RV_AMO(amoxor)
RV_AMO(amoand)
RV_AMO(amoor)
RV_AMO(amomin)
RV_AMO(amomax)
RV_AMO(amominu)
RV_AMO(amomaxu)

void test_main(void)
{
	test_puts("rv32a\n");

	/* A successful LR/SC sequence. */
	cell = 0x11223344u;
	CHECK_EQ(rv_lr((volatile uint32_t *)&cell), 0x11223344u);
	CHECK_EQ(rv_sc((volatile uint32_t *)&cell, 0x55667788u), 0u);
	CHECK_EQ(cell, 0x55667788u);

	/* SC without a reservation fails and does not store. */
	CHECK_EQ(rv_sc((volatile uint32_t *)&cell, 0xdeadbeefu), 1u);
	CHECK_EQ(cell, 0x55667788u);

	/* A plain store after LR invalidates the reservation. */
	CHECK_EQ(rv_lr((volatile uint32_t *)&cell), 0x55667788u);
	cell = 0x01020304u;
	CHECK_EQ(rv_sc((volatile uint32_t *)&cell, 0x0a0b0c0du), 1u);
	CHECK_EQ(cell, 0x01020304u);

	/* Swap. */
	cell = 10u;
	CHECK_EQ(rv_amoswap((volatile uint32_t *)&cell, 20u), 10u);
	CHECK_EQ(cell, 20u);

	/* Arithmetic and logic AMOs return the old value. */
	cell = 0x10u;
	CHECK_EQ(rv_amoadd((volatile uint32_t *)&cell, 0x05u), 0x10u);
	CHECK_EQ(cell, 0x15u);

	cell = 0xf0u;
	CHECK_EQ(rv_amoxor((volatile uint32_t *)&cell, 0x0fu), 0xf0u);
	CHECK_EQ(cell, 0xffu);

	cell = 0xf0u;
	CHECK_EQ(rv_amoand((volatile uint32_t *)&cell, 0x3cu), 0xf0u);
	CHECK_EQ(cell, 0x30u);

	cell = 0xf0u;
	CHECK_EQ(rv_amoor((volatile uint32_t *)&cell, 0x0fu), 0xf0u);
	CHECK_EQ(cell, 0xffu);

	/* Signed MIN/MAX. */
	cell = (uint32_t)-5;
	CHECK_EQ(rv_amomin((volatile uint32_t *)&cell, 3u), (uint32_t)-5);
	CHECK_EQ(cell, (uint32_t)-5);
	CHECK_EQ(rv_amomax((volatile uint32_t *)&cell, 3u), (uint32_t)-5);
	CHECK_EQ(cell, 3u);

	/* Unsigned MIN/MAX. */
	cell = 0xffffffffu;
	CHECK_EQ(rv_amominu((volatile uint32_t *)&cell, 1u), 0xffffffffu);
	CHECK_EQ(cell, 1u);
	CHECK_EQ(rv_amomaxu((volatile uint32_t *)&cell, 0x80000000u), 1u);
	CHECK_EQ(cell, 0x80000000u);

	test_pass();
}

/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * RV32M regression tests for the tern-linux interpreter.
 *
 * Each multiply/divide variant is exercised through an explicit instruction
 * so that the compiler cannot lower the operation to a libgcc helper.
 */

#include "test.h"

static inline uint32_t rv_mul(uint32_t a, uint32_t b)
{
	uint32_t result;

	asm volatile("mul %0, %1, %2" : "=r"(result) : "r"(a), "r"(b));
	return result;
}

static inline uint32_t rv_mulh(int32_t a, int32_t b)
{
	int32_t result;

	asm volatile("mulh %0, %1, %2" : "=r"(result) : "r"(a), "r"(b));
	return (uint32_t)result;
}

static inline uint32_t rv_mulhsu(int32_t a, uint32_t b)
{
	int32_t result;

	asm volatile("mulhsu %0, %1, %2" : "=r"(result) : "r"(a), "r"(b));
	return (uint32_t)result;
}

static inline uint32_t rv_mulhu(uint32_t a, uint32_t b)
{
	uint32_t result;

	asm volatile("mulhu %0, %1, %2" : "=r"(result) : "r"(a), "r"(b));
	return result;
}

static inline uint32_t rv_div(int32_t a, int32_t b)
{
	int32_t result;

	asm volatile("div %0, %1, %2" : "=r"(result) : "r"(a), "r"(b));
	return (uint32_t)result;
}

static inline uint32_t rv_divu(uint32_t a, uint32_t b)
{
	uint32_t result;

	asm volatile("divu %0, %1, %2" : "=r"(result) : "r"(a), "r"(b));
	return result;
}

static inline uint32_t rv_rem(int32_t a, int32_t b)
{
	int32_t result;

	asm volatile("rem %0, %1, %2" : "=r"(result) : "r"(a), "r"(b));
	return (uint32_t)result;
}

static inline uint32_t rv_remu(uint32_t a, uint32_t b)
{
	uint32_t result;

	asm volatile("remu %0, %1, %2" : "=r"(result) : "r"(a), "r"(b));
	return result;
}

void test_main(void)
{
	test_puts("rv32m\n");

	/* Multiplication: low word, signed/unsigned high words. */
	CHECK_EQ(rv_mul(7u, 6u), 42u);
	CHECK_EQ(rv_mul(0xffffffffu, 2u), 0xfffffffeu);
	CHECK_EQ(rv_mul(0x10000u, 0x10000u), 0u);

	CHECK_EQ(rv_mulh(0x40000000, 4), 1u);
	CHECK_EQ(rv_mulh(-1, -1), 0u);
	CHECK_EQ(rv_mulh((int32_t)0x80000000u, 2), 0xffffffffu);

	CHECK_EQ(rv_mulhsu((int32_t)0x80000000u, 2u), 0xffffffffu);
	CHECK_EQ(rv_mulhsu(0x40000000, 4u), 1u);

	CHECK_EQ(rv_mulhu(0xffffffffu, 0xffffffffu), 0xfffffffeu);
	CHECK_EQ(rv_mulhu(0x40000000u, 4u), 1u);

	/* Division truncates toward zero; remainders follow the dividend. */
	CHECK_EQ(rv_div(7, 2), 3u);
	CHECK_EQ(rv_div(-7, 2), (uint32_t)-3);
	CHECK_EQ(rv_div(7, -2), (uint32_t)-3);
	CHECK_EQ(rv_div(-7, -2), 3u);

	CHECK_EQ(rv_rem(7, 2), 1u);
	CHECK_EQ(rv_rem(-7, 2), (uint32_t)-1);
	CHECK_EQ(rv_rem(7, -2), 1u);
	CHECK_EQ(rv_rem(-7, -2), (uint32_t)-1);

	/* Division by zero and signed overflow are defined by RV32M. */
	CHECK_EQ(rv_div(123, 0), 0xffffffffu);
	CHECK_EQ(rv_divu(123u, 0u), 0xffffffffu);
	CHECK_EQ(rv_rem(123, 0), 123u);
	CHECK_EQ(rv_remu(123u, 0u), 123u);
	CHECK_EQ(rv_div((int32_t)0x80000000u, -1), 0x80000000u);
	CHECK_EQ(rv_rem((int32_t)0x80000000u, -1), 0u);

	/* Unsigned division. */
	CHECK_EQ(rv_divu(0xffffffffu, 2u), 0x7fffffffu);
	CHECK_EQ(rv_remu(0xffffffffu, 2u), 1u);
	CHECK_EQ(rv_divu(100u, 7u), 14u);
	CHECK_EQ(rv_remu(100u, 7u), 2u);

	test_pass();
}

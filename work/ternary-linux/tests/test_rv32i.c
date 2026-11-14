/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * RV32I regression tests for the tern-linux interpreter.
 *
 * The tests cover integer arithmetic, logical operations, shifts, signed and
 * unsigned comparisons, branches, jumps, loads/stores (including misaligned
 * accesses) and the fence instructions.
 */

#include "test.h"

static volatile uint16_t halfs[4];
static volatile uint8_t bytes[16];
static volatile uint8_t mem[16];

static inline uint32_t rv_lw(const volatile void *base)
{
	uint32_t result;

	asm volatile("lw %0, 0(%1)" : "=r"(result) : "r"(base) : "memory");
	return result;
}

static inline void rv_sw(volatile void *base, uint32_t value)
{
	asm volatile("sw %1, 0(%0)" : : "r"(base), "r"(value) : "memory");
}

static inline uint32_t rv_auipc(void)
{
	uint32_t result;

	asm volatile("auipc %0, 0" : "=r"(result));
	return result;
}

static inline uint32_t rv_sll(uint32_t a, uint32_t b)
{
	uint32_t result;

	asm volatile("sll %0, %1, %2" : "=r"(result) : "r"(a), "r"(b));
	return result;
}

static inline uint32_t rv_srl(uint32_t a, uint32_t b)
{
	uint32_t result;

	asm volatile("srl %0, %1, %2" : "=r"(result) : "r"(a), "r"(b));
	return result;
}

static inline uint32_t rv_sra(uint32_t a, uint32_t b)
{
	uint32_t result;

	asm volatile("sra %0, %1, %2" : "=r"(result) : "r"(a), "r"(b));
	return result;
}

static inline uint32_t rv_lw_misaligned(const void *base)
{
	uint32_t result;

	asm volatile("lw %0, 1(%1)" : "=r"(result) : "r"(base) : "memory");
	return result;
}

static inline void rv_sw_misaligned(void *base, uint32_t value)
{
	asm volatile("sw %1, 1(%0)" : : "r"(base), "r"(value) : "memory");
}

static inline void rv_fences(void)
{
	asm volatile("fence\n\tfence.i" : : : "memory");
}

static uint32_t add_one(uint32_t value)
{
	return value + 1;
}

static int32_t sign_compare(int32_t a, int32_t b)
{
	return a < b;
}

static uint32_t unsigned_compare(uint32_t a, uint32_t b)
{
	return a < b;
}

void test_main(void)
{
	uint32_t (*fn)(uint32_t) = add_one;

	test_puts("rv32i\n");

	/* Arithmetic and immediates. */
	CHECK_EQ(1u + 2u, 3u);
	CHECK_EQ(0x7fffffffu + 1u, 0x80000000u);
	CHECK_EQ(0x10u - 0x21u, 0xffffffefu);
	CHECK_EQ(0u - 1u, 0xffffffffu);
	CHECK_EQ((uint32_t)0x12345000u, 0x12345000u);
	CHECK_EQ(0xfffff000u, (uint32_t)(int32_t)-4096);
	CHECK_EQ(0xffffffffu, (uint32_t)(int32_t)-1);

	/* Logical operations. */
	CHECK_EQ(0xf0f0u & 0x0ff0u, 0x00f0u);
	CHECK_EQ(0xf0f0u | 0x0ff0u, 0xfff0u);
	CHECK_EQ(0xf0f0u ^ 0x0ff0u, 0xff00u);

	/* Shifts by register and by immediate. */
	CHECK_EQ(rv_sll(1u, 31u), 0x80000000u);
	CHECK_EQ(rv_srl(0x80000000u, 31u), 1u);
	CHECK_EQ(rv_sra(0x80000000u, 31u), 0xffffffffu);
	CHECK_EQ(1u << 5, 32u);
	CHECK_EQ(0x80000000u >> 5, 0x04000000u);
	CHECK_EQ((uint32_t)((int32_t)0x80000000u >> 5), 0xfc000000u);

	/* Signed and unsigned comparisons. */
	CHECK_EQ(sign_compare(-1, 0), 1);
	CHECK_EQ(sign_compare(0, -1), 0);
	CHECK_EQ(unsigned_compare(0xffffffffu, 1u), 0u);
	CHECK_EQ(unsigned_compare(1u, 0xffffffffu), 1u);

	/* AUIPC and JALR. */
	CHECK(rv_auipc() != 0u);
	CHECK((rv_auipc() & 3u) == 0u);
	CHECK_EQ(fn(41u), 42u);

	/* Branches, including a loop. */
	{
		uint32_t sum = 0;
		int32_t i;

		for (i = -3; i <= 3; i++)
			sum += (uint32_t)i;
		CHECK_EQ(sum, 0u);
	}

	{
		uint32_t acc = 0;
		uint32_t i = 0;

		while (i < 10u) {
			acc += i;
			i++;
		}
		CHECK_EQ(acc, 45u);
	}

	/* Byte and halfword loads with sign/zero extension. */
	bytes[0] = 0x7fu;
	bytes[1] = 0x80u;
	bytes[2] = 0xffu;
	bytes[3] = 0x01u;
	CHECK_EQ((uint32_t)(int32_t)(int8_t)bytes[0], 0x0000007fu);
	CHECK_EQ((uint32_t)(int32_t)(int8_t)bytes[1], 0xffffff80u);
	CHECK_EQ((uint32_t)bytes[2], 0x000000ffu);

	halfs[0] = 0x7fffu;
	halfs[1] = 0x8000u;
	halfs[2] = 0xffffu;
	CHECK_EQ((uint32_t)(int32_t)(int16_t)halfs[0], 0x00007fffu);
	CHECK_EQ((uint32_t)(int32_t)(int16_t)halfs[1], 0xffff8000u);
	CHECK_EQ((uint32_t)halfs[2], 0x0000ffffu);

	/* Word loads/stores and little-endian byte order. */
	rv_sw((volatile void *)mem, 0x11223344u);
	CHECK_EQ(rv_lw((const volatile void *)mem), 0x11223344u);
	CHECK_EQ(mem[0], 0x44u);
	CHECK_EQ(mem[1], 0x33u);
	CHECK_EQ(mem[2], 0x22u);
	CHECK_EQ(mem[3], 0x11u);

	/* Misaligned accesses are emulated by the interpreter. */
	{
		int i;

		for (i = 0; i < 8; i++)
			mem[i] = (uint8_t)(0x10 + i);
		CHECK_EQ(rv_lw_misaligned((const void *)mem), 0x14131211u);
		rv_sw_misaligned((void *)mem, 0xa1b2c3d4u);
		CHECK_EQ(mem[1], 0xd4u);
		CHECK_EQ(mem[2], 0xc3u);
		CHECK_EQ(mem[3], 0xb2u);
		CHECK_EQ(mem[4], 0xa1u);
	}

	rv_fences();
	test_pass();
}

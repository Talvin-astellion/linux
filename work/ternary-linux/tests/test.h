/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Minimal freestanding test helpers for the ternary Linux guest tests.
 *
 * The emulator maps a 4 KiB test device at 0x00100000.  A 32-bit store of
 * 0x5555 stops the machine with success, 0x3333 (or 0x7777) with failure.
 */

#ifndef TERNARY_LINUX_TEST_H
#define TERNARY_LINUX_TEST_H

#include <stdint.h>

#define TEST_PORT       (*(volatile uint32_t *)0x00100000u)
#define TEST_PASS_CODE  0x5555u
#define TEST_FAIL_CODE  0x3333u

static inline void test_putc(char c)
{
	register long a0 asm("a0") = (long)(unsigned char)c;
	register long a7 asm("a7") = 1;

	/*
	 * SBI calls return a0 (error) and a1 (value), so the compiler must not
	 * assume a1 survives the ecall even for the legacy console extension.
	 */
	asm volatile("ecall" : "+r"(a0) : "r"(a7) : "a1", "memory");
}

static inline void test_puts(const char *s)
{
	while (*s)
		test_putc(*s++);
}

static inline void test_put_uint(uint32_t value)
{
	char buf[10];
	int i = 0;

	if (value == 0) {
		test_putc('0');
		return;
	}
	while (value != 0 && i < (int)sizeof(buf)) {
		buf[i++] = (char)('0' + (value % 10u));
		value /= 10u;
	}
	while (i > 0)
		test_putc(buf[--i]);
}

static inline __attribute__((noreturn)) void test_fail(uint32_t line)
{
	test_puts("FAIL line ");
	test_put_uint(line);
	test_puts("\n");
	TEST_PORT = TEST_FAIL_CODE;
	for (;;)
		asm volatile("nop");
}

static inline __attribute__((noreturn)) void test_pass(void)
{
	TEST_PORT = TEST_PASS_CODE;
	for (;;)
		asm volatile("nop");
}

#define CHECK(cond)							\
	do {								\
		if (!(cond))						\
			test_fail(__LINE__);				\
	} while (0)

#define CHECK_EQ(actual, expected)					\
	do {								\
		uint32_t _a = (uint32_t)(actual);			\
		uint32_t _e = (uint32_t)(expected);			\
		if (_a != _e) {						\
			test_puts("mismatch line ");			\
			test_put_uint(__LINE__);			\
			test_puts(": ");				\
			test_put_uint(_a);				\
			test_puts(" != ");				\
			test_put_uint(_e);				\
			test_putc('\n');				\
			test_fail(__LINE__);				\
		}							\
	} while (0)

void test_main(void);

#endif /* TERNARY_LINUX_TEST_H */

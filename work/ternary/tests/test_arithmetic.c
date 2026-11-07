/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include <stdint.h>

#include "test_framework.h"
#include "test_helpers.h"
#include "ternary.h"

#define RANDOM_SAMPLES 2000

static uint64_t random_state = 0x9e3779b97f4a7c15ULL;

static uint64_t next_random(void)
{
	random_state ^= random_state << 13;
	random_state ^= random_state >> 7;
	random_state ^= random_state << 17;
	return random_state;
}

static trint32_t random_word(unsigned width)
{
	trint32_t word = 0;
	unsigned i;

	for (i = 0; i < width; i++)
		word = packed_set(word, i, (int)(next_random() % 3) - 1);
	return word;
}

static int64_t width_min(unsigned width)
{
	return -(int64_t)((pow3_i128(width) - 1) / 2);
}

static int64_t width_max(unsigned width)
{
	return (int64_t)((pow3_i128(width) - 1) / 2);
}

static void test_add_exhaustive(void)
{
	unsigned width;

	for (width = 1; width <= 5; width++) {
		int64_t left;
		int64_t min = width_min(width);
		int64_t max = width_max(width);

		for (left = min; left <= max; left++) {
			int64_t right;

			for (right = min; right <= max; right++) {
				trint32_t result = ternaryADD(
					pack_from_i64(left, width),
					pack_from_i64(right, width));

				CHECK_EQ_I64(unpack_to_i64(result, width),
					     (int64_t)ref_add(left, right,
							      width));
			}
		}
	}
}

static void test_add_random(void)
{
	unsigned sample;

	for (sample = 0; sample < RANDOM_SAMPLES; sample++) {
		int64_t left = (int64_t)wrap_balanced(
			(__int128)(next_random() & 0x3fffffffffffffffULL),
			32);
		int64_t right = (int64_t)wrap_balanced(
			(__int128)(next_random() & 0x3fffffffffffffffULL),
			32);
		trint32_t result = ternaryADD(pack_from_i64(left, 32),
					      pack_from_i64(right, 32));

		CHECK_EQ_I64(unpack_to_i64(result, 32),
			     (int64_t)ref_add(left, right, 32));
	}
}

static void test_adc_exhaustive(void)
{
	unsigned width;

	for (width = 1; width <= 4; width++) {
		int64_t left;
		int64_t min = width_min(width);
		int64_t max = width_max(width);

		for (left = min; left <= max; left++) {
			int64_t right;

			for (right = min; right <= max; right++) {
				int carry_in;

				for (carry_in = -1; carry_in <= 1;
				     carry_in++) {
					trit_t carry =
						int2trit(carry_in);
					trint32_t result = ternaryADC(
						pack_from_i64(left, width),
						pack_from_i64(right, width),
						&carry);
					__int128 total = (__int128)left +
							 right + carry_in;
					__int128 sum =
						wrap_balanced(total, 32);
					__int128 carry_out =
						(total - sum) /
						pow3_i128(32);

					CHECK_EQ_I64(
						unpack_to_i64(result, 32),
						(int64_t)sum);
					CHECK_EQ_INT(trit2int(carry),
						     (int)carry_out);
				}
			}
		}
	}
}

static void test_adc_random(void)
{
	unsigned sample;

	for (sample = 0; sample < RANDOM_SAMPLES; sample++) {
		int64_t left = (int64_t)wrap_balanced(
			(__int128)(next_random() & 0x3fffffffffffffffULL),
			32);
		int64_t right = (int64_t)wrap_balanced(
			(__int128)(next_random() & 0x3fffffffffffffffULL),
			32);
		int carry_in = (int)(next_random() % 3) - 1;
		trit_t carry = int2trit(carry_in);
		trint32_t result = ternaryADC(pack_from_i64(left, 32),
					      pack_from_i64(right, 32),
					      &carry);
		__int128 total = (__int128)left + right + carry_in;
		__int128 sum = wrap_balanced(total, 32);
		__int128 carry_out = (total - sum) / pow3_i128(32);

		CHECK_EQ_I64(unpack_to_i64(result, 32), (int64_t)sum);
		CHECK_EQ_INT(trit2int(carry), (int)carry_out);
	}
}

static void test_mul_exhaustive(void)
{
	unsigned width;

	for (width = 1; width <= 5; width++) {
		int64_t left;
		int64_t min = width_min(width);
		int64_t max = width_max(width);

		for (left = min; left <= max; left++) {
			int64_t right;

			for (right = min; right <= max; right++) {
				trint32_t result = ternaryMUL(
					pack_from_i64(left, width),
					pack_from_i64(right, width));

				CHECK_EQ_I64(unpack_to_i64(result, width),
					     (int64_t)ref_mul(left, right,
							      width));
			}
		}
	}
}

static void test_mul_random(void)
{
	unsigned sample;

	for (sample = 0; sample < RANDOM_SAMPLES; sample++) {
		int64_t left = (int64_t)wrap_balanced(
			(__int128)(next_random() & 0x3fffffffffffffffULL),
			32);
		int64_t right = (int64_t)wrap_balanced(
			(__int128)(next_random() & 0x3fffffffffffffffULL),
			32);
		trint32_t result = ternaryMUL(pack_from_i64(left, 32),
					      pack_from_i64(right, 32));

		CHECK_EQ_I64(unpack_to_i64(result, 32),
			     (int64_t)ref_mul(left, right, 32));
	}

	for (sample = 0; sample < RANDOM_SAMPLES; sample++) {
		int64_t left = (int64_t)wrap_balanced(
			(__int128)(next_random() & 0xffffffffULL), 9);
		int64_t right = (int64_t)wrap_balanced(
			(__int128)(next_random() & 0xffffffffULL), 9);
		trint32_t result = ternaryMUL(pack_from_i64(left, 9),
					      pack_from_i64(right, 9));

		CHECK_EQ_I64(unpack_to_i64(result, 9),
			     (int64_t)ref_mul(left, right, 9));
	}
}

static void test_shift(void)
{
	unsigned sample;
	int shift;

	for (sample = 0; sample < 200; sample++) {
		trint32_t word = random_word(32);

		for (shift = -31; shift <= 31; shift++) {
			trint32_t result = ternarySHL(
				word, pack_from_i64(shift, 32));
			trint32_t expected;

			if (shift >= 0) {
				int64_t value = unpack_to_i64(word, 32);
				__int128 scaled = (__int128)value *
						  pow3_i128(shift);

				if (shift >= 32)
					scaled = 0;
				expected = pack_from_i64(
					(int64_t)wrap_balanced(scaled, 32),
					32);
			} else {
				expected = ref_shl(word, shift);
			}
			CHECK_EQ_I64(unpack_to_i64(result, 32),
				     unpack_to_i64(expected, 32));
		}
	}

	CHECK_EQ_I64(unpack_to_i64(
			     ternarySHL(pack_from_i64(1, 32),
					pack_from_i64(32, 32)),
			     32),
		     0);
	CHECK_EQ_I64(unpack_to_i64(
			     ternarySHL(pack_from_i64(1, 32),
					pack_from_i64(-32, 32)),
			     32),
		     0);
}

static void test_rotate(void)
{
	static const unsigned widths[] = { 15, 27 };
	unsigned width_index;

	for (width_index = 0;
	     width_index < sizeof(widths) / sizeof(widths[0]);
	     width_index++) {
		unsigned width = widths[width_index];
		unsigned sample;

		for (sample = 0; sample < 200; sample++) {
			trint32_t first = random_word(width);
			trint32_t second = random_word(width);
			int shift;

			for (shift = -(int)width; shift <= (int)width;
			     shift++) {
				trint32_t result_first = first;
				trint32_t result_second = second;
				trint32_t expected_first;
				trint32_t expected_second;

				ternaryROL(&result_first, &result_second,
					   pack_from_i64(shift, 32), width);
				expected_first = ref_rol(first, second,
							 shift, width);
				expected_second = ref_rol_second(
					first, second, shift, width);

				CHECK_EQ_I64(unpack_to_i64(result_first,
							   width),
					     unpack_to_i64(expected_first,
							   width));
				CHECK_EQ_I64(unpack_to_i64(result_second,
							   width),
					     unpack_to_i64(expected_second,
							   width));
			}
		}
	}
}

void run_test_arithmetic(void)
{
	test_add_exhaustive();
	test_add_random();
	test_adc_exhaustive();
	test_adc_random();
	test_mul_exhaustive();
	test_mul_random();
	test_shift();
	test_rotate();
}

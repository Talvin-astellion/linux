/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef TERNARY_TEST_HELPERS_H
#define TERNARY_TEST_HELPERS_H

#include <stdint.h>

#include "ternary.h"

#define TEST_BITS_PER_TRIT 2
#define TEST_TRIT_N 3
#define TEST_UNDEFINED 2

static inline int packed_get(trint32_t word, unsigned index)
{
	unsigned value = (unsigned)((word >> (index * TEST_BITS_PER_TRIT)) & 3u);

	if (value == TEST_TRIT_N)
		return -1;
	return (int)value;
}

static inline trint32_t packed_set(trint32_t word, unsigned index, int trit)
{
	trint32_t mask = (trint32_t)3u << (index * TEST_BITS_PER_TRIT);
	trint32_t value = (trit < 0) ? TEST_TRIT_N : (trint32_t)trit;

	return (word & ~mask) | ((value & 3u) << (index * TEST_BITS_PER_TRIT));
}

static inline trint32_t pack_from_i64(int64_t value, unsigned width)
{
	trint32_t out = 0;
	int64_t n = value;
	unsigned i;

	for (i = 0; i < width; i++) {
		int64_t r = n % 3;

		n /= 3;
		if (r == 2) {
			r = -1;
			n += 1;
		} else if (r == -2) {
			r = 1;
			n -= 1;
		}
		out = packed_set(out, i, (int)r);
	}
	return out;
}

static inline int64_t unpack_to_i64(trint32_t word, unsigned width)
{
	int64_t total = 0;
	int64_t place = 1;
	unsigned i;

	for (i = 0; i < width; i++) {
		int trit = packed_get(word, i);

		if (trit == TEST_UNDEFINED)
			return INT64_MIN;
		total += (int64_t)trit * place;
		place *= 3;
	}
	return total;
}

static inline __int128 pow3_i128(unsigned exponent)
{
	__int128 value = 1;

	while (exponent--)
		value *= 3;
	return value;
}

static inline __int128 wrap_balanced(__int128 value, unsigned width)
{
	__int128 modulus = pow3_i128(width);
	__int128 half = modulus / 2;

	value %= modulus;
	if (value > half)
		value -= modulus;
	else if (value < -half)
		value += modulus;
	return value;
}

static inline __int128 ref_add(int64_t left, int64_t right, unsigned width)
{
	return wrap_balanced((__int128)left + right, width);
}

static inline __int128 ref_mul(int64_t left, int64_t right, unsigned width)
{
	return wrap_balanced((__int128)left * right, width);
}

static inline trint32_t ref_shl(trint32_t word, int shift)
{
	if (shift >= 32 || shift <= -32)
		return 0;
	if (shift >= 0)
		return (word << (shift * TEST_BITS_PER_TRIT));
	return (trint32_t)(word >> ((-shift) * TEST_BITS_PER_TRIT));
}

static inline trint32_t ref_rol(trint32_t first, trint32_t second,
				int shift, unsigned width)
{
	unsigned word_bits = width * TEST_BITS_PER_TRIT;
	unsigned total_bits = word_bits * 2;
	unsigned rotate_bits;
	unsigned __int128 mask;
	unsigned __int128 pair;
	unsigned __int128 rotated;
	int normalized = shift % (int)width;

	if (normalized < 0)
		normalized += (int)width;
	rotate_bits = (unsigned)normalized * TEST_BITS_PER_TRIT;
	if (total_bits == 128)
		mask = ~(unsigned __int128)0;
	else
		mask = ((unsigned __int128)1 << total_bits) - 1;
	pair = (((unsigned __int128)first << word_bits) |
		(unsigned __int128)second) & mask;
	if (rotate_bits == 0) {
		rotated = pair;
	} else {
		rotated = ((pair << rotate_bits) |
			   (pair >> (total_bits - rotate_bits))) & mask;
	}
	first = (trint32_t)((rotated >> word_bits) & mask);
	second = (trint32_t)(rotated & mask);
	return first;
}

static inline trint32_t ref_rol_second(trint32_t first, trint32_t second,
				       int shift, unsigned width)
{
	unsigned word_bits = width * TEST_BITS_PER_TRIT;
	unsigned total_bits = word_bits * 2;
	unsigned rotate_bits;
	unsigned __int128 mask;
	unsigned __int128 pair;
	unsigned __int128 rotated;
	int normalized = shift % (int)width;

	if (normalized < 0)
		normalized += (int)width;
	rotate_bits = (unsigned)normalized * TEST_BITS_PER_TRIT;
	if (total_bits == 128)
		mask = ~(unsigned __int128)0;
	else
		mask = ((unsigned __int128)1 << total_bits) - 1;
	pair = (((unsigned __int128)first << word_bits) |
		(unsigned __int128)second) & mask;
	if (rotate_bits == 0) {
		rotated = pair;
	} else {
		rotated = ((pair << rotate_bits) |
			   (pair >> (total_bits - rotate_bits))) & mask;
	}
	(void)first;
	return (trint32_t)(rotated & mask);
}

#endif /* TERNARY_TEST_HELPERS_H */

/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include <stdint.h>

#include "test_framework.h"
#include "test_helpers.h"
#include "ternary.h"

static const int trits[] = { -1, 0, 1 };

static uint64_t random_state = 0x243f6a8885a308d3ULL;

static uint64_t next_random(void)
{
	random_state ^= random_state << 13;
	random_state ^= random_state >> 7;
	random_state ^= random_state << 17;
	return random_state;
}

static int unary_expected(const char *name, int trit)
{
	if (name[0] == 'i')
		return (trit == 1) ? -1 : trit + 1;
	if (name[0] == 'd')
		return (trit == -1) ? 1 : trit - 1;
	if (name[0] == 'n')
		return -trit;
	if (name[0] == 'f')
		return (trit < 0) ? 0 : trit;
	return (trit < 0) ? 1 : trit;
}

static void test_unary_gates(void)
{
	static const struct {
		const char *name;
		trint32_t (*fn)(trint32_t);
	} gates[] = {
		{ "inc", ternaryINCB },
		{ "dec", ternaryDECB },
		{ "neg", ternaryNEGB },
		{ "flt", ternaryFLTB },
		{ "abs", ternaryABSB },
	};
	unsigned g;
	unsigned t;
	unsigned pos;

	for (g = 0; g < sizeof(gates) / sizeof(gates[0]); g++) {
		for (t = 0; t < sizeof(trits) / sizeof(trits[0]); t++) {
			for (pos = 0; pos < 32; pos += 31) {
				trint32_t input = packed_set(0, pos, trits[t]);
				trint32_t output = gates[g].fn(input);

				CHECK_EQ_INT(packed_get(output, pos),
					     unary_expected(gates[g].name,
							    trits[t]));
			}
		}
	}
}

static const int gate_min[3][3] = {
	{ -1, -1, -1 },
	{ -1, 0, 0 },
	{ -1, 0, 1 },
};

static const int gate_and[3][3] = {
	{ -1, -1, -1 },
	{ -1, -1, 0 },
	{ -1, 0, 1 },
};

static const int gate_mul[3][3] = {
	{ 1, 0, -1 },
	{ 0, 0, 0 },
	{ -1, 0, 1 },
};

static const int gate_max[3][3] = {
	{ -1, 0, 1 },
	{ 0, 0, 1 },
	{ 1, 1, 1 },
};

static const int gate_or[3][3] = {
	{ -1, 0, 1 },
	{ 0, 1, 1 },
	{ 1, 1, 1 },
};

static const int gate_add[3][3] = {
	{ 1, -1, 0 },
	{ -1, 0, 1 },
	{ 0, 1, -1 },
};

static const int gate_unm[3][3] = {
	{ -1, 0, 0 },
	{ 0, 0, 0 },
	{ 0, 0, 1 },
};

static const int gate_imp[3][3] = {
	{ 1, 1, 1 },
	{ 0, 1, 1 },
	{ -1, 0, 1 },
};

static const int gate_set[3][3] = {
	{ -1, -1, 1 },
	{ -1, 0, 1 },
	{ -1, 1, 1 },
};

static void test_binary_gates(void)
{
	static const struct {
		const char *name;
		trint32_t (*fn)(trint32_t, trint32_t);
		const int (*table)[3];
	} gates[] = {
		{ "min", ternaryMINB, gate_min },
		{ "and", ternaryANDB, gate_and },
		{ "mul", ternaryMULB, gate_mul },
		{ "max", ternaryMAXB, gate_max },
		{ "orr", ternaryORRB, gate_or },
		{ "add", ternaryADDB, gate_add },
		{ "unm", ternaryUNMB, gate_unm },
		{ "imp", ternaryIMPB, gate_imp },
		{ "set", ternarySETB, gate_set },
	};
	unsigned g;
	unsigned a;
	unsigned b;

	for (g = 0; g < sizeof(gates) / sizeof(gates[0]); g++) {
		for (a = 0; a < 3; a++) {
			for (b = 0; b < 3; b++) {
				trint32_t left = packed_set(0, 0, trits[a]);
				trint32_t right = packed_set(0, 0, trits[b]);
				trint32_t output =
					gates[g].fn(left, right);

				CHECK_EQ_INT(packed_get(output, 0),
					     gates[g].table[a][b]);
			}
		}
	}
}

static void test_conversions(void)
{
	CHECK_EQ_INT(trit2int(TEST_TRIT_N), -1);
	CHECK_EQ_INT(trit2int(0), 0);
	CHECK_EQ_INT(trit2int(1), 1);
	CHECK_EQ_INT(int2trit(-1), TEST_TRIT_N);
	CHECK_EQ_INT(int2trit(0), 0);
	CHECK_EQ_INT(int2trit(1), 1);
	CHECK_EQ_INT(char2trit('-'), TEST_TRIT_N);
	CHECK_EQ_INT(char2trit('0'), 0);
	CHECK_EQ_INT(char2trit('+'), 1);
	CHECK_EQ_INT(char2trit('?'), 2);
}

static void test_sign(void)
{
	unsigned sample;

	for (sample = 0; sample < 200; sample++) {
		trint32_t word = 0;
		unsigned i;
		int expected = 0;

		for (i = 0; i < 32; i++) {
			int trit = (int)(next_random() % 3) - 1;

			word = packed_set(word, i, trit);
			if (trit)
				expected = trit;
		}
		CHECK_EQ_INT(trit2int(ternarySGN(word)), expected);
	}
	CHECK_EQ_INT(trit2int(ternarySGN(0)), 0);
}

static void test_ternary2int(void)
{
	static const int64_t values[] = {
		0, 1, -1, 2, -2, 3, -3, 42, -42, 123456, -123456,
		1000000000LL, -1000000000LL,
	};
	unsigned i;

	for (i = 0; i < sizeof(values) / sizeof(values[0]); i++)
		CHECK_EQ_I64(ternary2int(pack_from_i64(values[i], 32)),
			     values[i]);

	CHECK_EQ_I64(ternary2int(pack_from_i64((int64_t)pow3_i128(30), 32)),
		     (int64_t)pow3_i128(30));
	CHECK_EQ_I64(ternary2int(pack_from_i64(-(int64_t)pow3_i128(30), 32)),
		     -(int64_t)pow3_i128(30));
}

void run_test_trit(void)
{
	test_conversions();
	test_unary_gates();
	test_binary_gates();
	test_sign();
	test_ternary2int();
}

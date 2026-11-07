/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#include "ternary.h"
#include "test_helpers.h"

#define CORPUS_SIZE 4096

static trint32_t corpus[CORPUS_SIZE];
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

static double seconds_now(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
	return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
}

static void report(const char *name, unsigned iterations, double elapsed,
		   unsigned long long checksum)
{
	printf("BENCH %s %.3f %llu\n", name,
	       (double)iterations / elapsed, checksum);
}

static void bench_binop(const char *name,
			trint32_t (*operation)(trint32_t, trint32_t),
			unsigned iterations)
{
	volatile trint32_t sink = 0;
	unsigned i;
	double start;
	double elapsed;

	for (i = 0; i < 10000; i++)
		sink ^= operation(corpus[i % CORPUS_SIZE],
				  corpus[(i * 7 + 13) % CORPUS_SIZE]);

	start = seconds_now();
	for (i = 0; i < iterations; i++)
		sink ^= operation(corpus[i % CORPUS_SIZE],
				  corpus[(i * 7 + 13) % CORPUS_SIZE]);
	elapsed = seconds_now() - start;
	report(name, iterations, elapsed, (unsigned long long)sink);
}

static void bench_adc(unsigned iterations)
{
	volatile trint32_t sink = 0;
	unsigned i;
	double start;
	double elapsed;

	for (i = 0; i < 10000; i++) {
		trit_t carry = (i & 1) ? 1 : TEST_TRIT_N;

		sink ^= ternaryADC(corpus[i % CORPUS_SIZE],
				   corpus[(i * 7 + 13) % CORPUS_SIZE],
				   &carry);
	}

	start = seconds_now();
	for (i = 0; i < iterations; i++) {
		trit_t carry = (i & 1) ? 1 : TEST_TRIT_N;

		sink ^= ternaryADC(corpus[i % CORPUS_SIZE],
				   corpus[(i * 7 + 13) % CORPUS_SIZE],
				   &carry);
	}
	elapsed = seconds_now() - start;
	report("ADC", iterations, elapsed, (unsigned long long)sink);
}

static void bench_shift(unsigned iterations)
{
	volatile trint32_t sink = 0;
	trint32_t shift_left = pack_from_i64(1, 32);
	trint32_t shift_right = pack_from_i64(-1, 32);
	unsigned i;
	double start;
	double elapsed;

	for (i = 0; i < 10000; i++) {
		sink ^= ternarySHL(corpus[i % CORPUS_SIZE], shift_left);
		sink ^= ternarySHL(corpus[i % CORPUS_SIZE], shift_right);
	}

	start = seconds_now();
	for (i = 0; i < iterations; i++) {
		sink ^= ternarySHL(corpus[i % CORPUS_SIZE], shift_left);
		sink ^= ternarySHL(corpus[i % CORPUS_SIZE], shift_right);
	}
	elapsed = seconds_now() - start;
	report("SHL", iterations * 2, elapsed,
	       (unsigned long long)sink);
}

int main(void)
{
	unsigned i;

	for (i = 0; i < CORPUS_SIZE; i++)
		corpus[i] = random_word(32);

	bench_binop("ADD", ternaryADD, 200000);
	bench_adc(200000);
	bench_binop("MUL", ternaryMUL, 50000);
	bench_binop("ANDB", ternaryANDB, 500000);
	bench_binop("ADDB", ternaryADDB, 500000);
	bench_shift(500000);
	return 0;
}

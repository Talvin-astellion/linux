/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef TERNARY_TEST_FRAMEWORK_H
#define TERNARY_TEST_FRAMEWORK_H

#include <stdint.h>
#include <stdio.h>

extern unsigned long test_checks;
extern unsigned long test_failures;

#define CHECK_TRUE(cond)                                                       \
	do {                                                                   \
		test_checks++;                                                 \
		if (!(cond)) {                                                 \
			test_failures++;                                       \
			printf("not ok %lu - %s:%d: %s\n", test_checks,        \
			       __FILE__, __LINE__, #cond);                     \
		} else {                                                       \
			printf("ok %lu - %s:%d: %s\n", test_checks,            \
			       __FILE__, __LINE__, #cond);                     \
		}                                                              \
	} while (0)

#define CHECK_EQ_I64(actual, expected)                                         \
	do {                                                                   \
		long long a_ = (long long)(actual);                            \
		long long e_ = (long long)(expected);                          \
		test_checks++;                                                 \
		if (a_ != e_) {                                                \
			test_failures++;                                       \
			printf("not ok %lu - %s:%d: expected %lld, got %lld\n", \
			       test_checks, __FILE__, __LINE__, e_, a_);       \
		} else {                                                       \
			printf("ok %lu - %s:%d\n", test_checks, __FILE__,       \
			       __LINE__);                                      \
		}                                                              \
	} while (0)

#define CHECK_EQ_U64(actual, expected)                                         \
	do {                                                                   \
		unsigned long long a_ = (unsigned long long)(actual);          \
		unsigned long long e_ = (unsigned long long)(expected);        \
		test_checks++;                                                 \
		if (a_ != e_) {                                                \
			test_failures++;                                       \
			printf("not ok %lu - %s:%d: expected %llu, got %llu\n", \
			       test_checks, __FILE__, __LINE__, e_, a_);       \
		} else {                                                       \
			printf("ok %lu - %s:%d\n", test_checks, __FILE__,       \
			       __LINE__);                                      \
		}                                                              \
	} while (0)

#define CHECK_EQ_INT(actual, expected) CHECK_EQ_I64(actual, expected)

#endif /* TERNARY_TEST_FRAMEWORK_H */

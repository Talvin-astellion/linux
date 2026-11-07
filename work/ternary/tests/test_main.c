/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include <stdio.h>

#include "test_framework.h"

unsigned long test_checks;
unsigned long test_failures;

void run_test_trit(void);
void run_test_arithmetic(void);
void run_test_disasm(void);

int main(void)
{
	run_test_trit();
	run_test_arithmetic();
	run_test_disasm();

	printf("RESULT checks=%lu failures=%lu\n", test_checks,
	       test_failures);
	return test_failures ? 1 : 0;
}

/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include <stdint.h>

#include "disassem.h"
#include "test_framework.h"

static TriWord make_instruction(TriOpcode opcode, TriReg r1,
				TriReg r2, OpModifiers r2mod,
				TriReg r3, OpModifiers r3mod,
				TriWord immediate)
{
	TriWord instruction = 0;

	instruction |= (TriWord)(opcode & 0x0fff);
	instruction |= (TriWord)(r1 & 0x3f) << 12;
	instruction |= (TriWord)(r2mod & 0xff) << 18;
	instruction |= (TriWord)(r2 & 0x3f) << 26;
	instruction |= (TriWord)(r3mod & 0xff) << 32;
	instruction |= (TriWord)(r3 & 0x3f) << 40;
	instruction |= (TriWord)(immediate & 0x003fffff) << 32;
	return instruction;
}

static void test_nop(void)
{
	TriOpInstruction fields;
	TriWord instruction = make_instruction(OPCODE_NOP, REG_ZERO,
					       REG_ZERO, OPMOD_NOP,
					       REG_ZERO, OPMOD_NOP, 0);

	parseInstruction(&fields, instruction);
	CHECK_EQ_INT(fields.opcode, OPCODE_NOP);
	CHECK_EQ_INT(fields.r1, REG_ZERO);
	CHECK_EQ_INT(fields.r2, REG_ZERO);
	CHECK_EQ_INT(fields.r3, REG_ZERO);
	CHECK_EQ_INT(fields.imm11, 0);
}

static void test_two_register(void)
{
	TriOpInstruction fields;
	TriWord instruction = make_instruction(OPCODE_ADDI, REG_P01,
					       REG_P02, OPMOD_NOP,
					       REG_ZERO, OPMOD_NOP, 0);

	parseInstruction(&fields, instruction);
	CHECK_EQ_INT(fields.opcode, OPCODE_ADDI);
	CHECK_EQ_INT(fields.r1, REG_P01);
	CHECK_EQ_INT(fields.r2, REG_P02);
	CHECK_EQ_INT(fields.r2mod, OPMOD_NOP);
	CHECK_EQ_INT(fields.imm11, 0);
}

static void test_three_register(void)
{
	TriOpInstruction fields;
	TriWord instruction = make_instruction(OPCODE_TADD, REG_P03,
					       REG_P04, OPMOD_NEG,
					       REG_P05, OPMOD_ABS, 0);

	parseInstruction(&fields, instruction);
	CHECK_EQ_INT(fields.opcode, OPCODE_TADD);
	CHECK_EQ_INT(fields.r1, REG_P03);
	CHECK_EQ_INT(fields.r2, REG_P04);
	CHECK_EQ_INT(fields.r2mod, OPMOD_NEG);
	CHECK_EQ_INT(fields.r3, REG_P05);
	CHECK_EQ_INT(fields.r3mod, OPMOD_ABS);
}

static void test_immediate_expansion(void)
{
	TriOpInstruction fields;
	TriWord all_positive = 0x15555555555555ULL;
	TriWord instruction = make_instruction(OPCODE_ADDI, REG_P01,
					       REG_P02, OPMOD_NOP,
					       REG_ZERO, (OpModifiers)0,
					       0b00010101);

	parseInstruction(&fields, instruction);
	CHECK_EQ_U64(fields.imm11, all_positive);
}

void run_test_disasm(void)
{
	test_nop();
	test_two_register();
	test_three_register();
	test_immediate_expansion();
}

#include <assert.h>

#define main rv32_unused_main
#include "../rv32.c"
#undef main

#define TEST_PC 0x80010000u
#define VEC     0x80020000u
#define ADDR1   0x80400000u
#define ADDR2   0x80401000u
#define TARGET  0x80030000u

static void put16(uint32_t pa, uint16_t val)
{
    ram[pa - RAM_BASE] = (uint8_t)val;
    ram[pa - RAM_BASE + 1] = (uint8_t)(val >> 8);
}

static void put32(uint32_t pa, uint32_t val)
{
    unsigned i;

    for (i = 0; i < 4; i++)
        ram[pa - RAM_BASE + i] = (uint8_t)(val >> (8 * i));
}

static uint32_t get32(uint32_t pa)
{
    return (uint32_t)ram[pa - RAM_BASE] |
           ((uint32_t)ram[pa - RAM_BASE + 1] << 8) |
           ((uint32_t)ram[pa - RAM_BASE + 2] << 16) |
           ((uint32_t)ram[pa - RAM_BASE + 3] << 24);
}

static void reset_cpu(void)
{
    unsigned i;

    for (i = 0; i < 32; i++)
        regs[i] = 0;
    satp = 0;
    priv = 1;
    mstatus = 0;
    stvec = VEC;
    pc = 0;
    scause = 0;
    stval = 0;
    sepc = 0;
    mem_fault = 0;
    mmu_fault_cause = 0;
    pmp_reset();
    put32(ADDR1, 0x11223344u);
    put32(ADDR2, 0x55667788u);
}

static void run_c(uint16_t c)
{
    uint32_t inst;

    assert(rv32c_expand(c, &inst));
    pc = TEST_PC + 2;
    execute(inst, TEST_PC);
}

int main(void)
{
    uint32_t inst;
    unsigned len;

    ram = calloc(1, RAM_SIZE);
    assert(ram != NULL);

    reset_cpu();
    regs[2] = 0x1000;
    run_c(0x0040);
    assert(scause == 0);
    assert(regs[8] == 0x1004);

    reset_cpu();
    regs[8] = ADDR1;
    put32(ADDR1 + 4, 0x12345678u);
    run_c(0x4044);
    assert(scause == 0);
    assert(regs[9] == 0x12345678u);

    reset_cpu();
    regs[8] = ADDR1;
    regs[9] = 0xdeadbeefu;
    run_c(0xc044);
    assert(scause == 0);
    assert(get32(ADDR1 + 4) == 0xdeadbeefu);

    reset_cpu();
    regs[10] = 5;
    run_c(0x157d);
    assert(scause == 0);
    assert(regs[10] == 4);

    reset_cpu();
    run_c(0x557d);
    assert(scause == 0);
    assert(regs[10] == 0xffffffffu);

    reset_cpu();
    run_c(0x7501);
    assert(scause == 0);
    assert(regs[10] == 0xfffe0000u);

    reset_cpu();
    regs[2] = 0x1000;
    run_c(0x6141);
    assert(scause == 0);
    assert(regs[2] == 0x1010);

    reset_cpu();
    regs[10] = 1;
    run_c(0x0506);
    assert(scause == 0);
    assert(regs[10] == 2);

    reset_cpu();
    regs[8] = 4;
    run_c(0x8005);
    assert(scause == 0);
    assert(regs[8] == 2);

    reset_cpu();
    regs[8] = 0x80000000u;
    run_c(0x8405);
    assert(scause == 0);
    assert(regs[8] == 0xc0000000u);

    reset_cpu();
    regs[8] = 0x12345678u;
    run_c(0x883d);
    assert(scause == 0);
    assert(regs[8] == 0x8);

    reset_cpu();
    regs[8] = 10;
    regs[9] = 3;
    run_c(0x8c05);
    assert(scause == 0);
    assert(regs[8] == 7);

    reset_cpu();
    regs[8] = 0xf0;
    regs[9] = 0x0f;
    run_c(0x8c25);
    assert(scause == 0);
    assert(regs[8] == 0xff);

    reset_cpu();
    regs[8] = 0xf0;
    regs[9] = 0x0f;
    run_c(0x8c45);
    assert(scause == 0);
    assert(regs[8] == 0xff);

    reset_cpu();
    regs[8] = 0xff;
    regs[9] = 0x0f;
    run_c(0x8c65);
    assert(scause == 0);
    assert(regs[8] == 0x0f);

    reset_cpu();
    run_c(0xa009);
    assert(scause == 0);
    assert(pc == TEST_PC + 2);

    reset_cpu();
    run_c(0x2009);
    assert(scause == 0);
    assert(regs[1] == TEST_PC + 2);
    assert(pc == TEST_PC + 2);

    reset_cpu();
    regs[8] = 0;
    run_c(0xc009);
    assert(scause == 0);
    assert(pc == TEST_PC + 2);

    reset_cpu();
    regs[8] = 1;
    run_c(0xc009);
    assert(scause == 0);
    assert(pc == TEST_PC + 2);

    reset_cpu();
    regs[8] = 1;
    run_c(0xe009);
    assert(scause == 0);
    assert(pc == TEST_PC + 2);

    reset_cpu();
    regs[2] = ADDR1;
    put32(ADDR1 + 4, 0x12345678u);
    run_c(0x4512);
    assert(scause == 0);
    assert(regs[10] == 0x12345678u);

    reset_cpu();
    regs[2] = ADDR1;
    regs[10] = 0xdeadbeefu;
    run_c(0xc22a);
    assert(scause == 0);
    assert(get32(ADDR1 + 4) == 0xdeadbeefu);

    reset_cpu();
    regs[10] = TARGET;
    run_c(0x8502);
    assert(scause == 0);
    assert(pc == TARGET);

    reset_cpu();
    regs[11] = 42;
    run_c(0x852e);
    assert(scause == 0);
    assert(regs[10] == 42);

    reset_cpu();
    regs[10] = TARGET;
    run_c(0x9502);
    assert(scause == 0);
    assert(regs[1] == TEST_PC + 2);
    assert(pc == TARGET);

    reset_cpu();
    regs[10] = 5;
    regs[11] = 3;
    run_c(0x952e);
    assert(scause == 0);
    assert(regs[10] == 8);

    reset_cpu();
    run_c(0x9002);
    assert(scause == CAUSE_BREAK);

    reset_cpu();
    run_c(0x0001);
    assert(scause == 0);

    assert(!rv32c_expand(0x0000, &inst));
    assert(!rv32c_expand(0x4002, &inst));
    assert(!rv32c_expand(0x8002, &inst));
    assert(!rv32c_expand(0x1002, &inst));
    assert(!rv32c_expand(0x9c25, &inst));
    assert(!rv32c_expand(0x0013, &inst));

    reset_cpu();
    pc = ADDR1;
    put16(ADDR1, 0x0506);
    assert(fetch_insn(&inst, &len));
    assert(len == 2);
    assert(inst == 0x00151513u);

    reset_cpu();
    pc = ADDR1;
    put32(ADDR1, 0x00151513u);
    assert(fetch_insn(&inst, &len));
    assert(len == 4);
    assert(inst == 0x00151513u);

    reset_cpu();
    pc = ADDR1;
    put16(ADDR1, 0x0000);
    assert(!fetch_insn(&inst, &len));
    assert(scause == CAUSE_ILLEGAL);
    assert(stval == 0);
    assert(pc == VEC);

    reset_cpu();
    pc = RAM_BASE + RAM_SIZE - 2;
    put16(pc, 0x0001);
    assert(fetch_insn(&inst, &len));
    assert(len == 2);

    reset_cpu();
    pc = RAM_BASE + RAM_SIZE - 2;
    put16(pc, 0x0013);
    assert(!fetch_insn(&inst, &len));
    assert(scause == CAUSE_INST_PF);
    assert(pc == VEC);

    free(ram);
    return 0;
}

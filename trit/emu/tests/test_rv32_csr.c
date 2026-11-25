#include <assert.h>

#define main rv32_unused_main
#include "../rv32.c"
#undef main

#define TEST_PC  0x80010000u
#define TEST_VEC 0x80020000u

static void reset_cpu(int mode)
{
    priv = mode;
    stvec = TEST_VEC;
    pc = 0;
    scause = 0;
    stval = 0;
    sepc = 0;
    sstatus = 0;
    mstatus = 0;
    scounteren = 0;
    mcounteren = 0;
    medeleg = MEDELEG_MASK;
    mideleg = MIDELEG_MASK;
    sie = sip = 0;
    mie = mip = 0;
    regs[1] = 0;
    regs[10] = 0;
    mem_fault = 0;
    mmu_fault_cause = 0;
    pmp_reset();
}

static uint32_t csr_op(int funct3, uint32_t csr, unsigned rs1, unsigned rd)
{
    return (csr << 20) | (rs1 << 15) | ((uint32_t)funct3 << 12) |
           (rd << 7) | 0x73u;
}

static void expect_illegal(uint32_t inst)
{
    execute(inst, TEST_PC);
    assert(scause == CAUSE_ILLEGAL);
    assert(stval == inst);
    assert(sepc == TEST_PC);
    assert(pc == TEST_VEC);
    assert(priv == 1);
}

int main(void)
{
    uint32_t inst;

    ram = calloc(1, RAM_SIZE);
    assert(ram != NULL);

    reset_cpu(0);
    inst = csr_op(1, 0x100, 1, 0);
    regs[1] = MSTATUS_SUM;
    expect_illegal(inst);
    assert(sstatus == 0);
    assert(mstatus == 0);

    reset_cpu(3);
    inst = csr_op(2, 0x301, 0, 10);
    execute(inst, TEST_PC);
    assert(scause == 0);
    assert(regs[10] == 0x4000112du);

    reset_cpu(1);
    inst = csr_op(1, 0x301, 1, 0);
    expect_illegal(inst);

    reset_cpu(1);
    inst = csr_op(1, 0x700, 1, 0);
    expect_illegal(inst);

    reset_cpu(0);
    expect_illegal(csr_op(2, 0xc00, 0, 10));

    reset_cpu(1);
    regs[1] = 7;
    inst = csr_op(1, 0x106, 1, 0);
    execute(inst, TEST_PC);
    assert(scause == 0);
    assert(scounteren == 7);

    reset_cpu(0);
    scounteren = 1;
    inst = csr_op(2, 0xc00, 0, 10);
    execute(inst, TEST_PC);
    assert(scause == 0);
    assert(regs[10] == (uint32_t)cycle);
    assert(pc == 0);

    reset_cpu(0);
    expect_illegal(csr_op(2, 0xc80, 0, 10));

    reset_cpu(3);
    regs[1] = 1;
    inst = csr_op(1, 0x306, 1, 0);
    execute(inst, TEST_PC);
    assert(scause == 0);
    assert(mcounteren == 1);

    reset_cpu(0);
    mcounteren = 1;
    inst = csr_op(2, 0xc80, 0, 10);
    execute(inst, TEST_PC);
    assert(scause == 0);
    assert(regs[10] == (uint32_t)(cycle >> 32));

    reset_cpu(1);
    expect_illegal(csr_op(1, 0x306, 1, 0));

    reset_cpu(3);
    regs[1] = 0xffffffffu;
    inst = csr_op(1, 0x302, 1, 0);
    execute(inst, TEST_PC);
    assert(scause == 0);
    assert(medeleg == MEDELEG_MASK);
    regs[1] = 0xffffffffu;
    inst = csr_op(1, 0x303, 1, 0);
    execute(inst, TEST_PC);
    assert(scause == 0);
    assert(mideleg == MIDELEG_MASK);

    reset_cpu(1);
    expect_illegal(csr_op(1, 0x302, 1, 0));

    reset_cpu(0);
    fcsr = 0;
    regs[1] = 0x1f;
    inst = csr_op(1, 0x003, 1, 0);
    execute(inst, TEST_PC);
    assert(scause == 0);
    assert(fcsr == 0x1f);
    assert(pc == 0);

    reset_cpu(0);
    regs[1] = 1;
    expect_illegal(csr_op(1, 0xc00, 1, 0));

    reset_cpu(0);
    inst = csr_op(1, 0x180, 1, 0);
    expect_illegal(inst);
    assert(satp == 0);

    reset_cpu(1);
    inst = csr_op(1, 0x100, 1, 0);
    regs[1] = MSTATUS_SUM | MSTATUS_MXR | MSTATUS_SPP | (1u << 3) | (1u << 11);
    execute(inst, TEST_PC);
    assert(scause == 0);
    assert(mstatus == (MSTATUS_SUM | MSTATUS_MXR | MSTATUS_SPP));
    assert(sstatus == (MSTATUS_SUM | MSTATUS_MXR | MSTATUS_SPP));

    reset_cpu(3);
    inst = csr_op(1, 0x300, 1, 0);
    regs[1] = MSTATUS_MIE | MSTATUS_MPIE | (1u << 11) | MSTATUS_SUM;
    execute(inst, TEST_PC);
    assert(scause == 0);
    assert(mstatus == (MSTATUS_MIE | MSTATUS_MPIE | (1u << 11) |
                       MSTATUS_SUM));
    assert(sstatus == MSTATUS_SUM);

    reset_cpu(0);
    execute(0x10200073u, TEST_PC);
    assert(scause == CAUSE_ILLEGAL);
    assert(stval == 0x10200073u);
    assert(pc == TEST_VEC);
    assert(priv == 1);

    reset_cpu(1);
    execute(0x30200073u, TEST_PC);
    assert(scause == CAUSE_ILLEGAL);
    assert(stval == 0x30200073u);
    assert(pc == TEST_VEC);

    free(ram);
    return 0;
}

#include <assert.h>
#include <sys/wait.h>

#define main rv32_unused_main
#include "../rv32.c"
#undef main

#define OLD_PC  0x80030000u
#define VEC     0x80020000u
#define M_VEC   0x80040000u

static void reset_irq(void)
{
    priv = 1;
    pc = OLD_PC;
    sepc = 0;
    scause = 0;
    stval = 0;
    stvec = VEC;
    sstatus = MSTATUS_SIE;
    mstatus = MSTATUS_SIE;
    sie = 0;
    sip = 0;
    mie = 0;
    mip = 0;
    medeleg = MEDELEG_MASK;
    mideleg = MIDELEG_MASK;
    timer_deadline = UINT64_MAX;
    instret = 0;
    guest_time = 0;
    time_inst_mode = 1;
    sleep_wfi = 1;
    debug_sbi = 0;
    pmp_reset();
}

static void reset_mret(void)
{
    reset_irq();
    priv = 3;
    mepc = OLD_PC;
    mcause = 0;
    mtvec = VEC;
    mstatus = 0;
    sstatus = 0;
}

static void reset_mtrap(void)
{
    reset_irq();
    priv = 3;
    mtvec = M_VEC;
    mepc = 0;
    mcause = 0;
    mtval = 0;
    mstatus = MSTATUS_MIE | MSTATUS_MPP;
    sstatus = 0;
}

static void reset_timer(void)
{
    reset_irq();
    satp = 0;
    time_inst_mode = 1;
    instret = 100;
    guest_time = 100;
    timer_deadline = UINT64_MAX;
    mtimecmp = UINT64_MAX;
    sip = 0;
    mip = 0;
    mem_fault = 0;
}

static void expect_irq(uint32_t cause, uint32_t vec)
{
    assert(maybe_interrupt() == 1);
    assert(scause == (cause | 0x80000000u));
    assert(stval == 0);
    assert(sepc == OLD_PC);
    assert(pc == vec);
    assert(priv == 1);
    assert((sstatus & MSTATUS_SIE) == 0);
    assert((sstatus & MSTATUS_SPIE) != 0);
    assert((sstatus & MSTATUS_SPP) != 0);
}

int main(void)
{
    ram = calloc(1, RAM_SIZE);
    assert(ram != NULL);

    reset_irq();
    sie = SIE_STIE;
    sip = SIP_STIP;
    expect_irq(5, VEC);
    assert((sip & SIP_STIP) != 0);

    reset_irq();
    csr_write(0x105, VEC | 1u);
    assert(stvec == (VEC | 1u));
    sie = SIE_STIE;
    sip = SIP_STIP;
    expect_irq(5, VEC + 4u * 5u);

    reset_irq();
    csr_write(0x105, VEC | 1u);
    execute(0xffffffffu, OLD_PC);
    assert(scause == CAUSE_ILLEGAL);
    assert(stval == 0xffffffffu);
    assert(pc == VEC);

    reset_irq();
    sie = SIE_STIE | SIE_SSIE;
    sip = SIP_STIP | SIP_SSIP;
    expect_irq(1, VEC);
    assert((sip & SIP_SSIP) == 0);
    assert((sip & SIP_STIP) != 0);

    reset_irq();
    sie = SIE_SSIE;
    sip = SIP_SSIP;
    expect_irq(1, VEC);
    assert(sip == 0);

    reset_irq();
    sie = SIE_SEIE;
    sip = SIP_SEIP;
    expect_irq(9, VEC);
    assert(sip == 0);

    reset_irq();
    sip = SIP_STIP;
    assert(maybe_interrupt() == 0);
    assert(pc == OLD_PC);
    assert(scause == 0);

    reset_irq();
    sstatus = 0;
    mstatus = 0;
    sie = SIE_STIE;
    sip = SIP_STIP;
    assert(maybe_interrupt() == 0);

    reset_irq();
    sie = SIE_SSIE;
    sip = SIP_SSIP;
    assert(interrupt_pending());
    sstatus &= ~MSTATUS_SIE;
    assert(!interrupt_pending());

    reset_irq();
    instret = 64;
    sie = SIE_STIE;
    timer_deadline = 1;
    assert(interrupt_pending());
    assert((sip & SIP_STIP) != 0);
    assert(timer_deadline == 1u);
    update_time();
    update_timer_irq();
    assert((sip & SIP_STIP) != 0);
    assert(timer_deadline == 1u);

    reset_irq();
    stvec = VEC;
    do_trap(CAUSE_ILLEGAL, 0xffffffffu, 0, OLD_PC);
    assert((sstatus & MSTATUS_SPP) != 0);
    execute(0x10200073u, OLD_PC + 4u);
    assert(pc == OLD_PC);
    assert(priv == 1);
    assert((sstatus & MSTATUS_SIE) != 0);
    assert((sstatus & MSTATUS_SPP) == 0);

    reset_irq();
    csr_write(0x141, OLD_PC | 2u);
    assert(csr_read(0x141) == (OLD_PC | 2u));
    execute(0x10200073u, VEC);
    assert(pc == (OLD_PC | 2u));

    reset_mret();
    csr_write(0x341, OLD_PC | 2u);
    assert(csr_read(0x341) == (OLD_PC | 2u));
    execute(0x30200073u, VEC);
    assert(pc == (OLD_PC | 2u));

    reset_mret();
    mstatus = MSTATUS_MPIE;
    execute(0x30200073u, OLD_PC + 4u);
    assert(pc == OLD_PC);
    assert(priv == 0);
    assert((mstatus & MSTATUS_MIE) != 0);
    assert((mstatus & MSTATUS_MPIE) != 0);
    assert((mstatus & MSTATUS_MPP) == 0);

    reset_mret();
    mstatus = 1u << 11;
    execute(0x30200073u, OLD_PC + 4u);
    assert(pc == OLD_PC);
    assert(priv == 1);
    assert((mstatus & MSTATUS_MIE) == 0);
    assert((mstatus & MSTATUS_MPIE) != 0);
    assert((mstatus & MSTATUS_MPP) == 0);

    reset_mret();
    mstatus = MSTATUS_MPP | MSTATUS_MPIE;
    execute(0x30200073u, OLD_PC + 4u);
    assert(pc == OLD_PC);
    assert(priv == 3);
    assert((mstatus & MSTATUS_MIE) != 0);
    assert((mstatus & MSTATUS_MPIE) != 0);
    assert((mstatus & MSTATUS_MPP) == 0);

    reset_mtrap();
    execute(0xffffffffu, OLD_PC);
    assert(mcause == CAUSE_ILLEGAL);
    assert(mtval == 0xffffffffu);
    assert(mepc == OLD_PC);
    assert(pc == M_VEC);
    assert(priv == 3);
    assert((mstatus & MSTATUS_MIE) == 0);
    assert((mstatus & MSTATUS_MPIE) != 0);
    assert((mstatus & MSTATUS_MPP) == MSTATUS_MPP);

    execute(0x30200073u, M_VEC);
    assert(pc == OLD_PC);
    assert(priv == 3);
    assert((mstatus & MSTATUS_MIE) != 0);
    assert((mstatus & MSTATUS_MPIE) != 0);
    assert((mstatus & MSTATUS_MPP) == 0);

    reset_mtrap();
    mie = SIE_STIE;
    mip = SIP_STIP;
    assert(!interrupt_pending());
    assert(!maybe_interrupt());
    assert(mcause == 0);
    assert(pc == OLD_PC);
    assert(priv == 3);

    reset_mtrap();
    mideleg = 0;
    csr_write(0x304, MIE_MTIE);
    csr_write(0x344, MIP_MTIP);
    assert(csr_read(0x304) == MIE_MTIE);
    assert((mip & MIP_MTIP) != 0);
    assert(maybe_interrupt() == 1);
    assert(mcause == (CAUSE_M_TIMER | 0x80000000u));
    assert(mtval == 0);
    assert(mepc == OLD_PC);
    assert(pc == M_VEC);
    assert(priv == 3);
    assert((mip & MIP_MTIP) != 0);
    assert((mstatus & MSTATUS_MIE) == 0);
    assert((mstatus & MSTATUS_MPIE) != 0);
    assert((mstatus & MSTATUS_MPP) == MSTATUS_MPP);

    reset_mtrap();
    mideleg = 0;
    csr_write(0x304, MIE_MEIE | MIE_MTIE | MIE_MSIE);
    mip = MIP_MEIP | MIP_MTIP | MIP_MSIP;
    assert(maybe_interrupt() == 1);
    assert(mcause == (CAUSE_M_EXTERNAL | 0x80000000u));
    assert((mip & MIP_MEIP) == 0);
    assert((mip & (MIP_MTIP | MIP_MSIP)) != 0);

    reset_mtrap();
    mideleg = 0;
    csr_write(0x304, MIE_MSIE | MIE_MTIE);
    mip = MIP_MSIP | MIP_MTIP;
    assert(maybe_interrupt() == 1);
    assert(mcause == (CAUSE_M_SOFTWARE | 0x80000000u));
    assert(mip == MIP_MTIP);
    assert((mip & MIP_MSIP) == 0);

    reset_mtrap();
    mideleg = 0;
    csr_write(0x304, MIE_MSIE);
    mip = MIP_MSIP;
    assert(maybe_interrupt() == 1);
    assert(mcause == (CAUSE_M_SOFTWARE | 0x80000000u));
    assert(mip == 0);

    reset_irq();
    mtvec = M_VEC;
    medeleg = 0;
    execute(0xffffffffu, OLD_PC);
    assert(scause == 0);
    assert(mcause == CAUSE_ILLEGAL);
    assert(mepc == OLD_PC);
    assert(pc == M_VEC);
    assert(priv == 3);

    reset_irq();
    mtvec = M_VEC;
    mideleg = 0;
    mstatus |= MSTATUS_MIE;
    mie = SIE_STIE;
    mip = SIP_STIP;
    assert(maybe_interrupt() == 1);
    assert(scause == 0);
    assert(mcause == (CAUSE_TIMER | 0x80000000u));
    assert(mepc == OLD_PC);
    assert(pc == M_VEC);
    assert(priv == 3);

    reset_timer();
    sie = SIE_STIE;
    mem_store32(CLINT_MTIMECMP, 0x100u);
    mem_store32(CLINT_MTIMECMP + 4u, 0);
    assert(!mem_fault);
    assert(mtimecmp == 0x100u);
    assert(timer_deadline == 0x100u);
    assert(mem_load32(CLINT_MTIME) == 100u);
    assert(mem_load32(CLINT_MTIME + 4u) == 0);
    assert(!mem_fault);
    instret = 0x100u;
    assert(maybe_interrupt() == 1);
    assert(scause == (CAUSE_TIMER | 0x80000000u));
    assert(sepc == OLD_PC);
    assert(pc == VEC);

    reset_timer();
    regs[17] = SBI_EXT_TIME;
    regs[16] = 0;
    regs[10] = 0x200u;
    regs[11] = 0;
    sbi_call();
    assert(timer_deadline == 0x200u);
    assert(mtimecmp == 0x200u);

    reset_irq();
    {
        int p[2];
        int saved = dup(STDIN_FILENO);

        assert(saved >= 0);
        assert(pipe(p) == 0);
        assert(write(p[1], "A", 1) == 1);
        close(p[1]);
        assert(dup2(p[0], STDIN_FILENO) == STDIN_FILENO);
        regs[17] = 2;
        regs[16] = 0;
        regs[10] = 0xdeadbeefu;
        regs[11] = 0xfeedfaceu;
        sbi_call();
        assert((int32_t)regs[10] == 'A');
        assert(regs[11] == 0);
        regs[10] = 0xdeadbeefu;
        regs[11] = 0xfeedfaceu;
        sbi_call();
        assert((int32_t)regs[10] == -1);
        assert(regs[11] == 0);
        assert(dup2(saved, STDIN_FILENO) == STDIN_FILENO);
        close(p[0]);
        close(saved);
    }

    reset_timer();
    regs[17] = 0;
    regs[16] = 0;
    regs[10] = 0x300u;
    regs[11] = 1u;
    sbi_call();
    assert(timer_deadline == 0x100000300ull);
    assert(mtimecmp == 0x100000300ull);
    assert(regs[10] == 0);

    reset_irq();
    mem_fault = 0;
    regs[17] = SBI_EXT_DBCN;
    regs[16] = 0;
    regs[10] = 1;
    regs[11] = 0x00110000u;
    regs[12] = 0;
    sbi_call();
    assert((int32_t)regs[10] == -5);
    assert(regs[11] == 0);
    assert(!mem_fault);

    reset_irq();
    regs[17] = SBI_EXT_DBCN;
    regs[16] = 1;
    regs[10] = 1;
    regs[11] = 0x00110000u;
    regs[12] = 0;
    sbi_call();
    assert((int32_t)regs[10] == -5);
    assert(regs[11] == 0);

    {
        pid_t pid = fork();
        int status;

        assert(pid >= 0);
        if (pid == 0) {
            reset_irq();
            regs[17] = SBI_EXT_SRST;
            regs[16] = 0;
            regs[10] = 99;
            regs[11] = 0;
            sbi_call();
            _exit((int32_t)regs[10] == -3 ? 42 : 43);
        }
        assert(waitpid(pid, &status, 0) == pid);
        assert(WIFEXITED(status));
        assert(WEXITSTATUS(status) == 42);

        pid = fork();
        assert(pid >= 0);
        if (pid == 0) {
            reset_irq();
            regs[17] = SBI_EXT_SRST;
            regs[16] = 0;
            regs[10] = 1;
            regs[11] = 0;
            sbi_call();
            _exit((int32_t)regs[10] == -2 ? 42 : 43);
        }
        assert(waitpid(pid, &status, 0) == pid);
        assert(WIFEXITED(status));
        assert(WEXITSTATUS(status) == 42);
    }

    {
        struct timespec ts;
        uint32_t hi;

        clock_gettime(CLOCK_MONOTONIC, &ts);
        reset_irq();
        time_inst_mode = 0;
        timebase = 1000000000u;
        boot_ns = (uint64_t)ts.tv_sec * 1000000000ull +
                  (uint64_t)ts.tv_nsec - 20000000000ull;
        update_time();
        assert(guest_time >= 19000000000ull);
        assert(guest_time <= 21000000000ull);
        assert(clint_load32(CLINT_MTIME + 4u, &hi));
        assert(hi >= 4u);
        time_inst_mode = 1;
        timebase = 10000000u;
    }

    free(ram);
    return 0;
}

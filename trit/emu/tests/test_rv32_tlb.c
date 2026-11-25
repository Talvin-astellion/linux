#include <assert.h>

#define main rv32_unused_main
#include "../rv32.c"
#undef main

#define ROOT_PA    0x80100000u
#define L2_PA      0x80200000u
#define VA1        0x80400000u
#define VA2        0x80401000u
#define ASID_SHIFT 22

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

static void reset_mmu(void)
{
    satp = 0;
    priv = 1;
    mstatus = 0;
    mem_fault = 0;
    mmu_fault_cause = 0;
    mmu_walks = tlb_hits = tlb_misses = mmu_faults = 0;
    pmp_reset();
    tlb_flush();
}

static void set_satp(uint32_t asid)
{
    csr_write(0x180, SATP_MODE_SV32 | ((asid & 0x1ffu) << ASID_SHIFT) |
               (0x80100u & SATP_PPN_MASK));
}

static void map_4k(uint32_t va, uint32_t pa)
{
    uint32_t vpn0 = (va >> 12) & 0x3ffu;
    uint32_t leaf = (pa & 0xfffff000u) >> 2;

    put32(ROOT_PA + 0x201u * 4u, (0x80200u << 10) | PTE_V);
    put32(L2_PA + vpn0 * 4u, leaf | PTE_V | PTE_R | PTE_W | PTE_X |
          PTE_A | PTE_D);
}

static int xlate(uint32_t va, uint32_t *pa)
{
    mem_fault = 0;
    mmu_fault_cause = 0;
    return translate_addr(va, ACCESS_LOAD, pa);
}

static void sfence(unsigned rs1, unsigned rs2)
{
    execute(0x12000073u | (rs1 << 15) | (rs2 << 20), VA1);
}

int main(void)
{
    uint32_t pa;
    uint32_t root_entry;

    ram = calloc(1, RAM_SIZE);
    assert(ram != NULL);

    reset_mmu();
    set_satp(3);
    assert((satp & 0x7fc00000u) == (3u << ASID_SHIFT));

    reset_mmu();
    map_4k(VA1, VA1);
    map_4k(VA2, VA2);
    set_satp(0);
    assert(xlate(VA1, &pa) && pa == VA1);
    assert(xlate(VA2, &pa) && pa == VA2);
    mmu_walks = 0;
    regs[5] = VA1;
    sfence(5, 0);
    assert(xlate(VA1, &pa));
    assert(mmu_walks == 1);
    assert(xlate(VA2, &pa));
    assert(mmu_walks == 1);

    reset_mmu();
    map_4k(VA1, VA1);
    set_satp(1);
    assert(xlate(VA1, &pa));
    set_satp(2);
    assert(xlate(VA1, &pa));
    regs[6] = 1;
    sfence(0, 6);
    mmu_walks = 0;
    set_satp(1);
    assert(xlate(VA1, &pa));
    assert(mmu_walks == 1);
    mmu_walks = 0;
    set_satp(2);
    assert(xlate(VA1, &pa));
    assert(mmu_walks == 0);

    reset_mmu();
    root_entry = (0x80401u << 10) | PTE_V | PTE_R | PTE_X |
                 PTE_A | PTE_D;
    put32(ROOT_PA + 0x201u * 4u, root_entry);
    set_satp(0);
    assert(!xlate(VA1, &pa));
    assert(mmu_fault_cause == CAUSE_LOAD_PF);
    assert(get32(ROOT_PA + 0x201u * 4u) == root_entry);

    reset_mmu();
    put32(ROOT_PA + 0x201u * 4u, (0x80200u << 10) | PTE_V);
    put32(L2_PA + 0u * 4u, (0x80400u << 10) | PTE_V | PTE_W);
    set_satp(0);
    assert(!xlate(VA1, &pa));
    assert(mmu_fault_cause == CAUSE_LOAD_PF);

    reset_mmu();
    map_4k(VA1, VA1);
    set_satp(0);
    assert(xlate(VA1, &pa));
    assert(mmu_walks == 1);
    pmpaddr[0] = (VA1 >> 2) | 0x3ffu;
    assert(!xlate(VA1, &pa));
    assert(mmu_fault_cause == CAUSE_LOAD);

    reset_mmu();
    csr_write(0x180, SATP_MODE_SV32 | (0x100u << ASID_SHIFT) |
               (0x80100u & SATP_PPN_MASK));
    assert((satp & SATP_MODE_SV32) != 0);
    assert(((satp & SATP_ASID_MASK) >> ASID_SHIFT) == 0x100u);

    free(ram);
    return 0;
}

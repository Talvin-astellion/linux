#include <assert.h>

#define main rv32_unused_main
#include "../rv32.c"
#undef main

#define ROOT_PA 0x80100000u
#define L2_PA   0x80200000u

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

static int xlate(uint32_t va, enum MemAccess access, uint32_t *pa)
{
    mem_fault = 0;
    mmu_fault_cause = 0;
    return translate_addr(va, access, pa);
}

int main(void)
{
    uint32_t pa;
    uint32_t leaf;
    uint32_t l2_index0 = L2_PA + 0u * 4u;
    uint32_t l2_index1 = L2_PA + 1u * 4u;
    uint32_t l2_index2 = L2_PA + 2u * 4u;

    ram = calloc(1, RAM_SIZE);
    assert(ram != NULL);

    satp = 0;
    priv = 1;
    mstatus = 0;
    pmp_reset();
    assert(xlate(0x80400000u, ACCESS_FETCH, &pa) && pa == 0x80400000u);
    assert(xlate(0x10000000u, ACCESS_LOAD, &pa) && pa == 0x10000000u);

    put32(ROOT_PA + 0x201u * 4u, (0x80200u << 10) | PTE_V);
    leaf = (0x80400u << 10) | PTE_V | PTE_R | PTE_W | PTE_X | PTE_U;
    put32(l2_index0, leaf);
    satp = SATP_MODE_SV32 | (0x80100u & SATP_PPN_MASK);

    priv = 0;
    assert(xlate(0x80400123u, ACCESS_LOAD, &pa) && pa == 0x80400123u);
    assert(xlate(0x80400123u, ACCESS_FETCH, &pa) && pa == 0x80400123u);
    assert(xlate(0x80400123u, ACCESS_STORE, &pa) && pa == 0x80400123u);

    priv = 1;
    assert(!xlate(0x80400123u, ACCESS_LOAD, &pa));
    assert(mmu_fault_cause == CAUSE_LOAD_PF);
    mstatus |= MSTATUS_SUM;
    assert(xlate(0x80400123u, ACCESS_LOAD, &pa));
    assert(!xlate(0x80400123u, ACCESS_FETCH, &pa));
    assert(mmu_fault_cause == CAUSE_INST_PF);
    mstatus &= ~MSTATUS_SUM;

    put32(ROOT_PA + 0x202u * 4u, (0x80800u << 10) | PTE_V |
          PTE_R | PTE_X | PTE_A);
    assert(xlate(0x80b00123u, ACCESS_FETCH, &pa) && pa == 0x80b00123u);

    put32(ROOT_PA + 0x203u * 4u, (0x80b00u << 10) | PTE_V |
          PTE_R | PTE_X | PTE_A);
    assert(!xlate(0x80c00123u, ACCESS_FETCH, &pa));
    assert(mmu_fault_cause == CAUSE_INST_PF);

    put32(l2_index1, (0x80400u << 10) | PTE_V | PTE_R | PTE_X | PTE_U);
    priv = 0;
    assert(xlate(0x80401123u, ACCESS_LOAD, &pa));
    assert(!xlate(0x80401123u, ACCESS_STORE, &pa));
    assert(mmu_fault_cause == CAUSE_STORE_PF);

    put32(l2_index2, (0x80400u << 10) | PTE_V | PTE_X | PTE_U);
    priv = 0;
    assert(!xlate(0x80402123u, ACCESS_LOAD, &pa));
    mstatus |= MSTATUS_MXR;
    assert(xlate(0x80402123u, ACCESS_LOAD, &pa));
    mstatus &= ~MSTATUS_MXR;

    put32(l2_index1, 0);
    tlb_flush();
    priv = 1;
    assert(!xlate(0x80401123u, ACCESS_LOAD, &pa));
    assert(mmu_fault_cause == CAUSE_LOAD_PF);

    assert(!xlate(0x90000000u, ACCESS_FETCH, &pa));
    assert(mmu_fault_cause == CAUSE_INST_PF);

    priv = 0;
    put32(l2_index0, (0x80400u << 10) | PTE_V | PTE_R | PTE_W | PTE_X | PTE_U);
    tlb_flush();
    assert(xlate(0x80400123u, ACCESS_LOAD, &pa));
    assert((get32(l2_index0) & PTE_A) != 0);
    assert(xlate(0x80400123u, ACCESS_STORE, &pa));
    assert((get32(l2_index0) & PTE_D) != 0);

    put32(l2_index1, (0x80400u << 10) | PTE_V | PTE_W | PTE_U);
    tlb_flush();
    priv = 0;
    assert(!xlate(0x80401123u, ACCESS_STORE, &pa));
    assert(mmu_fault_cause == CAUSE_STORE_PF);
    put32(l2_index1, 0);
    tlb_flush();

    put32(ROOT_PA + 0x203u * 4u, (0x80800u << 10) | PTE_V | PTE_W | PTE_U);
    tlb_flush();
    assert(!xlate(0x80c00123u, ACCESS_STORE, &pa));
    assert(mmu_fault_cause == CAUSE_STORE_PF);

    put32(l2_index0, (0x80400u << 10) | PTE_V | PTE_R | PTE_W | PTE_X |
          PTE_U | PTE_A | PTE_D);
    tlb_flush();
    mem_fault = 0;
    mmu_fault_cause = 0;
    (void)mem_load32(0x80400ffeu);
    assert(mmu_fault_cause == CAUSE_LOAD_PF);

    mem_fault = 0;
    mmu_fault_cause = 0;
    mem_store32(0x80400ffeu, 0x11223344u);
    assert(mmu_fault_cause == CAUSE_STORE_PF);
    assert(mem_fault == 0);

    csr_write(0x180, 0xc0000000u);
    assert(satp == 0xc0000000u);
    csr_write(0x180, 0u);
    assert(satp == 0);
    assert(xlate(0x80400123u, ACCESS_LOAD, &pa) && pa == 0x80400123u);

    put32(ROOT_PA + 0x201u * 4u, (0x80200u << 10) | PTE_V);
    put32(l2_index0, (0x80400u << 10) | PTE_V | PTE_R | PTE_U |
          PTE_A | PTE_D);
    csr_write(0x180, SATP_MODE_SV32 | (0x80100u & SATP_PPN_MASK));
    priv = 0;
    mstatus = 0;
    mmu_walks = tlb_hits = tlb_misses = 0;
    assert(xlate(0x80400123u, ACCESS_LOAD, &pa) && pa == 0x80400123u);
    assert(mmu_walks == 1 && tlb_misses == 1 && tlb_hits == 0);
    assert(xlate(0x80400123u, ACCESS_LOAD, &pa) && pa == 0x80400123u);
    assert(mmu_walks == 1 && tlb_misses == 1 && tlb_hits == 1);

    put32(l2_index0, 0);
    assert(xlate(0x80400123u, ACCESS_LOAD, &pa) && pa == 0x80400123u);
    assert(mmu_walks == 1);
    tlb_flush();
    assert(!xlate(0x80400123u, ACCESS_LOAD, &pa));
    assert(mmu_fault_cause == CAUSE_LOAD_PF);

    put32(l2_index0, (0x80400u << 10) | PTE_V | PTE_R | PTE_U |
          PTE_A | PTE_D);
    tlb_flush();
    priv = 1;
    assert(!xlate(0x80400123u, ACCESS_LOAD, &pa));
    mstatus |= MSTATUS_SUM;
    assert(xlate(0x80400123u, ACCESS_LOAD, &pa));
    mmu_walks = 0;
    mstatus &= ~MSTATUS_SUM;
    assert(!xlate(0x80400123u, ACCESS_LOAD, &pa));
    assert(mmu_walks == 0);
    assert(mmu_fault_cause == CAUSE_LOAD_PF);

    priv = 0;
    csr_write(0x180, SATP_MODE_SV32 | (0x80200u & SATP_PPN_MASK));
    assert(!xlate(0x80400123u, ACCESS_LOAD, &pa));
    assert(mmu_fault_cause == CAUSE_LOAD_PF);
    csr_write(0x180, 0);

    memset(pmpcfg, 0, sizeof(pmpcfg));
    memset(pmpaddr, 0, sizeof(pmpaddr));
    csr_write(0x3b0, 0x201001ffu);
    csr_write(0x3a0, (3u << 3) | 1u);
    priv = 0;
    satp = 0;
    assert(pmp_check(0x80400000u, 4, ACCESS_LOAD));
    assert(!pmp_check(0x80400000u, 4, ACCESS_STORE));
    assert(!pmp_check(0x80400000u, 4, ACCESS_FETCH));
    put32(0x80400000u, 0x12345678u);
    assert(mem_load32(0x80400000u) == 0x12345678u);
    mem_store32(0x80400000u, 0xdeadbeefu);
    assert(mem_fault);

    priv = 3;
    assert(pmp_check(0x80400000u, 4, ACCESS_STORE));
    priv = 0;
    csr_write(0x3a0, 0x80u | (3u << 3) | 1u);
    csr_write(0x3a0, (3u << 3) | 1u);
    csr_write(0x3b0, 0u);
    assert((pmpcfg[0] & 0xffu) == (0x80u | (3u << 3) | 1u));
    assert(pmpaddr[0] == 0x201001ffu);

    memset(pmpcfg, 0, sizeof(pmpcfg));
    memset(pmpaddr, 0, sizeof(pmpaddr));
    csr_write(0x3b0, 0x80400000u >> 2);
    csr_write(0x3a0, (2u << 3) | 2u);
    assert(pmp_check(0x80400000u, 4, ACCESS_STORE));
    assert(!pmp_check(0x80400000u, 4, ACCESS_LOAD));

    memset(pmpcfg, 0, sizeof(pmpcfg));
    memset(pmpaddr, 0, sizeof(pmpaddr));
    pmpaddr[0] = 0x80400000u >> 2;
    csr_write(0x3a0, (2u << 3) | 1u);
    priv = 0;
    assert(pmp_check(0x80400000u, 4, ACCESS_LOAD));
    assert(!pmp_check(0x80400000u, 4, ACCESS_STORE));
    assert(!pmp_check(0x80400004u, 4, ACCESS_STORE));

    memset(pmpcfg, 0, sizeof(pmpcfg));
    memset(pmpaddr, 0, sizeof(pmpaddr));
    pmpaddr[0] = 0x80400000u >> 2;
    pmpaddr[1] = 0x80401000u >> 2;
    csr_write(0x3a0, ((1u << 3) | 2u) << 8);
    assert(pmp_check(0x80400000u, 4, ACCESS_STORE));
    assert(!pmp_check(0x80400ffcu, 4, ACCESS_LOAD));

    memset(pmpcfg, 0, sizeof(pmpcfg));
    memset(pmpaddr, 0, sizeof(pmpaddr));
    pmpaddr[0] = 0x201001ffu;
    csr_write(0x3a0, (3u << 3) | 1u);
    assert(pmp_check(0x80400fffu, 1, ACCESS_LOAD));
    assert(!pmp_check(0x80400fffu, 1, ACCESS_STORE));
    assert(!pmp_check(0x80401000u, 1, ACCESS_STORE));

    memset(pmpcfg, 0, sizeof(pmpcfg));
    memset(pmpaddr, 0, sizeof(pmpaddr));
    priv = 1;
    satp = 0;
    assert(!pmp_check(0x80400000u, 4, ACCESS_LOAD));
    priv = 3;
    assert(pmp_check(0x80400000u, 4, ACCESS_LOAD));

    free(ram);
    return 0;
}

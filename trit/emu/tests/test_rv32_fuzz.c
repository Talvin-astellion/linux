#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define main rv32_unused_main
#include "../rv32.c"
#undef main

#define ROOT_PA 0x80100000u
#define L2_PA   0x80200000u

static uint32_t rng_state = 0x5eed1234u;

static uint32_t rnd(void)
{
    uint32_t x = rng_state;

    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng_state = x;
    return x;
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

static int ref_pmp(uint32_t pa, uint32_t size, enum MemAccess access)
{
    uint32_t need = access == ACCESS_LOAD ? 0x01u :
                    access == ACCESS_STORE ? 0x02u : 0x04u;
    unsigned i;

    if (priv == 3 || size == 0)
        return 1;
    for (i = 0; i < 16u; i++) {
        uint32_t cfg = (pmpcfg[i >> 2] >> (8u * (i & 3u))) & 0xffu;
        unsigned mode = (cfg >> 3) & 3u;
        uint64_t base, end;

        if (mode == 0)
            continue;
        if (mode == 1) {
            base = i ? (uint64_t)pmpaddr[i - 1] << 2 : 0;
            end = (uint64_t)pmpaddr[i] << 2;
        } else if (mode == 2) {
            base = (uint64_t)pmpaddr[i] << 2;
            end = base + 4;
        } else {
            uint32_t addr = pmpaddr[i];
            unsigned ones = 0;

            while (ones < 32 && (addr & 1u)) {
                ones++;
                addr >>= 1;
            }
            if (ones == 0)
                continue;
            if (ones >= 31) {
                base = 0;
            } else {
                uint32_t lowmask = (1u << (ones + 1)) - 1u;

                base = (uint64_t)(pmpaddr[i] & ~lowmask) << 2;
            }
            end = base + (1ULL << (ones + 3));
        }
        if ((uint64_t)pa >= base && (uint64_t)pa + size <= end)
            return (cfg & need) != 0;
    }
    return 0;
}

static uint32_t random_pmpaddr(void)
{
    switch (rnd() & 7u) {
    case 0:
        return 0xffffffffu;
    case 1:
        return 0x7fffffffu;
    case 2:
        return 0x3fffffffu;
    case 3:
        return 0x201001ffu;
    case 4:
        return ROOT_PA >> 2;
    default:
        return rnd();
    }
}

static int fuzz_pmp(unsigned long iterations)
{
    unsigned long i;

    for (i = 0; i < iterations; i++) {
        uint32_t pa, size, access_word;
        enum MemAccess access;
        int got, expected;

        pmpcfg[0] = rnd();
        pmpcfg[1] = rnd();
        pmpcfg[2] = rnd();
        pmpcfg[3] = rnd();
        for (size = 0; size < 16u; size++)
            pmpaddr[size] = random_pmpaddr();

        priv = (int)(rnd() & 3u);
        switch (rnd() & 3u) {
        case 0:
            pa = 0x80400000u + (rnd() & 0xfffu);
            break;
        case 1:
            pa = RAM_BASE + (rnd() & 0x0fffffffu);
            break;
        default:
            pa = rnd();
            break;
        }
        size = 1u << (rnd() & 4u);
        access_word = rnd() % 3u;
        access = (enum MemAccess)access_word;

        got = pmp_check(pa, size, access);
        expected = ref_pmp(pa, size, access);
        if (got != expected) {
            fprintf(stderr,
                    "pmp mismatch i=%lu pa=0x%08x size=%u access=%u priv=%d "
                    "got=%d expected=%d cfg=0x%08x/0x%08x/0x%08x/0x%08x\n",
                    i, pa, size, access_word, priv, got, expected,
                    pmpcfg[0], pmpcfg[1], pmpcfg[2], pmpcfg[3]);
            for (size = 0; size < 16u; size++)
                fprintf(stderr, "pmpaddr[%u]=0x%08x\n", size, pmpaddr[size]);
            return 1;
        }
    }
    return 0;
}

static uint32_t random_leaf(uint32_t base)
{
    uint32_t pte = (base >> 12) << 10;

    if ((rnd() & 7u) != 0)
        pte |= PTE_V;
    if (rnd() & 3u)
        pte |= PTE_R;
    if (rnd() & 3u)
        pte |= PTE_W;
    if (rnd() & 3u)
        pte |= PTE_X;
    if (rnd() & 3u)
        pte |= PTE_U;
    if (rnd() & 3u)
        pte |= PTE_A;
    if (rnd() & 3u)
        pte |= PTE_D;
    if ((rnd() & 7u) == 0)
        pte |= (rnd() & 0x3ffu) << 10;
    return pte;
}

static int ref_translate(uint32_t va, enum MemAccess access,
                         uint32_t *pa_out, uint32_t *cause_out,
                         uint32_t *pte_addr_out)
{
    uint32_t root = (satp & SATP_PPN_MASK) << 12;
    uint32_t vpn1 = va >> 22;
    uint32_t vpn0 = (va >> 12) & 0x3ffu;
    uint32_t pte_addr = root + vpn1 * 4u;
    uint32_t pte, base, offset_mask;
    uint32_t needed;

    *pa_out = 0;
    *pte_addr_out = pte_addr;
    *cause_out = 0;
    if (!(satp & SATP_MODE_SV32)) {
        *pa_out = va;
        return 1;
    }
    if (!phys_in_ram_n(pte_addr, 4) ||
        !ref_pmp(pte_addr, 4, ACCESS_LOAD)) {
        *cause_out = pmp_fault_cause(access);
        return 0;
    }
    pte = get32(pte_addr);
    if (!(pte & PTE_V))
        goto fail;

    if (!(pte & PTE_RWX)) {
        uint32_t next = (pte & 0xfffffc00u) << 2;

        pte_addr = next + vpn0 * 4u;
        *pte_addr_out = pte_addr;
        if (!phys_in_ram_n(pte_addr, 4) ||
            !ref_pmp(pte_addr, 4, ACCESS_LOAD)) {
            *cause_out = pmp_fault_cause(access);
            return 0;
        }
        pte = get32(pte_addr);
        if (!(pte & PTE_V) || !(pte & PTE_RWX))
            goto fail;
        if ((pte & PTE_W) && !(pte & PTE_R))
            goto fail;
        base = (pte & 0xfffffc00u) << 2;
        offset_mask = 0xfffu;
    } else {
        if ((pte & PTE_W) && !(pte & PTE_R))
            goto fail;
        if (pte & (0x3ffu << 10))
            goto fail;
        base = (pte & 0xfffffc00u) << 2;
        offset_mask = 0x003fffffu;
    }

    if (priv == 0) {
        if (!(pte & PTE_U))
            goto fail;
    } else if (pte & PTE_U) {
        if (access == ACCESS_FETCH || !(mstatus & MSTATUS_SUM))
            goto fail;
    }
    if (access == ACCESS_STORE)
        needed = PTE_W;
    else if (access == ACCESS_FETCH)
        needed = PTE_X;
    else if ((pte & PTE_X) && !(pte & PTE_R) && (mstatus & MSTATUS_MXR))
        needed = PTE_X;
    else
        needed = PTE_R;
    if (!(pte & needed))
        goto fail;

    if (!(pte & PTE_A) || (access == ACCESS_STORE && !(pte & PTE_D))) {
        uint32_t updated = pte | PTE_A |
                           (access == ACCESS_STORE ? PTE_D : 0u);

        if (!ref_pmp(pte_addr, 4, ACCESS_STORE)) {
            *cause_out = pmp_fault_cause(access);
            return 0;
        }
        if (phys_in_ram_n(pte_addr, 4))
            put32(pte_addr, updated);
        pte = updated;
    }

    *pa_out = base | (va & offset_mask);
    return 1;

fail:
    *cause_out = access == ACCESS_FETCH ? CAUSE_INST_PF :
                 access == ACCESS_STORE ? CAUSE_STORE_PF : CAUSE_LOAD_PF;
    return 0;
}

static int fuzz_sv32(unsigned long iterations, int with_pmp)
{
    unsigned long i;

    for (i = 0; i < iterations; i++) {
        uint32_t vpn1 = rnd() & 0x3ffu;
        uint32_t vpn0 = rnd() & 0x3ffu;
        uint32_t va = (vpn1 << 22) | (vpn0 << 12) | (rnd() & 0xfffu);
        uint32_t pte1_addr = ROOT_PA + vpn1 * 4u;
        uint32_t pte2_addr = L2_PA + vpn0 * 4u;
        uint32_t pte1, pte2;
        uint32_t expected_pa, expected_cause, expected_pte_addr;
        uint32_t expected_word, got_pa = 0, got_word;
        enum MemAccess access = (enum MemAccess)(rnd() % 3u);
        int expected, got;

        if (with_pmp) {
            unsigned entry;

            pmpcfg[0] = rnd();
            pmpcfg[1] = rnd();
            pmpcfg[2] = rnd();
            pmpcfg[3] = rnd();
            for (entry = 0; entry < 16u; entry++)
                pmpaddr[entry] = random_pmpaddr();
            entry = rnd() & 15u;
            pmpaddr[entry] = (rnd() & 1u) ? (ROOT_PA >> 2) : (L2_PA >> 2);
            pmpcfg[entry >> 2] &= ~(0x9fu << (8u * (entry & 3u)));
            pmpcfg[entry >> 2] |= (rnd() & 0x9fu) << (8u * (entry & 3u));
        } else {
            pmp_reset();
        }
        priv = (rnd() & 1u) ? 0 : 1;
        mstatus = 0;
        if (rnd() & 1u)
            mstatus |= MSTATUS_SUM;
        if (rnd() & 1u)
            mstatus |= MSTATUS_MXR;
        satp = SATP_MODE_SV32 |
               ((rnd() & 0x1ffu) << SATP_ASID_SHIFT) |
               ((ROOT_PA >> 12) & SATP_PPN_MASK);

        if (rnd() & 1u) {
            uint32_t base = 0x80400000u + ((rnd() & 0x0fu) << 22);

            pte1 = random_leaf(base);
        } else {
            pte1 = ((L2_PA >> 12) << 10) | PTE_V;
            if (rnd() & 1u)
                pte1 |= PTE_U;
            if ((rnd() & 3u) == 0) {
                pte2 = 0;
            } else {
                uint32_t base = 0x80500000u + ((rnd() & 0x3fffu) << 12);

                pte2 = random_leaf(base);
            }
            put32(pte2_addr, pte2);
        }
        put32(pte1_addr, pte1);

        expected = ref_translate(va, access, &expected_pa, &expected_cause,
                                 &expected_pte_addr);
        expected_word = get32(expected_pte_addr);

        tlb_flush();
        mem_fault = 0;
        mmu_fault_cause = 0;
        got = translate_addr(va, access, &got_pa);
        got_word = get32(expected_pte_addr);

        if (got != expected || (got && got_pa != expected_pa) ||
            (!got && mmu_fault_cause != expected_cause) ||
            got_word != expected_word) {
            fprintf(stderr,
                    "sv32 mismatch pmp=%d i=%lu va=0x%08x access=%u priv=%d "
                    "mstatus=0x%08x satp=0x%08x pte1=0x%08x pte2=0x%08x "
                    "got=%d pa=0x%08x cause=%u word=0x%08x "
                    "expected=%d pa=0x%08x cause=%u word=0x%08x\n",
                    with_pmp, i, va, (unsigned)access, priv, mstatus, satp,
                    get32(pte1_addr), get32(pte2_addr),
                    got, got_pa, mmu_fault_cause, got_word,
                    expected, expected_pa, expected_cause, expected_word);
            return 1;
        }
    }
    return 0;
}

int main(int argc, char **argv)
{
    unsigned long iterations = 100000ul;

    ram = calloc(1, RAM_SIZE);
    assert(ram != NULL);
    if (argc > 1)
        iterations = strtoul(argv[1], NULL, 0);

    if (fuzz_pmp(iterations))
        return 1;
    if (fuzz_sv32(iterations, 0))
        return 1;
    if (fuzz_sv32(iterations, 1))
        return 1;
    free(ram);
    printf("all fuzz cases passed\n");
    return 0;
}

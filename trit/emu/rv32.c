/****************************************************************************
 * Copyright (C) 2026 by the TernCpuEmu contributors                          *
 *                                                                          *
 * This file is part of TernCpuEmu.                                         *
 *                                                                          *
 *   TernCpuEmu is free software: you can redistribute it and/or modify it  *
 *   under the terms of the GNU Lesser General Public License as published  *
 *   by the Free Software Foundation, either version 2 of the License, or   *
 *   (at your option) any later version.                                    *
 *                                                                          *
 *   TernCpuEmu is distributed in the hope that it will be useful,          *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of         *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          *
 *   GNU Lesser General Public License for more details.                    *
 *                                                                          *
 *   You should have received a copy of the GNU Lesser General Public       *
 *   License along with TernCpuEmu. If not, see                             *
 *   <http://www.gnu.org/licenses/>.                                        *
 ****************************************************************************/

/**
 * @file    rv32.c
 * @brief   RV32IMA guest interpreter for the ternary host emulator.
 *
 * The interpreter is deliberately independent of the ternary ISA: one guest
 * byte occupies one host byte.  The ternary machine is the machine that runs
 * this interpreter, not the machine that runs Linux directly.
 */

#include <errno.h>
#include <fenv.h>
#include <fcntl.h>
#include <inttypes.h>
#include <math.h>
#include <poll.h>
#include <quadmath.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define RAM_BASE        0x80000000u
#define RAM_SIZE        (128u * 1024u * 1024u)
#define KERNEL_ADDR     0x80400000u
#define DTB_ADDR        0x83e00000u
#define INITRD_ADDR     0x83000000u

#define UART_ADDR       0x10000000u
#define UART_SIZE       0x100u
#define CLINT_ADDR      0x02000000u
#define CLINT_SIZE      0x00010000u
#define CLINT_MTIMECMP  0x02004000u
#define CLINT_MTIME     0x0200bff8u
#define TEST_ADDR       0x00100000u
#define TEST_SIZE       0x1000u

#define MSTATUS_SIE     (1u << 1)
#define MSTATUS_MIE     (1u << 3)
#define MSTATUS_SPIE    (1u << 5)
#define MSTATUS_MPIE    (1u << 7)
#define MSTATUS_SPP     (1u << 8)
#define MSTATUS_MPP     (3u << 11)
#define MSTATUS_FS      (3u << 13)
#define MSTATUS_XS      (3u << 15)
#define MSTATUS_SUM     (1u << 18)
#define MSTATUS_MXR     (1u << 19)

#define SSTATUS_MASK    (MSTATUS_SIE | MSTATUS_SPIE | MSTATUS_SPP | \
                         MSTATUS_FS | MSTATUS_XS | MSTATUS_SUM | MSTATUS_MXR)
#define MSTATUS_MASK    (SSTATUS_MASK | MSTATUS_MIE | MSTATUS_MPIE | \
                         MSTATUS_MPP)

#define SIE_SSIE        (1u << 1)
#define SIE_STIE        (1u << 5)
#define SIE_SEIE        (1u << 9)
#define MIE_MSIE        (1u << 3)
#define MIE_MTIE        (1u << 7)
#define MIE_MEIE        (1u << 11)

#define SIP_SSIP        (1u << 1)
#define SIP_STIP        (1u << 5)
#define SIP_SEIP        (1u << 9)
#define MIP_MSIP        (1u << 3)
#define MIP_MTIP        (1u << 7)
#define MIP_MEIP        (1u << 11)

#define S_INTERRUPT_MASK (SIP_SSIP | SIP_STIP | SIP_SEIP)
#define M_INTERRUPT_MASK (MIP_MSIP | MIP_MTIP | MIP_MEIP)
#define INTERRUPT_MASK   (S_INTERRUPT_MASK | M_INTERRUPT_MASK)

#define CAUSE_SOFTWARE  1
#define CAUSE_ILLEGAL   2
#define CAUSE_BREAK     3
#define CAUSE_LOAD      5
#define CAUSE_STORE     7
#define CAUSE_ECALL_U   8
#define CAUSE_ECALL_S   9
#define CAUSE_EXT_S     9
#define CAUSE_TIMER     5
#define CAUSE_M_SOFTWARE 3
#define CAUSE_M_TIMER    7
#define CAUSE_M_EXTERNAL 11

#define SBI_EXT_BASE    0x10u
#define SBI_EXT_TIME    0x54494d45u
#define SBI_EXT_IPI     0x735049u
#define SBI_EXT_RFENCE  0x52464e43u
#define SBI_EXT_HSM     0x48534du
#define SBI_EXT_SRST    0x53525354u
#define SBI_EXT_DBCN    0x4442434eu

#define SBI_SUCCESS     0
#define SBI_ERR_FAILED  (-1)
#define SBI_ERR_NOT_SUPPORTED (-2)
#define SBI_ERR_INVALID_PARAM (-3)
#define SBI_ERR_INVALID_ADDRESS (-5)

#define SBI_SPEC_VERSION 0x02000000u
#define SBI_IMPL_ID      0
#define SBI_IMPL_VERSION 0

#define FDT_MAGIC       0xd00dfeedu
#define FDT_BEGIN_NODE  1
#define FDT_END_NODE    2
#define FDT_PROP        3
#define FDT_NOP         4
#define FDT_END         9

#define SATP_MODE_SV32  0x80000000u
#define SATP_ASID_MASK  0x7fc00000u
#define SATP_ASID_SHIFT 22
#define SATP_PPN_MASK   0x003fffffu
#define PTE_V           0x00000001u
#define PTE_R           0x00000002u
#define PTE_W           0x00000004u
#define PTE_X           0x00000008u
#define PTE_U           0x00000010u
#define PTE_A           0x00000040u
#define PTE_D           0x00000080u
#define PTE_RWX         (PTE_R | PTE_W | PTE_X)
#define TLB_SETS        128u
#define TLB_WAYS        2u
#define CAUSE_MISALIGNED 4
#define CAUSE_INST_FAULT 1
#define CAUSE_INST_PF   12
#define CAUSE_LOAD_PF   13
#define CAUSE_STORE_PF  15

#define MEDELEG_MASK    0x0000ffffu
#define MIDELEG_MASK    ((1u << CAUSE_SOFTWARE) | (1u << CAUSE_TIMER) | \
                         (1u << CAUSE_EXT_S))

#define PMP_GRANT_ALL_CFG  0x1fu
#define PMP_GRANT_ALL_ADDR 0x7fffffffu

enum MemAccess {
    ACCESS_FETCH,
    ACCESS_LOAD,
    ACCESS_STORE
};

static uint8_t *ram;
static uint32_t regs[32];
static uint64_t fpr[32];
static uint32_t fcsr;
static uint32_t fp_rm;
static uint32_t pc;
static uint32_t sstatus;
static int priv;
static uint32_t sie;
static uint32_t sip;
static uint32_t scounteren;
static uint32_t mcounteren;
static uint32_t medeleg = MEDELEG_MASK;
static uint32_t mideleg = MIDELEG_MASK;
static uint32_t pmpcfg[4];
static uint32_t pmpaddr[16];
static uint32_t stvec;
static uint32_t sepc;
static uint32_t scause;
static uint32_t stval;
static uint32_t sscratch;
static uint32_t satp;
static uint32_t mstatus;
static uint32_t mie;
static uint32_t mip;
static uint32_t mtvec;
static uint32_t mepc;
static uint32_t mcause;
static uint32_t mtval;
static uint32_t mscratch;

static uint64_t instret;
static uint64_t cycle;
static uint64_t timer_deadline = UINT64_MAX;
static uint64_t mtimecmp = UINT64_MAX;
static uint64_t boot_ns;
static uint64_t guest_time;
static uint64_t max_insn;
static uint32_t timebase = 10000000u;
static uint32_t reservation = ~0u;
static int time_inst_mode;
static int sleep_wfi = 1;
static uint64_t trace_count;
static uint64_t sample_count;
static int quiet;
static int debug_sbi;
static int debug_mmu;
static int stats_enabled;
static int mem_fault;
static uint32_t mmu_fault_cause;
static uint64_t mmu_walks;
static uint64_t mmu_faults;
static uint64_t tlb_hits;
static uint64_t tlb_misses;

struct TlbEntry {
    uint32_t vpn;
    uint32_t satp;
    uint32_t base;
    uint32_t pte;
    uint32_t pte_addr;
    uint32_t offset_mask;
    uint8_t valid;
};

static struct TlbEntry tlb[TLB_SETS][TLB_WAYS];
static uint8_t tlb_next_way[TLB_SETS];

static void console_putc(uint8_t c)
{
    ssize_t n;

    do {
        n = write(STDOUT_FILENO, &c, 1);
    } while (n < 0 && errno == EINTR);
}

static void console_write(const uint8_t *p, size_t len)
{
    size_t off = 0;

    while (off < len) {
        ssize_t n = write(STDOUT_FILENO, p + off, len - off);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return;
        }
        off += (size_t)n;
    }
}

static ssize_t console_try_read(uint8_t *p, size_t len)
{
    struct pollfd pfd = { .fd = STDIN_FILENO, .events = POLLIN };

    if (len == 0)
        return 0;
    if (poll(&pfd, 1, 0) <= 0)
        return 0;
    return read(STDIN_FILENO, p, len);
}

static void fatal(const char *msg)
{
    fprintf(stderr, "\ntern-linux: %s\n", msg);
    exit(3);
}

static void update_time(void)
{
    struct timespec ts;
    uint64_t ns;

    if (time_inst_mode) {
        guest_time = instret;
        return;
    }
    clock_gettime(CLOCK_MONOTONIC, &ts);
    ns = (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
    ns -= boot_ns;
    guest_time = (ns / 1000000000ull) * timebase +
                 (ns % 1000000000ull) * timebase / 1000000000ull;
}

static void raise_timer_irq(void)
{
    sip |= SIP_STIP;
    mip |= SIP_STIP | MIP_MTIP;
}

static void set_timer_deadline(uint64_t deadline)
{
    update_time();
    timer_deadline = deadline;
    mtimecmp = deadline;
    if (deadline != UINT64_MAX && guest_time >= deadline)
        raise_timer_irq();
    else {
        sip &= ~SIP_STIP;
        mip &= ~(SIP_STIP | MIP_MTIP);
    }
}

static void print_stats(void)
{
    struct timespec ts;
    uint64_t now_ns, wall_ns;

    update_time();
    clock_gettime(CLOCK_MONOTONIC, &ts);
    now_ns = (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
    wall_ns = now_ns >= boot_ns ? now_ns - boot_ns : 0;
    fprintf(stderr,
            "tern-linux: stats instret=%" PRIu64 " mmu_walks=%" PRIu64
            " mmu_faults=%" PRIu64 " tlb_hits=%" PRIu64
            " tlb_misses=%" PRIu64 " wall_ms=%" PRIu64
            " guest_time=%" PRIu64 "\n",
            instret, mmu_walks, mmu_faults, tlb_hits, tlb_misses,
            (uint64_t)(wall_ns / 1000000ull), guest_time);
}

static inline int in_ram(uint32_t addr)
{
    return addr >= RAM_BASE && (uint64_t)addr + 4 <= (uint64_t)RAM_BASE + RAM_SIZE;
}

static inline int in_uart(uint32_t addr)
{
    return addr >= UART_ADDR && addr < UART_ADDR + UART_SIZE;
}

static inline int in_clint(uint32_t addr)
{
    return addr >= CLINT_ADDR && addr < CLINT_ADDR + CLINT_SIZE;
}

static int clint_load8(uint32_t addr, uint8_t *val)
{
    uint64_t v;
    unsigned shift;

    if (!in_clint(addr))
        return 0;
    if (addr >= CLINT_MTIME && addr < CLINT_MTIME + 8u) {
        update_time();
        v = guest_time;
        shift = 8u * (addr - CLINT_MTIME);
    } else if (addr >= CLINT_MTIMECMP && addr < CLINT_MTIMECMP + 8u) {
        v = mtimecmp;
        shift = 8u * (addr - CLINT_MTIMECMP);
    } else {
        *val = 0;
        return 1;
    }
    *val = (uint8_t)(v >> shift);
    return 1;
}

static int clint_load32(uint32_t addr, uint32_t *val)
{
    uint64_t v;

    if (!in_clint(addr))
        return 0;
    if (addr == CLINT_MTIME || addr == CLINT_MTIME + 4u) {
        update_time();
        v = addr == CLINT_MTIME ? guest_time : guest_time >> 32;
    } else if (addr == CLINT_MTIMECMP) {
        v = mtimecmp;
    } else if (addr == CLINT_MTIMECMP + 4u) {
        v = mtimecmp >> 32;
    } else {
        *val = 0;
        return 1;
    }
    *val = (uint32_t)v;
    return 1;
}

static int clint_store8(uint32_t addr, uint8_t val)
{
    uint64_t v;
    unsigned shift;

    if (!in_clint(addr))
        return 0;
    if (addr >= CLINT_MTIMECMP && addr < CLINT_MTIMECMP + 8u) {
        shift = 8u * (addr - CLINT_MTIMECMP);
        v = (mtimecmp & ~(0xffull << shift)) | ((uint64_t)val << shift);
        set_timer_deadline(v);
    }
    return 1;
}

static int clint_store32(uint32_t addr, uint32_t val)
{
    if (!in_clint(addr))
        return 0;
    if (addr == CLINT_MTIMECMP) {
        set_timer_deadline((mtimecmp & 0xffffffff00000000ull) | val);
    } else if (addr == CLINT_MTIMECMP + 4u) {
        set_timer_deadline((mtimecmp & 0xffffffffull) | ((uint64_t)val << 32));
    }
    return 1;
}

static inline int in_test(uint32_t addr)
{
    return addr >= TEST_ADDR && addr < TEST_ADDR + TEST_SIZE;
}

static inline int phys_in_ram(uint32_t addr)
{
    return addr >= RAM_BASE && addr < (uint64_t)RAM_BASE + RAM_SIZE;
}

static inline int phys_in_ram_n(uint32_t addr, uint32_t size)
{
    return addr >= RAM_BASE && (uint64_t)addr + size <= (uint64_t)RAM_BASE + RAM_SIZE;
}

static int phys_range_valid(uint32_t addr, uint32_t size)
{
    if (size == 0 || phys_in_ram_n(addr, size))
        return 1;
    if (in_uart(addr) && size <= UART_ADDR + UART_SIZE - addr)
        return 1;
    if (in_clint(addr) && size <= CLINT_ADDR + CLINT_SIZE - addr)
        return 1;
    if (in_test(addr) && size <= TEST_ADDR + TEST_SIZE - addr)
        return 1;
    return 0;
}

static int pmp_locked(unsigned entry)
{
    return (pmpcfg[entry >> 2] >> (8u * (entry & 3u))) & 0x80u;
}

static void pmp_reset(void)
{
    memset(pmpcfg, 0, sizeof(pmpcfg));
    memset(pmpaddr, 0, sizeof(pmpaddr));
    pmpcfg[0] = PMP_GRANT_ALL_CFG;
    pmpaddr[0] = PMP_GRANT_ALL_ADDR;
}

static int pmp_check(uint32_t pa, uint32_t size, enum MemAccess access)
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

static uint32_t pmp_fault_cause(enum MemAccess access)
{
    return access == ACCESS_FETCH ? CAUSE_INST_FAULT :
           access == ACCESS_STORE ? CAUSE_STORE : CAUSE_LOAD;
}

static uint8_t phys_load8(uint32_t addr)
{
    uint8_t val;

    if (phys_in_ram(addr))
        return ram[addr - RAM_BASE];
    if (clint_load8(addr, &val))
        return val;
    if (in_uart(addr) || in_test(addr))
        return 0;
    mem_fault = 1;
    return 0;
}

static uint32_t phys_load32(uint32_t addr)
{
    uint32_t val;

    if (clint_load32(addr, &val))
        return val;
    if (!(addr & 3u) && phys_in_ram_n(addr, 4))
        return (uint32_t)ram[addr - RAM_BASE] |
               ((uint32_t)ram[addr - RAM_BASE + 1] << 8) |
               ((uint32_t)ram[addr - RAM_BASE + 2] << 16) |
               ((uint32_t)ram[addr - RAM_BASE + 3] << 24);
    return (uint32_t)phys_load8(addr) |
           ((uint32_t)phys_load8(addr + 1) << 8) |
           ((uint32_t)phys_load8(addr + 2) << 16) |
           ((uint32_t)phys_load8(addr + 3) << 24);
}

static void test_finisher(uint32_t val)
{
    if (val == 0x5555) {
        if (!quiet)
            fprintf(stderr, "\ntern-linux: test finisher PASS\n");
        exit(0);
    }
    if (val == 0x3333 || val == 0x7777) {
        if (!quiet)
            fprintf(stderr, "\ntern-linux: test finisher FAIL (0x%08x)\n", val);
        exit(1);
    }
}

static void phys_store8(uint32_t addr, uint8_t val)
{
    if (phys_in_ram(addr)) {
        ram[addr - RAM_BASE] = val;
        return;
    }
    if (clint_store8(addr, val))
        return;
    if (in_uart(addr))
        return;
    if (in_test(addr)) {
        test_finisher(val);
        return;
    }
    mem_fault = 1;
}

static void phys_store32(uint32_t addr, uint32_t val)
{
    if (clint_store32(addr, val))
        return;
    if (!(addr & 3u) && phys_in_ram_n(addr, 4)) {
        ram[addr - RAM_BASE] = val & 0xff;
        ram[addr - RAM_BASE + 1] = (val >> 8) & 0xff;
        ram[addr - RAM_BASE + 2] = (val >> 16) & 0xff;
        ram[addr - RAM_BASE + 3] = (val >> 24) & 0xff;
        return;
    }
    if (in_test(addr)) {
        test_finisher(val);
        return;
    }
    phys_store8(addr, val & 0xff);
    phys_store8(addr + 1, (val >> 8) & 0xff);
    phys_store8(addr + 2, (val >> 16) & 0xff);
    phys_store8(addr + 3, (val >> 24) & 0xff);
}

static void mmu_page_fault(uint32_t va, enum MemAccess access,
                           uint32_t pte, uint32_t pte_addr)
{
    mmu_faults++;
    mmu_fault_cause = access == ACCESS_FETCH ? CAUSE_INST_PF :
                      access == ACCESS_STORE ? CAUSE_STORE_PF : CAUSE_LOAD_PF;
    if (debug_mmu) {
        uint32_t root = (satp & SATP_PPN_MASK) << 12;

        fprintf(stderr, "mmu: va=0x%08x access=%u satp=0x%08x pte=0x%08x@0x%08x cause=%u\n",
                va, (unsigned)access, satp, pte, pte_addr, mmu_fault_cause);
        for (uint32_t i = 0; i < 1024u; i++) {
            uint32_t entry = phys_load32(root + i * 4u);
            if (entry & PTE_V)
                fprintf(stderr, "mmu: root[%03x]=0x%08x va=0x%08x\n",
                        i, entry, i << 22);
        }
    }
}

static void tlb_flush(void)
{
    memset(tlb, 0, sizeof(tlb));
    memset(tlb_next_way, 0, sizeof(tlb_next_way));
}

static void tlb_invalidate(uint32_t va, int has_va, uint32_t asid, int has_asid)
{
    unsigned set;
    unsigned way;

    for (set = 0; set < TLB_SETS; set++) {
        for (way = 0; way < TLB_WAYS; way++) {
            struct TlbEntry *e = &tlb[set][way];
            uint32_t base_va;
            uint32_t size;

            if (!e->valid)
                continue;
            if (has_asid &&
                ((e->satp & SATP_ASID_MASK) >> SATP_ASID_SHIFT) != (asid & 0x1ffu))
                continue;
            if (has_va) {
                base_va = e->vpn << 12;
                size = e->offset_mask + 1u;
                if (e->offset_mask == 0x003fffffu)
                    base_va &= ~0x003fffffu;
                if ((uint64_t)va < base_va ||
                    (uint64_t)va >= (uint64_t)base_va + size)
                    continue;
            }
            e->valid = 0;
        }
    }
}

static int pte_allows(uint32_t pte, enum MemAccess access)
{
    uint32_t needed;

    if (priv == 0) {
        if (!(pte & PTE_U))
            return 0;
    } else if (pte & PTE_U) {
        if (access == ACCESS_FETCH)
            return 0;
        if (!(mstatus & MSTATUS_SUM))
            return 0;
    }

    if (access == ACCESS_STORE)
        needed = PTE_W;
    else if (access == ACCESS_FETCH)
        needed = PTE_X;
    else if ((pte & PTE_X) && !(pte & PTE_R) && (mstatus & MSTATUS_MXR))
        needed = PTE_X;
    else
        needed = PTE_R;
    return (pte & needed) != 0;
}

static int tlb_lookup(uint32_t va, enum MemAccess access, uint32_t *pa_out)
{
    unsigned set = (va >> 12) & (TLB_SETS - 1u);
    unsigned way;
    uint32_t vpn = va >> 12;
    uint32_t denied_pte = 0, denied_pte_addr = 0;
    int matched = 0;

    for (way = 0; way < TLB_WAYS; way++) {
        struct TlbEntry *e = &tlb[set][way];

        if (!e->valid || e->vpn != vpn || e->satp != satp)
            continue;
        matched = 1;
        if (!pte_allows(e->pte, access)) {
            denied_pte = e->pte;
            denied_pte_addr = e->pte_addr;
            continue;
        }
        {
            int update = !(e->pte & PTE_A) ||
                         (access == ACCESS_STORE && !(e->pte & PTE_D));
            enum MemAccess pte_access = update ? ACCESS_STORE : ACCESS_LOAD;

            if (!pmp_check(e->pte_addr, 4, pte_access)) {
                mmu_fault_cause = pmp_fault_cause(access);
                return -1;
            }
            if (update) {
                uint32_t updated = e->pte | PTE_A |
                                   (access == ACCESS_STORE ? PTE_D : 0u);

                if (phys_in_ram_n(e->pte_addr, 4))
                    phys_store32(e->pte_addr, updated);
                e->pte = updated;
            }
        }
        *pa_out = e->base | (va & e->offset_mask);
        tlb_hits++;
        mmu_fault_cause = 0;
        return 1;
    }
    if (!matched) {
        tlb_misses++;
        return 0;
    }
    tlb_hits++;
    mmu_page_fault(va, access, denied_pte, denied_pte_addr);
    return -1;
}

static void tlb_fill(uint32_t va, uint32_t pte, uint32_t pte_addr,
                     uint32_t base, uint32_t offset_mask)
{
    unsigned set = (va >> 12) & (TLB_SETS - 1u);
    unsigned way = tlb_next_way[set] & (TLB_WAYS - 1u);
    struct TlbEntry *e = &tlb[set][way];

    tlb_next_way[set]++;
    e->vpn = va >> 12;
    e->satp = satp;
    e->base = base;
    e->pte = pte;
    e->pte_addr = pte_addr;
    e->offset_mask = offset_mask;
    e->valid = 1;
}

static int translate_addr(uint32_t va, enum MemAccess access, uint32_t *pa_out)
{
    uint32_t root, pte_addr = 0, pte = 0, base, offset_mask;
    uint32_t vpn0, vpn1;
    int lookup;

    if (!(satp & SATP_MODE_SV32)) {
        *pa_out = va;
        return 1;
    }
    lookup = tlb_lookup(va, access, pa_out);
    if (lookup != 0)
        return lookup > 0;

    mmu_walks++;
    vpn1 = va >> 22;
    vpn0 = (va >> 12) & 0x3ffu;
    root = (satp & SATP_PPN_MASK) << 12;
    pte_addr = root + vpn1 * 4u;
    if (!phys_in_ram_n(pte_addr, 4))
        goto fail;
    if (!pmp_check(pte_addr, 4, ACCESS_LOAD)) {
        mmu_fault_cause = pmp_fault_cause(access);
        return 0;
    }
    pte = phys_load32(pte_addr);
    if (!(pte & PTE_V))
        goto fail;

    if (!(pte & PTE_RWX)) {
        uint32_t next = (pte & 0xfffffc00u) << 2;

        pte_addr = next + vpn0 * 4u;
        if (!phys_in_ram_n(pte_addr, 4))
            goto fail;
        if (!pmp_check(pte_addr, 4, ACCESS_LOAD)) {
            mmu_fault_cause = pmp_fault_cause(access);
            return 0;
        }
        pte = phys_load32(pte_addr);
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

    if (!pte_allows(pte, access))
        goto fail;

    if (!(pte & PTE_A) || (access == ACCESS_STORE && !(pte & PTE_D))) {
        uint32_t updated = pte | PTE_A |
                           (access == ACCESS_STORE ? PTE_D : 0u);

        if (!pmp_check(pte_addr, 4, ACCESS_STORE)) {
            mmu_fault_cause = pmp_fault_cause(access);
            return 0;
        }
        if (phys_in_ram_n(pte_addr, 4))
            phys_store32(pte_addr, updated);
        pte = updated;
    }

    tlb_fill(va, pte, pte_addr, base, offset_mask);
    *pa_out = base | (va & offset_mask);
    mmu_fault_cause = 0;
    return 1;

fail:
    mmu_page_fault(va, access, pte, pte_addr);
    return 0;
}

static int amo_translate(uint32_t addr, enum MemAccess access,
                         uint32_t *pa, uint32_t *cause)
{
    mem_fault = 0;
    mmu_fault_cause = 0;
    if (!translate_addr(addr, access, pa)) {
        *cause = mmu_fault_cause ? mmu_fault_cause : pmp_fault_cause(access);
        return 0;
    }
    if (!pmp_check(*pa, 4, access)) {
        *cause = pmp_fault_cause(access);
        mem_fault = 0;
        return 0;
    }
    return 1;
}

static int amo_store_translate(uint32_t addr, uint32_t *pa, uint32_t *cause)
{
    uint32_t load_pa = 0;
    uint32_t load_cause = 0;

    if (!amo_translate(addr, ACCESS_STORE, pa, cause))
        return 0;
    if (!amo_translate(addr, ACCESS_LOAD, &load_pa, &load_cause) ||
        load_pa != *pa) {
        *cause = load_cause == CAUSE_LOAD_PF ? CAUSE_STORE_PF :
                 pmp_fault_cause(ACCESS_STORE);
        return 0;
    }
    return 1;
}

static uint8_t mem_load8_raw(uint32_t addr)
{
    uint32_t pa;

    if (mem_fault || mmu_fault_cause)
        return 0;
    if (!translate_addr(addr, ACCESS_LOAD, &pa))
        return 0;
    if (!pmp_check(pa, 1, ACCESS_LOAD)) {
        mem_fault = 1;
        return 0;
    }
    return phys_load8(pa);
}

static uint8_t mem_load8(uint32_t addr)
{
    mem_fault = 0;
    mmu_fault_cause = 0;
    return mem_load8_raw(addr);
}

static uint16_t mem_load16(uint32_t addr)
{
    uint16_t v;

    mem_fault = 0;
    mmu_fault_cause = 0;
    v = mem_load8_raw(addr);
    if (!mem_fault && !mmu_fault_cause)
        v |= (uint16_t)mem_load8_raw(addr + 1) << 8;
    return v;
}

static uint32_t mem_load32(uint32_t addr)
{
    uint32_t pa, v;

    mem_fault = 0;
    mmu_fault_cause = 0;
    if (!(addr & 3u)) {
        if (!translate_addr(addr, ACCESS_LOAD, &pa))
            return 0;
        if (!pmp_check(pa, 4, ACCESS_LOAD)) {
            mem_fault = 1;
            return 0;
        }
        return phys_load32(pa);
    }
    v = mem_load8_raw(addr);
    if (!mem_fault && !mmu_fault_cause)
        v |= (uint32_t)mem_load8_raw(addr + 1) << 8;
    if (!mem_fault && !mmu_fault_cause)
        v |= (uint32_t)mem_load8_raw(addr + 2) << 16;
    if (!mem_fault && !mmu_fault_cause)
        v |= (uint32_t)mem_load8_raw(addr + 3) << 24;
    return v;
}

static void mem_store8_raw(uint32_t addr, uint8_t val)
{
    uint32_t pa;

    if (mem_fault || mmu_fault_cause)
        return;
    if (!translate_addr(addr, ACCESS_STORE, &pa))
        return;
    if (!pmp_check(pa, 1, ACCESS_STORE)) {
        mem_fault = 1;
        return;
    }
    phys_store8(pa, val);
}

static void mem_store8(uint32_t addr, uint8_t val)
{
    mem_fault = 0;
    mmu_fault_cause = 0;
    mem_store8_raw(addr, val);
}

static void mem_store16(uint32_t addr, uint16_t val)
{
    mem_fault = 0;
    mmu_fault_cause = 0;
    mem_store8_raw(addr, val & 0xff);
    if (!mem_fault && !mmu_fault_cause)
        mem_store8_raw(addr + 1, val >> 8);
}

static void mem_store32(uint32_t addr, uint32_t val)
{
    uint32_t pa;

    mem_fault = 0;
    mmu_fault_cause = 0;
    if (!(addr & 3u)) {
        if (!translate_addr(addr, ACCESS_STORE, &pa))
            return;
        if (!pmp_check(pa, 4, ACCESS_STORE)) {
            mem_fault = 1;
            return;
        }
        if (in_test(pa)) {
            test_finisher(val);
            return;
        }
        phys_store32(pa, val);
        return;
    }
    mem_store8_raw(addr, val & 0xff);
    if (!mem_fault && !mmu_fault_cause)
        mem_store8_raw(addr + 1, (val >> 8) & 0xff);
    if (!mem_fault && !mmu_fault_cause)
        mem_store8_raw(addr + 2, (val >> 16) & 0xff);
    if (!mem_fault && !mmu_fault_cause)
        mem_store8_raw(addr + 3, (val >> 24) & 0xff);
}

static uint64_t mem_load64(uint32_t addr)
{
    uint64_t lo, hi;

    lo = mem_load32(addr);
    if (mem_fault || mmu_fault_cause)
        return 0;
    hi = mem_load32(addr + 4);
    if (mem_fault || mmu_fault_cause)
        return 0;
    return lo | (hi << 32);
}

static void mem_store64(uint32_t addr, uint64_t val)
{
    mem_store32(addr, (uint32_t)val);
    if (mem_fault || mmu_fault_cause)
        return;
    mem_store32(addr + 4, (uint32_t)(val >> 32));
}

static void fp_dirty(void);

static int csr_valid(uint32_t csr)
{
    if ((csr >= 0x3a0u && csr <= 0x3a3u) ||
        (csr >= 0x3b0u && csr <= 0x3bfu))
        return 1;
    switch (csr) {
    case 0x001: case 0x002: case 0x003:
    case 0x100: case 0x104: case 0x105: case 0x106: case 0x140:
    case 0x141: case 0x142: case 0x143: case 0x144: case 0x146:
    case 0x180:
    case 0x300: case 0x301: case 0x302: case 0x303: case 0x304:
    case 0x305: case 0x306:
    case 0x310: case 0x340: case 0x341: case 0x342: case 0x343:
    case 0x344: case 0x346:
    case 0xc00: case 0xc01: case 0xc02: case 0xc80: case 0xc81: case 0xc82:
    case 0xf11: case 0xf12: case 0xf13: case 0xf14:
        return 1;
    default:
        return 0;
    }
}

static int csr_readonly(uint32_t csr)
{
    switch (csr) {
    case 0x142: case 0x143: case 0x301: case 0x342: case 0x343:
    case 0xc00: case 0xc01: case 0xc02: case 0xc80: case 0xc81: case 0xc82:
    case 0xf11: case 0xf12: case 0xf13: case 0xf14:
        return 1;
    default:
        return 0;
    }
}

static int csr_user_ok(uint32_t csr)
{
    switch (csr) {
    case 0x001: case 0x002: case 0x003:
    case 0xc00: case 0xc01: case 0xc02: case 0xc80: case 0xc81: case 0xc82:
        return 1;
    default:
        return 0;
    }
}

static int csr_user_writable(uint32_t csr)
{
    return csr >= 0x001u && csr <= 0x003u;
}

static int csr_min_priv(uint32_t csr)
{
    if (csr <= 0x003u || (csr >= 0xc00u && csr <= 0xc82u))
        return 0;
    if (csr >= 0x100u && csr <= 0x180u)
        return 1;
    return 3;
}

static int csr_user_enabled(uint32_t csr)
{
    if (!csr_user_ok(csr))
        return 0;
    if (csr >= 0xc00u && csr <= 0xc02u)
        return (scounteren >> (csr - 0xc00u)) & 1u;
    if (csr >= 0xc80u && csr <= 0xc82u)
        return (mcounteren >> (csr - 0xc80u)) & 1u;
    return 1;
}

static uint32_t csr_read(uint32_t csr)
{
    if (csr >= 0x3a0u && csr <= 0x3a3u)
        return pmpcfg[csr - 0x3a0u];
    if (csr >= 0x3b0u && csr <= 0x3bfu)
        return pmpaddr[csr - 0x3b0u];
    switch (csr) {
    case 0x001: return fcsr & 0x1fu;
    case 0x002: return (fcsr >> 5) & 7u;
    case 0x003: return fcsr & 0xffu;
    case 0x100: return sstatus;
    case 0x104: return sie & mideleg;
    case 0x105: return stvec;
    case 0x106: return scounteren;
    case 0x140: return sscratch;
    case 0x141: return sepc;
    case 0x142: return scause;
    case 0x143: return stval;
    case 0x144: return sip & mideleg;
    case 0x180: return satp;
    case 0x300: return mstatus;
    case 0x301: return 0x4000112du; /* RV32, IMACFD */
    case 0x302: return medeleg;
    case 0x303: return mideleg;
    case 0x304: return mie;
    case 0x305: return mtvec;
    case 0x306: return mcounteren;
    case 0x340: return mscratch;
    case 0x341: return mepc;
    case 0x342: return mcause;
    case 0x343: return mtval;
    case 0x344: return mip;
    case 0xc00: return (uint32_t)cycle;
    case 0xc80: return (uint32_t)(cycle >> 32);
    case 0xc01: update_time(); return (uint32_t)guest_time;
    case 0xc81: update_time(); return (uint32_t)(guest_time >> 32);
    case 0xc02: return (uint32_t)instret;
    case 0xc82: return (uint32_t)(instret >> 32);
    case 0xf11: return 0;
    case 0xf12: return 0;
    case 0xf13: return 0;
    case 0xf14: return 0;
    default: return 0;
    }
}

static void csr_write(uint32_t csr, uint32_t val)
{
    if (csr >= 0x3a0u && csr <= 0x3a3u) {
        unsigned base = (csr - 0x3a0u) * 4u;
        unsigned i;

        for (i = 0; i < 4u; i++) {
            if (pmp_locked(base + i))
                continue;
            pmpcfg[csr - 0x3a0u] &= ~(0xffu << (8u * i));
            pmpcfg[csr - 0x3a0u] |= (val & 0x9fu) << (8u * i);
            val >>= 8;
        }
        return;
    }
    if (csr >= 0x3b0u && csr <= 0x3bfu) {
        unsigned entry = csr - 0x3b0u;

        if (!pmp_locked(entry))
            pmpaddr[entry] = val;
        return;
    }
    switch (csr) {
    case 0x001:
        fcsr = (fcsr & 0xe0u) | (val & 0x1fu);
        fp_dirty();
        break;
    case 0x002:
        fcsr = (fcsr & 0x1fu) | ((val & 7u) << 5);
        fp_dirty();
        break;
    case 0x003:
        fcsr = val & 0xffu;
        fp_dirty();
        break;
    case 0x100:
        sstatus = val & SSTATUS_MASK;
        mstatus = (mstatus & ~SSTATUS_MASK) | sstatus;
        break;
    case 0x104: sie = val & S_INTERRUPT_MASK & mideleg; break;
    case 0x105: stvec = val & ~2u; break;
    case 0x106: scounteren = val & 7u; break;
    case 0x140: sscratch = val; break;
    case 0x141: sepc = val & ~1u; break;
    case 0x142: break;
    case 0x143: break;
    case 0x144:
        sip = (sip & SIP_STIP) | (val & ~SIP_STIP);
        break;
    case 0x180: {
        uint32_t old_satp = satp;

        if ((val & SATP_MODE_SV32) == 0)
            satp = 0;
        else
            satp = val & (SATP_MODE_SV32 | SATP_ASID_MASK | SATP_PPN_MASK);
        if ((satp ^ old_satp) & ~SATP_ASID_MASK)
            tlb_flush();
        reservation = ~0u;
        break;
    }
    case 0x300:
        mstatus = val & MSTATUS_MASK;
        sstatus = mstatus & SSTATUS_MASK;
        break;
    case 0x302: medeleg = val & MEDELEG_MASK; break;
    case 0x303: mideleg = val & MIDELEG_MASK; break;
    case 0x304: mie = val & INTERRUPT_MASK; break;
    case 0x305: mtvec = val & ~2u; break;
    case 0x306: mcounteren = val & 7u; break;
    case 0x340: mscratch = val; break;
    case 0x341: mepc = val & ~1u; break;
    case 0x342: break;
    case 0x343: break;
    case 0x344:
        mip = (mip & (SIP_STIP | MIP_MTIP)) | (val & INTERRUPT_MASK);
        break;
    default:
        break;
    }
}

static void do_m_trap(uint32_t cause, uint32_t tval, int interrupt,
                      uint32_t epc)
{
    uint32_t vec;
    uint32_t mpp = priv == 0 ? 0 : (uint32_t)priv << 11;

    reservation = ~0u;
    mstatus = (mstatus & ~(MSTATUS_MIE | MSTATUS_MPIE | MSTATUS_MPP)) |
              ((mstatus & MSTATUS_MIE) ? MSTATUS_MPIE : 0) | mpp;
    sstatus = mstatus & SSTATUS_MASK;
    priv = 3;
    mepc = epc;
    mcause = interrupt ? (cause | 0x80000000u) : cause;
    mtval = tval;
    vec = mtvec & ~3u;
    if (interrupt && (mtvec & 1u))
        vec += 4u * cause;
    pc = vec;
    if (vec == 0)
        fatal("M trap before mtvec was installed");
}

static void do_trap(uint32_t cause, uint32_t tval, int interrupt, uint32_t epc)
{
    uint32_t vec;
    uint32_t spp;
    uint32_t deleg = interrupt ? mideleg : medeleg;

    reservation = ~0u;
    if (priv == 3 || cause >= 32 || !(deleg & (1u << cause))) {
        do_m_trap(cause, tval, interrupt, epc);
        return;
    }
    spp = (priv == 0) ? 0 : MSTATUS_SPP;

    sstatus = (sstatus & ~(MSTATUS_SIE | MSTATUS_SPIE | MSTATUS_SPP)) |
              ((sstatus & MSTATUS_SIE) ? MSTATUS_SPIE : 0) | spp;
    mstatus = (mstatus & ~SSTATUS_MASK) | sstatus;
    priv = 1;
    sepc = epc;
    scause = interrupt ? (cause | 0x80000000u) : cause;
    stval = tval;
    vec = stvec & ~3u;
    if (interrupt && (stvec & 1u))
        vec += 4u * cause;
    pc = vec;
    if (vec == 0)
        fatal("trap before stvec was installed");
}

static void update_timer_irq(void)
{
    if ((instret & 0x3fu) == 0)
        update_time();

    if (timer_deadline != UINT64_MAX && guest_time >= timer_deadline)
        raise_timer_irq();
}

static uint32_t irq_cause(uint32_t ip, uint32_t ie, uint32_t *pending)
{
    if ((ip & MIP_MEIP) && (ie & MIE_MEIE)) {
        *pending = MIP_MEIP;
        return CAUSE_M_EXTERNAL;
    }
    if ((ip & MIP_MSIP) && (ie & MIE_MSIE)) {
        *pending = MIP_MSIP;
        return CAUSE_M_SOFTWARE;
    }
    if ((ip & MIP_MTIP) && (ie & MIE_MTIE)) {
        *pending = MIP_MTIP;
        return CAUSE_M_TIMER;
    }
    if ((ip & SIP_SEIP) && (ie & SIE_SEIE)) {
        *pending = SIP_SEIP;
        return CAUSE_EXT_S;
    }
    if ((ip & SIP_SSIP) && (ie & SIE_SSIE)) {
        *pending = SIP_SSIP;
        return CAUSE_SOFTWARE;
    }
    if ((ip & SIP_STIP) && (ie & SIE_STIE)) {
        *pending = SIP_STIP;
        return CAUSE_TIMER;
    }
    return 0;
}

static int interrupt_pending(void)
{
    uint32_t pending;

    update_timer_irq();
    if ((mstatus & MSTATUS_MIE) != 0 &&
        irq_cause(mip & ~mideleg, mie, &pending) != 0)
        return 1;
    if (priv != 3 && (sstatus & MSTATUS_SIE) != 0 &&
        irq_cause(sip & mideleg, sie & mideleg, &pending) != 0)
        return 1;
    return 0;
}

static int maybe_interrupt(void)
{
    uint32_t pending;
    uint32_t cause;

    update_timer_irq();
    if ((mstatus & MSTATUS_MIE) != 0) {
        cause = irq_cause(mip & ~mideleg, mie, &pending);
        if (cause) {
            if (pending != SIP_STIP && pending != MIP_MTIP) {
                mip &= ~pending;
                if (pending & S_INTERRUPT_MASK)
                    sip &= ~pending;
            }
            if (debug_sbi)
                fprintf(stderr, "irq cause=%u time=%" PRIu64 " pc=0x%08x\n",
                        cause, guest_time, pc);
            do_trap(cause, 0, 1, pc);
            return 1;
        }
    }
    if (priv != 3 && (sstatus & MSTATUS_SIE) != 0) {
        cause = irq_cause(sip & mideleg, sie & mideleg, &pending);
        if (cause) {
            if (pending != SIP_STIP) {
                sip &= ~pending;
                mip &= ~pending;
            }
            if (debug_sbi)
                fprintf(stderr, "irq cause=%u time=%" PRIu64 " pc=0x%08x\n",
                        cause, guest_time, pc);
            do_trap(cause, 0, 1, pc);
            return 1;
        }
    }
    return 0;
}

static void set_reg(uint32_t rd, uint32_t val)
{
    if (rd)
        regs[rd] = val;
}

static void fp_dirty(void)
{
    mstatus = (mstatus & ~MSTATUS_FS) | (3u << 13);
    sstatus = mstatus & SSTATUS_MASK;
}

static int32_t imm_i(uint32_t inst)
{
    return (int32_t)inst >> 20;
}

static int32_t imm_s(uint32_t inst)
{
    uint32_t v = ((inst >> 25) << 5) | ((inst >> 7) & 0x1fu);
    return (int32_t)(v << 20) >> 20;
}

static int32_t imm_b(uint32_t inst)
{
    uint32_t v = (((inst >> 31) & 1u) << 12) |
                 (((inst >> 7) & 1u) << 11) |
                 (((inst >> 25) & 0x3fu) << 5) |
                 (((inst >> 8) & 0x0fu) << 1);
    return (int32_t)(v << 19) >> 19;
}

static int32_t imm_j(uint32_t inst)
{
    uint32_t v = (((inst >> 31) & 1u) << 20) |
                 (((inst >> 12) & 0xffu) << 12) |
                 (((inst >> 20) & 1u) << 11) |
                 (((inst >> 21) & 0x3ffu) << 1);
    return (int32_t)(v << 11) >> 11;
}

static uint32_t imm_u(uint32_t inst)
{
    return inst & 0xfffff000u;
}

static uint32_t insn_i(uint32_t imm, uint32_t rs1, uint32_t funct3,
                       uint32_t rd, uint32_t op)
{
    return ((imm & 0xfffu) << 20) | (rs1 << 15) | (funct3 << 12) |
           (rd << 7) | op;
}

static uint32_t insn_r(uint32_t funct7, uint32_t rs2, uint32_t rs1,
                       uint32_t funct3, uint32_t rd, uint32_t op)
{
    return (funct7 << 25) | (rs2 << 20) | (rs1 << 15) | (funct3 << 12) |
           (rd << 7) | op;
}

static uint32_t insn_s(uint32_t imm, uint32_t rs2, uint32_t rs1,
                       uint32_t funct3)
{
    return (((imm >> 5) & 0x7fu) << 25) | (rs2 << 20) | (rs1 << 15) |
           (funct3 << 12) | ((imm & 0x1fu) << 7) | 0x23u;
}

static uint32_t insn_s_fp(uint32_t imm, uint32_t rs2, uint32_t rs1,
                          uint32_t funct3)
{
    return (((imm >> 5) & 0x7fu) << 25) | (rs2 << 20) | (rs1 << 15) |
           (funct3 << 12) | ((imm & 0x1fu) << 7) | 0x27u;
}

static uint32_t insn_b(int32_t imm, uint32_t rs2, uint32_t rs1,
                       uint32_t funct3)
{
    uint32_t v = (uint32_t)imm;

    return (((v >> 12) & 1u) << 31) | (((v >> 5) & 0x3fu) << 25) |
           (rs2 << 20) | (rs1 << 15) | (funct3 << 12) |
           (((v >> 1) & 0xfu) << 8) | (((v >> 11) & 1u) << 7) | 0x63u;
}

static uint32_t insn_j(int32_t imm, uint32_t rd)
{
    uint32_t v = (uint32_t)imm;

    return (((v >> 20) & 1u) << 31) | (((v >> 1) & 0x3ffu) << 21) |
           (((v >> 11) & 1u) << 20) | (((v >> 12) & 0xffu) << 12) |
           (rd << 7) | 0x6fu;
}

static uint32_t insn_u(uint32_t imm, uint32_t rd, uint32_t op)
{
    return (imm & 0xfffff000u) | (rd << 7) | op;
}

static float bits_f32(uint32_t bits)
{
    float v;

    memcpy(&v, &bits, sizeof(v));
    return v;
}

static uint32_t f32_bits(float v)
{
    uint32_t bits;

    memcpy(&bits, &v, sizeof(bits));
    return bits;
}

static double bits_f64(uint64_t bits)
{
    double v;

    memcpy(&v, &bits, sizeof(v));
    return v;
}

static uint64_t f64_bits(double v)
{
    uint64_t bits;

    memcpy(&bits, &v, sizeof(bits));
    return bits;
}

static int fp_begin(uint32_t rm)
{
    if (rm == 7)
        rm = (fcsr >> 5) & 7u;
    if (rm > 4)
        return -1;
    fp_rm = rm;
    feclearexcept(FE_ALL_EXCEPT);
    switch (rm) {
    case 0: case 4: fesetround(FE_TONEAREST); break;
    case 1: fesetround(FE_TOWARDZERO); break;
    case 2: fesetround(FE_DOWNWARD); break;
    case 3: fesetround(FE_UPWARD); break;
    }
    return 0;
}

static void fp_end(void)
{
    int e = fetestexcept(FE_INVALID | FE_DIVBYZERO | FE_OVERFLOW |
                         FE_UNDERFLOW | FE_INEXACT);

    if (e & FE_INVALID) fcsr |= 0x10u;
    if (e & FE_DIVBYZERO) fcsr |= 0x08u;
    if (e & FE_OVERFLOW) fcsr |= 0x04u;
    if (e & FE_UNDERFLOW) fcsr |= 0x02u;
    if (e & FE_INEXACT) fcsr |= 0x01u;
    fp_dirty();
    fesetround(FE_TONEAREST);
}

static uint32_t rmm_f32(float rn, __float128 x)
{
    uint32_t bits = f32_bits(rn);
    float up, down;
    __float128 lo, hi, mid, rv;

    if (!isfinite(rn) || !finiteq(x))
        return bits;
    rv = (__float128)rn;
    if (x == rv)
        return bits;
    up = nextafterf(rn, INFINITY);
    down = nextafterf(rn, -INFINITY);
    if (isinf(up) || isinf(down))
        return bits;
    if (x > rv) {
        lo = rv;
        hi = (__float128)up;
    } else {
        lo = (__float128)down;
        hi = rv;
    }
    mid = (lo + hi) / 2;
    if (x != mid)
        return bits;
    return f32_bits(x > 0 ? up : down);
}

static uint64_t rmm_f64(double rn, __float128 x)
{
    uint64_t bits = f64_bits(rn);
    double up, down;
    __float128 lo, hi, mid, rv;

    if (!isfinite(rn) || !finiteq(x))
        return bits;
    rv = (__float128)rn;
    if (x == rv)
        return bits;
    up = nextafter(rn, INFINITY);
    down = nextafter(rn, -INFINITY);
    if (isinf(up) || isinf(down))
        return bits;
    if (x > rv) {
        lo = rv;
        hi = (__float128)up;
    } else {
        lo = (__float128)down;
        hi = rv;
    }
    mid = (lo + hi) / 2;
    if (x != mid)
        return bits;
    return f64_bits(x > 0 ? up : down);
}

static uint32_t fp_f32_op(float a, float b, float rn, uint32_t funct7)
{
    __float128 aq, bq, x;

    if (fp_rm != 4)
        return f32_bits(rn);
    aq = a;
    bq = b;
    switch (funct7) {
    case 0x00: x = aq + bq; break;
    case 0x04: x = aq - bq; break;
    case 0x08: x = aq * bq; break;
    case 0x0c: x = aq / bq; break;
    case 0x2c: x = sqrtq(aq); break;
    default: return f32_bits(rn);
    }
    return rmm_f32(rn, x);
}

static uint64_t fp_f64_op(double a, double b, double rn, uint32_t funct7)
{
    __float128 aq, bq, x;

    if (fp_rm != 4)
        return f64_bits(rn);
    aq = a;
    bq = b;
    switch (funct7) {
    case 0x01: x = aq + bq; break;
    case 0x05: x = aq - bq; break;
    case 0x09: x = aq * bq; break;
    case 0x0d: x = aq / bq; break;
    case 0x2d: x = sqrtq(aq); break;
    default: return f64_bits(rn);
    }
    return rmm_f64(rn, x);
}

static uint32_t fp_f32_fma(float a, float b, float c, float rn, uint32_t opcode)
{
    __float128 x;

    if (fp_rm != 4)
        return f32_bits(rn);
    switch (opcode) {
    case 0x43: x = fmaq((__float128)a, (__float128)b, (__float128)c); break;
    case 0x47: x = fmaq((__float128)a, -(__float128)b, (__float128)c); break;
    case 0x4b: x = fmaq(-(__float128)a, (__float128)b, (__float128)c); break;
    case 0x4f: x = fmaq(-(__float128)a, -(__float128)b, (__float128)c); break;
    default: return f32_bits(rn);
    }
    return rmm_f32(rn, x);
}

static uint64_t fp_f64_fma(double a, double b, double c, double rn, uint32_t opcode)
{
    __float128 x;

    if (fp_rm != 4)
        return f64_bits(rn);
    switch (opcode) {
    case 0x43: x = fmaq((__float128)a, (__float128)b, (__float128)c); break;
    case 0x47: x = fmaq((__float128)a, -(__float128)b, (__float128)c); break;
    case 0x4b: x = fmaq(-(__float128)a, (__float128)b, (__float128)c); break;
    case 0x4f: x = fmaq(-(__float128)a, -(__float128)b, (__float128)c); break;
    default: return f64_bits(rn);
    }
    return rmm_f64(rn, x);
}

static uint32_t fp_f32_from_i32(int32_t v, float rn)
{
    return fp_rm == 4 ? rmm_f32(rn, (__float128)v) : f32_bits(rn);
}

static uint32_t fp_f32_from_u32(uint32_t v, float rn)
{
    return fp_rm == 4 ? rmm_f32(rn, (__float128)v) : f32_bits(rn);
}

static uint64_t fp_f64_from_i32(int32_t v, double rn)
{
    return fp_rm == 4 ? rmm_f64(rn, (__float128)v) : f64_bits(rn);
}

static uint64_t fp_f64_from_u32(uint32_t v, double rn)
{
    return fp_rm == 4 ? rmm_f64(rn, (__float128)v) : f64_bits(rn);
}

static uint32_t fp_f32_from_f64(double v, float rn)
{
    return fp_rm == 4 ? rmm_f32(rn, (__float128)v) : f32_bits(rn);
}

static int32_t c_sext(uint32_t val, unsigned bits)
{
    val &= (1u << bits) - 1u;
    return (int32_t)(val << (32 - bits)) >> (32 - bits);
}

static uint32_t c_ci_imm(uint16_t c)
{
    return (((uint32_t)c >> 12) & 1u) << 5 | (((uint32_t)c >> 2) & 0x1fu);
}

static uint32_t c_shamt(uint16_t c)
{
    return c_ci_imm(c);
}

static int32_t c_j_imm(uint16_t c)
{
    uint32_t v = (((uint32_t)c >> 12) & 1u) << 11 |
                 (((uint32_t)c >> 11) & 1u) << 4 |
                 (((uint32_t)c >> 9) & 3u) << 8 |
                 (((uint32_t)c >> 8) & 1u) << 10 |
                 (((uint32_t)c >> 7) & 1u) << 6 |
                 (((uint32_t)c >> 6) & 1u) << 7 |
                 (((uint32_t)c >> 3) & 7u) << 1 |
                 (((uint32_t)c >> 2) & 1u) << 5;

    return c_sext(v, 12);
}

static int32_t c_b_imm(uint16_t c)
{
    uint32_t v = (((uint32_t)c >> 12) & 1u) << 8 |
                 (((uint32_t)c >> 10) & 3u) << 3 |
                 (((uint32_t)c >> 5) & 3u) << 6 |
                 (((uint32_t)c >> 3) & 3u) << 1 |
                 (((uint32_t)c >> 2) & 1u) << 5;

    return c_sext(v, 9);
}

static uint32_t c_cl_imm(uint16_t c)
{
    return (((uint32_t)c >> 10) & 7u) << 3 |
           (((uint32_t)c >> 6) & 1u) << 2 |
           (((uint32_t)c >> 5) & 1u) << 6;
}

static uint32_t c_addi4spn_imm(uint16_t c)
{
    return (((uint32_t)c >> 12) & 1u) << 5 |
           (((uint32_t)c >> 11) & 1u) << 4 |
           (((uint32_t)c >> 7) & 0xfu) << 6 |
           (((uint32_t)c >> 6) & 1u) << 2 |
           (((uint32_t)c >> 5) & 1u) << 3;
}

static int32_t c_addi16sp_imm(uint16_t c)
{
    uint32_t v = (((uint32_t)c >> 12) & 1u) << 9 |
                 (((uint32_t)c >> 6) & 1u) << 4 |
                 (((uint32_t)c >> 5) & 1u) << 6 |
                 (((uint32_t)c >> 3) & 3u) << 7 |
                 (((uint32_t)c >> 2) & 1u) << 5;

    return c_sext(v, 10);
}

static int32_t c_lui_imm(uint16_t c)
{
    uint32_t v = (((uint32_t)c >> 12) & 1u) << 17 |
                 (((uint32_t)c >> 2) & 0x1fu) << 12;

    return c_sext(v, 18);
}

static uint32_t c_lwsp_imm(uint16_t c)
{
    return (((uint32_t)c >> 12) & 1u) << 5 |
           (((uint32_t)c >> 4) & 7u) << 2 |
           (((uint32_t)c >> 2) & 3u) << 6;
}

static uint32_t c_swsp_imm(uint16_t c)
{
    return (((uint32_t)c >> 9) & 0xfu) << 2 |
           (((uint32_t)c >> 7) & 3u) << 6;
}

static uint32_t c_fldsp_imm(uint16_t c)
{
    return (((uint32_t)c >> 12) & 1u) << 5 |
           (((uint32_t)c >> 5) & 3u) << 3 |
           (((uint32_t)c >> 2) & 7u) << 6;
}

static uint32_t c_fsdsp_imm(uint16_t c)
{
    return (((uint32_t)c >> 10) & 7u) << 3 |
           (((uint32_t)c >> 7) & 7u) << 6;
}

static int rv32c_expand(uint16_t c, uint32_t *out)
{
    uint32_t q = c & 3u;
    uint32_t f3 = ((uint32_t)c >> 13) & 7u;
    uint32_t rd = ((uint32_t)c >> 7) & 0x1fu;
    uint32_t rs2 = ((uint32_t)c >> 2) & 0x1fu;
    uint32_t rdp = 8u + (((uint32_t)c >> 2) & 7u);
    uint32_t rs1p = 8u + (((uint32_t)c >> 7) & 7u);
    uint32_t rs2p = 8u + (((uint32_t)c >> 2) & 7u);
    uint32_t uimm, shamt;
    int32_t imm;

    if (q == 3u)
        return 0;
    switch (q) {
    case 0:
        switch (f3) {
        case 0:
            uimm = c_addi4spn_imm(c);
            if (!uimm)
                return 0;
            *out = insn_i(uimm, 2, 0, rdp, 0x13u);
            return 1;
        case 1:
            *out = insn_i(c_cl_imm(c), rs1p, 3, rdp, 0x07u);
            return 1;
        case 2:
            *out = insn_i(c_cl_imm(c), rs1p, 2, rdp, 0x03u);
            return 1;
        case 3:
            *out = insn_i(c_cl_imm(c), rs1p, 2, rdp, 0x07u);
            return 1;
        case 5:
            *out = insn_s_fp(c_cl_imm(c), rs2p, rs1p, 3);
            return 1;
        case 6:
            *out = insn_s(c_cl_imm(c), rs2p, rs1p, 2);
            return 1;
        case 7:
            *out = insn_s_fp(c_cl_imm(c), rs2p, rs1p, 2);
            return 1;
        default:
            return 0;
        }
    case 1:
        switch (f3) {
        case 0:
            imm = c_sext(c_ci_imm(c), 6);
            *out = insn_i((uint32_t)imm, rd, 0, rd, 0x13u);
            return 1;
        case 1:
        case 5:
            *out = insn_j(c_j_imm(c), f3 == 1 ? 1 : 0);
            return 1;
        case 2:
            imm = c_sext(c_ci_imm(c), 6);
            *out = insn_i((uint32_t)imm, 0, 0, rd, 0x13u);
            return 1;
        case 3:
            if (rd == 2) {
                imm = c_addi16sp_imm(c);
                if (!imm)
                    return 0;
                *out = insn_i((uint32_t)imm, 2, 0, 2, 0x13u);
            } else {
                imm = c_lui_imm(c);
                if (!imm)
                    return 0;
                *out = insn_u((uint32_t)imm, rd, 0x37u);
            }
            return 1;
        case 4:
            shamt = c_shamt(c);
            switch ((c >> 10) & 3u) {
            case 0:
                if ((c >> 12) & 1u)
                    return 0;
                *out = insn_i(shamt, rs1p, 5, rs1p, 0x13u);
                return 1;
            case 1:
                if ((c >> 12) & 1u)
                    return 0;
                *out = insn_i((0x20u << 5) | shamt, rs1p, 5, rs1p, 0x13u);
                return 1;
            case 2:
                imm = c_sext(c_ci_imm(c), 6);
                *out = insn_i((uint32_t)imm, rs1p, 7, rs1p, 0x13u);
                return 1;
            case 3:
                if ((c >> 12) & 1u)
                    return 0;
                switch ((c >> 5) & 3u) {
                case 0:
                    *out = insn_r(0x20, rs2p, rs1p, 0, rs1p, 0x33u);
                    return 1;
                case 1:
                    *out = insn_r(0, rs2p, rs1p, 4, rs1p, 0x33u);
                    return 1;
                case 2:
                    *out = insn_r(0, rs2p, rs1p, 6, rs1p, 0x33u);
                    return 1;
                case 3:
                    *out = insn_r(0, rs2p, rs1p, 7, rs1p, 0x33u);
                    return 1;
                }
                return 0;
            }
            return 0;
        case 6:
        case 7:
            *out = insn_b(c_b_imm(c), 0, rs1p, f3 == 6 ? 0 : 1);
            return 1;
        }
        return 0;
    case 2:
        switch (f3) {
        case 0:
            if ((c >> 12) & 1u)
                return 0;
            *out = insn_i(c_shamt(c), rd, 1, rd, 0x13u);
            return 1;
        case 1:
            *out = insn_i(c_fldsp_imm(c), 2, 3, rd, 0x07u);
            return 1;
        case 2:
            if (!rd)
                return 0;
            *out = insn_i(c_lwsp_imm(c), 2, 2, rd, 0x03u);
            return 1;
        case 3:
            *out = insn_i(c_lwsp_imm(c), 2, 2, rd, 0x07u);
            return 1;
        case 4:
            if (!((c >> 12) & 1u)) {
                if (!rs2) {
                    if (!rd)
                        return 0;
                    *out = insn_i(0, rd, 0, 0, 0x67u);
                    return 1;
                }
                *out = insn_r(0, rs2, 0, 0, rd, 0x33u);
                return 1;
            }
            if (!rs2) {
                if (!rd) {
                    *out = 0x00100073u;
                    return 1;
                }
                *out = insn_i(0, rd, 0, 1, 0x67u);
                return 1;
            }
            *out = insn_r(0, rs2, rd, 0, rd, 0x33u);
            return 1;
        case 5:
            *out = insn_s_fp(c_fsdsp_imm(c), rs2, 2, 3);
            return 1;
        case 6:
            *out = insn_s(c_swsp_imm(c), rs2, 2, 2);
            return 1;
        case 7:
            *out = insn_s_fp(c_swsp_imm(c), rs2, 2, 2);
            return 1;
        default:
            return 0;
        }
    }
    return 0;
}

static uint32_t sbi_ext_supported(uint32_t eid)
{
    switch (eid) {
    case SBI_EXT_BASE:
    case SBI_EXT_TIME:
    case SBI_EXT_IPI:
    case SBI_EXT_RFENCE:
    case SBI_EXT_HSM:
    case SBI_EXT_SRST:
    case SBI_EXT_DBCN:
    case 0:
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
        return 1;
    default:
        return 0;
    }
}

static void sbi_shutdown(uint32_t type, uint32_t reason)
{
    if (!quiet)
        fprintf(stderr, "\ntern-linux: SBI shutdown type=%u reason=%u\n",
                type, reason);
    exit(0);
}

static void sbi_call(void)
{
    uint32_t eid = regs[17];
    uint32_t fid = regs[16];
    uint32_t a0 = regs[10], a1 = regs[11], a2 = regs[12];
    int32_t err = SBI_SUCCESS;
    if (debug_sbi)
        fprintf(stderr, "sbi eid=0x%08x fid=%u a0=0x%08x a1=0x%08x a2=0x%08x pc=0x%08x\n",
                eid, fid, a0, a1, a2, pc);
    uint32_t ret = 0;

    switch (eid) {
    case SBI_EXT_BASE:
        switch (fid) {
        case 0:
            ret = SBI_SPEC_VERSION;
            break;
        case 1:
            ret = SBI_IMPL_ID;
            break;
        case 2:
            ret = SBI_IMPL_VERSION;
            break;
        case 3:
            ret = sbi_ext_supported(a0) ? 1 : 0;
            break;
        case 4:
        case 5:
        case 6:
            ret = 0;
            break;
        default:
            err = SBI_ERR_NOT_SUPPORTED;
            break;
        }
        break;
    case SBI_EXT_TIME:
        if (fid == 0) {
            set_timer_deadline(((uint64_t)a1 << 32) | a0);
        } else {
            err = SBI_ERR_NOT_SUPPORTED;
        }
        break;
    case SBI_EXT_IPI:
    case SBI_EXT_RFENCE:
        break;
    case SBI_EXT_HSM:
        switch (fid) {
        case 0:
            err = SBI_ERR_INVALID_PARAM;
            break;
        case 1:
            sbi_shutdown(0, 0);
            break;
        case 2:
            ret = 0;
            break;
        case 3:
            break;
        default:
            err = SBI_ERR_NOT_SUPPORTED;
            break;
        }
        break;
    case SBI_EXT_SRST:
        if (fid != 0) {
            err = SBI_ERR_NOT_SUPPORTED;
        } else if (a0 > 2u || a1 > 1u) {
            err = SBI_ERR_INVALID_PARAM;
        } else if (a0 == 0) {
            sbi_shutdown(0, 0);
        } else {
            err = SBI_ERR_NOT_SUPPORTED;
        }
        break;
    case SBI_EXT_DBCN: {
        uint64_t addr = ((uint64_t)a2 << 32) | a1;
        uint8_t buf[256];
        uint32_t len = a0 > sizeof(buf) ? (uint32_t)sizeof(buf) : a0;
        uint32_t i;

        if (fid <= 1 && (a2 != 0u ||
                         addr + len > UINT64_C(0x100000000) ||
                         !phys_range_valid((uint32_t)addr, len))) {
            err = SBI_ERR_INVALID_ADDRESS;
            break;
        }
        switch (fid) {
        case 0:
            for (i = 0; i < len; i++) {
                mem_fault = 0;
                buf[i] = phys_load8((uint32_t)(addr + i));
                if (mem_fault) {
                    err = SBI_ERR_INVALID_ADDRESS;
                    mem_fault = 0;
                    break;
                }
            }
            console_write(buf, i);
            ret = i;
            break;
        case 1: {
            ssize_t n = console_try_read(buf, len);

            if (n > 0) {
                for (i = 0; i < (uint32_t)n; i++) {
                    mem_fault = 0;
                    phys_store8((uint32_t)(addr + i), buf[i]);
                    if (mem_fault) {
                        err = SBI_ERR_INVALID_ADDRESS;
                        mem_fault = 0;
                        break;
                    }
                }
                ret = i;
            }
            break;
        }
        case 2:
            console_putc((uint8_t)a0);
            break;
        default:
            err = SBI_ERR_NOT_SUPPORTED;
            break;
        }
        break;
    }
    default:
        switch (eid) {
        case 0:
            set_timer_deadline(((uint64_t)a1 << 32) | a0);
            break;
        case 1:
            console_putc((uint8_t)a0);
            break;
        case 2: {
            uint8_t c;
            ssize_t n = console_try_read(&c, 1);

            err = n > 0 ? (int32_t)c : -1;
            break;
        }
        case 3:
        case 4:
        case 5:
        case 6:
            break;
        case 7:
            sbi_shutdown(0, 0);
            break;
        default:
            err = SBI_ERR_NOT_SUPPORTED;
            break;
        }
        break;
    }

    regs[10] = (uint32_t)err;
    regs[11] = ret;
}

static uint32_t f32_snan(uint32_t bits)
{
    return (bits & 0x7fffffffu) > 0x7f800000u && !(bits & 0x00400000u);
}

static uint64_t f64_snan(uint64_t bits)
{
    return (bits & 0x7fffffffffffffffULL) > 0x7ff0000000000000ULL &&
           !(bits & 0x0008000000000000ULL);
}

static uint32_t fp_f32_minmax(uint32_t a, uint32_t b, int max)
{
    float fa = bits_f32(a);
    float fb = bits_f32(b);
    float r;

    if (isnan(fa) || isnan(fb)) {
        if (f32_snan(a) || f32_snan(b))
            feraiseexcept(FE_INVALID);
        if (isnan(fa) && isnan(fb))
            return 0x7fc00000u;
        return isnan(fa) ? b : a;
    }
    if (fa == 0.0f && fb == 0.0f) {
        if ((a & 0x80000000u) == (b & 0x80000000u))
            return a;
        return max ? (a & 0x80000000u ? b : a) :
                     (a & 0x80000000u ? a : b);
    }
    r = max ? fmaxf(fa, fb) : fminf(fa, fb);
    if (isnan(r))
        return 0x7fc00000u;
    return f32_bits(r);
}

static uint64_t fp_f64_minmax(uint64_t a, uint64_t b, int max)
{
    double fa = bits_f64(a);
    double fb = bits_f64(b);
    double r;

    if (isnan(fa) || isnan(fb)) {
        if (f64_snan(a) || f64_snan(b))
            feraiseexcept(FE_INVALID);
        if (isnan(fa) && isnan(fb))
            return 0x7ff8000000000000ULL;
        return isnan(fa) ? b : a;
    }
    if (fa == 0.0 && fb == 0.0) {
        if ((a & 0x8000000000000000ULL) == (b & 0x8000000000000000ULL))
            return a;
        return max ? (a & 0x8000000000000000ULL ? b : a) :
                     (a & 0x8000000000000000ULL ? a : b);
    }
    r = max ? fmax(fa, fb) : fmin(fa, fb);
    if (isnan(r))
        return 0x7ff8000000000000ULL;
    return f64_bits(r);
}

static uint32_t fp_fclass32(uint32_t bits)
{
    uint32_t exp = bits & 0x7f800000u;
    uint32_t frac = bits & 0x007fffffu;
    uint32_t sign = bits >> 31;

    if (exp == 0x7f800000u) {
        if (frac) return 1u << (frac & 0x00400000u ? 9 : 8);
        return 1u << (sign ? 0 : 7);
    }
    if (!exp) {
        if (frac) return 1u << (sign ? 2 : 5);
        return 1u << (sign ? 3 : 4);
    }
    return 1u << (sign ? 1 : 6);
}

static uint32_t fp_fclass64(uint64_t bits)
{
    uint64_t exp = bits & 0x7ff0000000000000ULL;
    uint64_t frac = bits & 0x000fffffffffffffULL;
    uint32_t sign = bits >> 63;

    if (exp == 0x7ff0000000000000ULL) {
        if (frac) return 1u << (frac & 0x0008000000000000ULL ? 9 : 8);
        return 1u << (sign ? 0 : 7);
    }
    if (!exp) {
        if (frac) return 1u << (sign ? 2 : 5);
        return 1u << (sign ? 3 : 4);
    }
    return 1u << (sign ? 1 : 6);
}

static float fp_rint_f32(float v)
{
    float r = rintf(v);
    float d;

    if (fp_rm != 4 || !isfinite(v))
        return r;
    d = v - r;
    if ((d == 0.5f && v > 0.0f) || (d == -0.5f && v < 0.0f))
        r = v > 0.0f ? r + 1.0f : r - 1.0f;
    return r;
}

static double fp_rint_f64(double v)
{
    double r = rint(v);
    double d;

    if (fp_rm != 4 || !isfinite(v))
        return r;
    d = v - r;
    if ((d == 0.5 && v > 0.0) || (d == -0.5 && v < 0.0))
        r = v > 0.0 ? r + 1.0 : r - 1.0;
    return r;
}

static int32_t fp_f32_to_i32(float v, uint32_t uns, uint32_t *invalid)
{
    float r = fp_rint_f32(v);

    if (isnan(v)) {
        *invalid = 1;
        return uns ? (int32_t)UINT32_MAX : INT32_MAX;
    }
    if (uns) {
        if (r < 0.0f || r >= 4294967296.0f) {
            *invalid = 1;
            return (int32_t)UINT32_MAX;
        }
        return (int32_t)(uint32_t)r;
    }
    if (r < -2147483648.0f || r >= 2147483648.0f) {
        *invalid = 1;
        return r < 0.0f ? INT32_MIN : INT32_MAX;
    }
    return (int32_t)r;
}

static int32_t fp_f64_to_i32(double v, uint32_t uns, uint32_t *invalid)
{
    double r = fp_rint_f64(v);

    if (isnan(v)) {
        *invalid = 1;
        return uns ? (int32_t)UINT32_MAX : INT32_MAX;
    }
    if (uns) {
        if (r < 0.0 || r >= 4294967296.0) {
            *invalid = 1;
            return (int32_t)UINT32_MAX;
        }
        return (int32_t)(uint32_t)r;
    }
    if (r < -2147483648.0 || r >= 2147483648.0) {
        *invalid = 1;
        return r < 0.0 ? INT32_MIN : INT32_MAX;
    }
    return (int32_t)r;
}

static void execute_fp(uint32_t inst, uint32_t cur_pc)
{
    uint32_t opcode = inst & 0x7fu;
    uint32_t rd = (inst >> 7) & 0x1fu;
    uint32_t funct3 = (inst >> 12) & 7u;
    uint32_t rs1 = (inst >> 15) & 0x1fu;
    uint32_t rs2 = (inst >> 20) & 0x1fu;
    uint32_t rs3 = (inst >> 27) & 0x1fu;
    uint32_t funct7 = inst >> 25;
    uint32_t fmt = (inst >> 25) & 3u;
    uint32_t addr, r, invalid;
    float fa, fb;
    double da, db;

    switch (opcode) {
    case 0x07:
        addr = regs[rs1] + (uint32_t)imm_i(inst);
        mem_fault = 0;
        mmu_fault_cause = 0;
        if (funct3 == 2) {
            r = mem_load32(addr);
            if (mem_fault || mmu_fault_cause) {
                do_trap(mmu_fault_cause ? mmu_fault_cause : CAUSE_LOAD,
                        addr, 0, cur_pc);
                return;
            }
            fpr[rd] = r;
        } else if (funct3 == 3) {
            uint64_t v = mem_load64(addr);

            if (mem_fault || mmu_fault_cause) {
                do_trap(mmu_fault_cause ? mmu_fault_cause : CAUSE_LOAD,
                        addr, 0, cur_pc);
                return;
            }
            fpr[rd] = v;
        } else {
            do_trap(CAUSE_ILLEGAL, inst, 0, cur_pc);
            return;
        }
        fp_dirty();
        return;
    case 0x27:
        addr = regs[rs1] + (uint32_t)imm_s(inst);
        mem_fault = 0;
        mmu_fault_cause = 0;
        if (funct3 == 2) {
            mem_store32(addr, (uint32_t)fpr[rs2]);
        } else if (funct3 == 3) {
            mem_store64(addr, fpr[rs2]);
        } else {
            do_trap(CAUSE_ILLEGAL, inst, 0, cur_pc);
            return;
        }
        reservation = ~0u;
        if (mem_fault || mmu_fault_cause)
            do_trap(mmu_fault_cause ? mmu_fault_cause : CAUSE_STORE,
                    addr, 0, cur_pc);
        return;
    case 0x43:
    case 0x47:
    case 0x4b:
    case 0x4f:
        if (fmt > 1 || fp_begin(funct3)) {
            do_trap(CAUSE_ILLEGAL, inst, 0, cur_pc);
            return;
        }
        if (!fmt) {
            float a = bits_f32(fpr[rs1]);
            float b = bits_f32(fpr[rs2]);
            float c = bits_f32(fpr[rs3]);
            float v = opcode == 0x43 ? fmaf(a, b, c) :
                      opcode == 0x47 ? fmaf(a, b, -c) :
                      opcode == 0x4b ? fmaf(-a, b, c) : fmaf(-a, b, -c);

            fpr[rd] = fp_f32_fma(a, b, c, v, opcode);
            if (isnan(v)) fpr[rd] = 0x7fc00000u;
        } else {
            double a = bits_f64(fpr[rs1]);
            double b = bits_f64(fpr[rs2]);
            double c = bits_f64(fpr[rs3]);
            double v = opcode == 0x43 ? fma(a, b, c) :
                       opcode == 0x47 ? fma(a, b, -c) :
                       opcode == 0x4b ? fma(-a, b, c) : fma(-a, b, -c);

            fpr[rd] = fp_f64_fma(a, b, c, v, opcode);
            if (isnan(v)) fpr[rd] = 0x7ff8000000000000ULL;
        }
        fp_end();
        return;
    case 0x53:
        break;
    default:
        do_trap(CAUSE_ILLEGAL, inst, 0, cur_pc);
        return;
    }

    switch (funct7) {
    case 0x00:
    case 0x01:
    case 0x04:
    case 0x05:
    case 0x08:
    case 0x09:
    case 0x0c:
    case 0x0d:
    case 0x2c:
    case 0x2d:
        if (funct7 & 1) {
            if ((funct7 == 0x2d) && rs2) break;
        } else if ((funct7 == 0x2c) && rs2) break;
        if (fp_begin(funct3)) break;
        if (!(funct7 & 1)) {
            float a = bits_f32(fpr[rs1]);
            float b = bits_f32(fpr[rs2]);

            fa = a;
            fb = b;
            switch (funct7) {
            case 0x00: fa += fb; break;
            case 0x04: fa -= fb; break;
            case 0x08: fa *= fb; break;
            case 0x0c: fa /= fb; break;
            case 0x2c: fa = sqrtf(fa); break;
            }
            fpr[rd] = fp_f32_op(a, b, fa, funct7);
            if (isnan(fa)) fpr[rd] = 0x7fc00000u;
        } else {
            double a = bits_f64(fpr[rs1]);
            double b = bits_f64(fpr[rs2]);

            da = a;
            db = b;
            switch (funct7) {
            case 0x01: da += db; break;
            case 0x05: da -= db; break;
            case 0x09: da *= db; break;
            case 0x0d: da /= db; break;
            case 0x2d: da = sqrt(da); break;
            }
            fpr[rd] = fp_f64_op(a, b, da, funct7);
            if (isnan(da)) fpr[rd] = 0x7ff8000000000000ULL;
        }
        fp_end();
        return;
    case 0x10:
    case 0x11:
        if (funct3 > 2) break;
        if (!(funct7 & 1)) {
            uint32_t a = (uint32_t)fpr[rs1];
            uint32_t b = (uint32_t)fpr[rs2];
            uint32_t s = funct3 == 0 ? b : funct3 == 1 ? ~b : a ^ b;

            fpr[rd] = (a & 0x7fffffffu) | (s & 0x80000000u);
        } else {
            uint64_t a = fpr[rs1];
            uint64_t b = fpr[rs2];
            uint64_t s = funct3 == 0 ? b : funct3 == 1 ? ~b : a ^ b;

            fpr[rd] = (a & 0x7fffffffffffffffULL) |
                      (s & 0x8000000000000000ULL);
        }
        fp_dirty();
        return;
    case 0x14:
    case 0x15:
        if (funct3 > 1 || fp_begin(0)) break;
        if (!(funct7 & 1))
            fpr[rd] = fp_f32_minmax((uint32_t)fpr[rs1],
                                    (uint32_t)fpr[rs2], funct3);
        else
            fpr[rd] = fp_f64_minmax(fpr[rs1], fpr[rs2], funct3);
        fp_end();
        return;
    case 0x20: {
        double src;

        if (rs2 != 1 || fp_begin(funct3)) break;
        src = bits_f64(fpr[rs1]);
        fa = (float)src;
        fpr[rd] = fp_f32_from_f64(src, fa);
        if (isnan(fa)) fpr[rd] = 0x7fc00000u;
        fp_end();
        return;
    }
    case 0x21:
        if (rs2 != 0 || fp_begin(funct3)) break;
        da = (double)bits_f32((uint32_t)fpr[rs1]);
        if (isnan(da)) da = bits_f64(0x7ff8000000000000ULL);
        fpr[rd] = f64_bits(da);
        fp_end();
        return;
    case 0x50:
    case 0x51:
        if (funct3 > 2 || fp_begin(0)) break;
        if (!(funct7 & 1)) {
            uint32_t a = (uint32_t)fpr[rs1];
            uint32_t b = (uint32_t)fpr[rs2];

            fa = bits_f32(a);
            fb = bits_f32(b);
            if (funct3 == 2) {
                if (f32_snan(a) || f32_snan(b)) feraiseexcept(FE_INVALID);
                r = !isnan(fa) && !isnan(fb) && fa == fb;
            } else {
                if (isnan(fa) || isnan(fb)) feraiseexcept(FE_INVALID);
                r = !isnan(fa) && !isnan(fb) &&
                    (funct3 == 1 ? fa < fb : fa <= fb);
            }
        } else {
            da = bits_f64(fpr[rs1]);
            db = bits_f64(fpr[rs2]);
            if (funct3 == 2) {
                if (f64_snan(fpr[rs1]) || f64_snan(fpr[rs2]))
                    feraiseexcept(FE_INVALID);
                r = !isnan(da) && !isnan(db) && da == db;
            } else {
                if (isnan(da) || isnan(db)) feraiseexcept(FE_INVALID);
                r = !isnan(da) && !isnan(db) &&
                    (funct3 == 1 ? da < db : da <= db);
            }
        }
        set_reg(rd, r);
        fp_end();
        return;
    case 0x60:
    case 0x61:
        if (rs2 > 1 || fp_begin(funct3)) break;
        invalid = 0;
        if (!(funct7 & 1))
            r = (uint32_t)fp_f32_to_i32(bits_f32((uint32_t)fpr[rs1]),
                                        rs2, &invalid);
        else
            r = (uint32_t)fp_f64_to_i32(bits_f64(fpr[rs1]), rs2, &invalid);
        if (invalid) feraiseexcept(FE_INVALID);
        set_reg(rd, r);
        fp_end();
        return;
    case 0x68:
    case 0x69:
        if (rs2 > 1 || fp_begin(funct3)) break;
        if (!(funct7 & 1)) {
            if (rs2 == 0)
                fa = (float)(int32_t)regs[rs1];
            else
                fa = (float)regs[rs1];
            fpr[rd] = rs2 == 0 ? fp_f32_from_i32((int32_t)regs[rs1], fa) :
                                 fp_f32_from_u32(regs[rs1], fa);
        } else {
            if (rs2 == 0)
                da = (double)(int32_t)regs[rs1];
            else
                da = (double)regs[rs1];
            fpr[rd] = rs2 == 0 ? fp_f64_from_i32((int32_t)regs[rs1], da) :
                                 fp_f64_from_u32(regs[rs1], da);
        }
        fp_end();
        return;
    case 0x70:
        if (rs2 || funct3 > 1) break;
        set_reg(rd, funct3 == 1 ? fp_fclass32((uint32_t)fpr[rs1]) :
                                  (uint32_t)fpr[rs1]);
        fp_dirty();
        return;
    case 0x71:
        if (rs2 || funct3 > 1) break;
        set_reg(rd, funct3 == 1 ? fp_fclass64(fpr[rs1]) :
                                  (uint32_t)fpr[rs1]);
        fp_dirty();
        return;
    case 0x78:
        if (rs2 || funct3) break;
        fpr[rd] = regs[rs1];
        fp_dirty();
        return;
    case 0x79:
        if (rs2 || funct3) break;
        fpr[rd] = regs[rs1];
        fp_dirty();
        return;
    default:
        break;
    }
    do_trap(CAUSE_ILLEGAL, inst, 0, cur_pc);
}

static void execute(uint32_t inst, uint32_t cur_pc)
{
    uint32_t opcode = inst & 0x7fu;
    uint32_t rd = (inst >> 7) & 0x1fu;
    uint32_t funct3 = (inst >> 12) & 7u;
    uint32_t rs1 = (inst >> 15) & 0x1fu;
    uint32_t rs2 = (inst >> 20) & 0x1fu;
    uint32_t funct7 = inst >> 25;
    uint32_t addr, val, old;
    int32_t simm;
    int take;

    switch (opcode) {
    case 0x07:
    case 0x27:
    case 0x43:
    case 0x47:
    case 0x4b:
    case 0x4f:
    case 0x53:
        execute_fp(inst, cur_pc);
        return;
    case 0x37: /* LUI */
        set_reg(rd, imm_u(inst));
        break;
    case 0x17: /* AUIPC */
        set_reg(rd, cur_pc + imm_u(inst));
        break;
    case 0x6f: /* JAL */
        set_reg(rd, pc);
        pc = cur_pc + (uint32_t)imm_j(inst);
        break;
    case 0x67: /* JALR */
        addr = (regs[rs1] + (uint32_t)imm_i(inst)) & ~1u;
        set_reg(rd, pc);
        pc = addr;
        break;
    case 0x63: /* branches */
        take = 0;
        switch (funct3) {
        case 0: take = regs[rs1] == regs[rs2]; break;
        case 1: take = regs[rs1] != regs[rs2]; break;
        case 4: take = (int32_t)regs[rs1] < (int32_t)regs[rs2]; break;
        case 5: take = (int32_t)regs[rs1] >= (int32_t)regs[rs2]; break;
        case 6: take = regs[rs1] < regs[rs2]; break;
        case 7: take = regs[rs1] >= regs[rs2]; break;
        default:
            do_trap(CAUSE_ILLEGAL, inst, 0, cur_pc);
            return;
        }
        if (take)
            pc = cur_pc + (uint32_t)imm_b(inst);
        break;
    case 0x03: /* loads */
        addr = regs[rs1] + (uint32_t)imm_i(inst);
        mem_fault = 0;
        mmu_fault_cause = 0;
        switch (funct3) {
        case 0:
            val = (uint32_t)(int32_t)(int8_t)mem_load8(addr);
            break;
        case 1:
            val = (uint32_t)(int32_t)(int16_t)mem_load16(addr);
            break;
        case 2:
            val = mem_load32(addr);
            break;
        case 4:
            val = mem_load8(addr);
            break;
        case 5:
            val = mem_load16(addr);
            break;
        default:
            do_trap(CAUSE_ILLEGAL, inst, 0, cur_pc);
            return;
        }
        if (mem_fault || mmu_fault_cause)
            do_trap(mmu_fault_cause ? mmu_fault_cause : CAUSE_LOAD,
                    addr, 0, cur_pc);
        else
            set_reg(rd, val);
        break;
    case 0x23: /* stores */
        addr = regs[rs1] + (uint32_t)imm_s(inst);
        mem_fault = 0;
        mmu_fault_cause = 0;
        switch (funct3) {
        case 0: mem_store8(addr, (uint8_t)regs[rs2]); break;
        case 1: mem_store16(addr, (uint16_t)regs[rs2]); break;
        case 2: mem_store32(addr, regs[rs2]); break;
        default:
            do_trap(CAUSE_ILLEGAL, inst, 0, cur_pc);
            return;
        }
        reservation = ~0u;
        if (mem_fault || mmu_fault_cause)
            do_trap(mmu_fault_cause ? mmu_fault_cause : CAUSE_STORE,
                    addr, 0, cur_pc);
        break;
    case 0x13: /* OP-IMM */
        simm = imm_i(inst);
        switch (funct3) {
        case 0: set_reg(rd, regs[rs1] + (uint32_t)simm); break;
        case 1:
            if (funct7 != 0) {
                do_trap(CAUSE_ILLEGAL, inst, 0, cur_pc);
                return;
            }
            set_reg(rd, regs[rs1] << ((inst >> 20) & 0x1fu));
            break;
        case 2:
            set_reg(rd, (int32_t)regs[rs1] < simm ? 1u : 0u);
            break;
        case 3:
            set_reg(rd, regs[rs1] < (uint32_t)simm ? 1u : 0u);
            break;
        case 4: set_reg(rd, regs[rs1] ^ (uint32_t)simm); break;
        case 5:
            if (funct7 == 0)
                set_reg(rd, regs[rs1] >> ((inst >> 20) & 0x1fu));
            else if (funct7 == 0x20)
                set_reg(rd, (uint32_t)((int32_t)regs[rs1] >> ((inst >> 20) & 0x1fu)));
            else
                do_trap(CAUSE_ILLEGAL, inst, 0, cur_pc);
            break;
        case 6: set_reg(rd, regs[rs1] | (uint32_t)simm); break;
        case 7: set_reg(rd, regs[rs1] & (uint32_t)simm); break;
        }
        break;
    case 0x33: /* OP */
        if (funct7 == 0x01) {
            int32_t sa = (int32_t)regs[rs1], sb = (int32_t)regs[rs2];
            uint64_t ua = regs[rs1], ub = regs[rs2];
            switch (funct3) {
            case 0: set_reg(rd, (uint32_t)((int64_t)sa * sb)); break;
            case 1:
                set_reg(rd, (uint32_t)(((int64_t)sa * sb) >> 32));
                break;
            case 2:
                set_reg(rd, (uint32_t)(((int64_t)sa * (int64_t)ub) >> 32));
                break;
            case 3:
                set_reg(rd, (uint32_t)((ua * ub) >> 32));
                break;
            case 4:
                if (sb == 0)
                    val = 0xffffffffu;
                else if (sa == INT32_MIN && sb == -1)
                    val = (uint32_t)INT32_MIN;
                else
                    val = (uint32_t)(sa / sb);
                set_reg(rd, val);
                break;
            case 5:
                set_reg(rd, sb == 0 ? 0xffffffffu : ua / ub);
                break;
            case 6:
                if (sb == 0)
                    val = (uint32_t)sa;
                else if (sa == INT32_MIN && sb == -1)
                    val = 0;
                else
                    val = (uint32_t)(sa % sb);
                set_reg(rd, val);
                break;
            case 7:
                set_reg(rd, sb == 0 ? ua : ua % ub);
                break;
            }
            break;
        }
        switch (funct3) {
        case 0:
            if (funct7 == 0)
                set_reg(rd, regs[rs1] + regs[rs2]);
            else if (funct7 == 0x20)
                set_reg(rd, regs[rs1] - regs[rs2]);
            else
                do_trap(CAUSE_ILLEGAL, inst, 0, cur_pc);
            break;
        case 1:
            if (funct7 == 0)
                set_reg(rd, regs[rs1] << (regs[rs2] & 0x1fu));
            else
                do_trap(CAUSE_ILLEGAL, inst, 0, cur_pc);
            break;
        case 2:
            if (funct7 == 0)
                set_reg(rd, (int32_t)regs[rs1] < (int32_t)regs[rs2] ? 1u : 0u);
            else
                do_trap(CAUSE_ILLEGAL, inst, 0, cur_pc);
            break;
        case 3:
            if (funct7 == 0)
                set_reg(rd, regs[rs1] < regs[rs2] ? 1u : 0u);
            else
                do_trap(CAUSE_ILLEGAL, inst, 0, cur_pc);
            break;
        case 4:
            if (funct7 == 0)
                set_reg(rd, regs[rs1] ^ regs[rs2]);
            else
                do_trap(CAUSE_ILLEGAL, inst, 0, cur_pc);
            break;
        case 5:
            if (funct7 == 0)
                set_reg(rd, regs[rs1] >> (regs[rs2] & 0x1fu));
            else if (funct7 == 0x20)
                set_reg(rd, (uint32_t)((int32_t)regs[rs1] >> (regs[rs2] & 0x1fu)));
            else
                do_trap(CAUSE_ILLEGAL, inst, 0, cur_pc);
            break;
        case 6:
            if (funct7 == 0)
                set_reg(rd, regs[rs1] | regs[rs2]);
            else
                do_trap(CAUSE_ILLEGAL, inst, 0, cur_pc);
            break;
        case 7:
            if (funct7 == 0)
                set_reg(rd, regs[rs1] & regs[rs2]);
            else
                do_trap(CAUSE_ILLEGAL, inst, 0, cur_pc);
            break;
        }
        break;
    case 0x0f: /* FENCE, FENCE.I */
        break;
    case 0x2f: /* A extension */
        if (funct3 != 2) {
            do_trap(CAUSE_ILLEGAL, inst, 0, cur_pc);
            return;
        }
        addr = regs[rs1];
        if (addr & 3u) {
            do_trap(CAUSE_MISALIGNED, addr, 0, cur_pc);
            return;
        }
        if (((inst >> 27) == 0x02u && ((inst >> 25) & 3u) == 1u) ||
            ((inst >> 27) == 0x03u && ((inst >> 25) & 3u) == 2u)) {
            do_trap(CAUSE_ILLEGAL, inst, 0, cur_pc);
            return;
        }
        switch (inst >> 27) {
        case 0x02: { /* lr.w */
            uint32_t pa, cause;

            if (!amo_translate(addr, ACCESS_LOAD, &pa, &cause)) {
                do_trap(cause, addr, 0, cur_pc);
                return;
            }
            reservation = pa;
            set_reg(rd, phys_load32(pa));
            break;
        }
        case 0x03: { /* sc.w */
            uint32_t pa, cause;

            if (!amo_translate(addr, ACCESS_STORE, &pa, &cause)) {
                do_trap(cause, addr, 0, cur_pc);
                return;
            }
            if (reservation == pa) {
                mem_fault = 0;
                mmu_fault_cause = 0;
                mem_store32(addr, regs[rs2]);
                if (mem_fault || mmu_fault_cause) {
                    do_trap(mmu_fault_cause ? mmu_fault_cause : CAUSE_STORE,
                            addr, 0, cur_pc);
                    return;
                }
                set_reg(rd, 0);
            } else {
                set_reg(rd, 1);
            }
            reservation = ~0u;
            break;
        }
        default: {
            uint32_t pa, cause, src = regs[rs2];

            switch (inst >> 27) {
            case 0x00: case 0x01: case 0x04: case 0x08: case 0x0c:
            case 0x10: case 0x14: case 0x18: case 0x1c:
                break;
            default:
                do_trap(CAUSE_ILLEGAL, inst, 0, cur_pc);
                return;
            }
            if (!amo_store_translate(addr, &pa, &cause)) {
                do_trap(cause, addr, 0, cur_pc);
                return;
            }
            old = phys_load32(pa);
            val = 0;
            switch (inst >> 27) {
            case 0x00: val = old + src; break;
            case 0x04: val = old ^ src; break;
            case 0x0c: val = old & src; break;
            case 0x08: val = old | src; break;
            case 0x10: val = (int32_t)old < (int32_t)src ? old : src; break;
            case 0x14: val = (int32_t)old > (int32_t)src ? old : src; break;
            case 0x18: val = old < src ? old : src; break;
            case 0x1c: val = old > src ? old : src; break;
            case 0x01: val = src; break;
            }
            mem_fault = 0;
            mmu_fault_cause = 0;
            mem_store32(addr, val);
            if (mem_fault || mmu_fault_cause) {
                do_trap(mmu_fault_cause ? mmu_fault_cause : CAUSE_STORE,
                        addr, 0, cur_pc);
                return;
            }
            reservation = ~0u;
            set_reg(rd, old);
            break;
        }
        }
        break;
    case 0x73: /* SYSTEM */
        if (funct3 == 0) {
            switch (inst) {
            case 0x00000073: /* ecall */
                if (priv == 0)
                    do_trap(CAUSE_ECALL_U, 0, 0, cur_pc);
                else
                    sbi_call();
                break;
            case 0x00100073: /* ebreak */
                do_trap(CAUSE_BREAK, 0, 0, cur_pc);
                break;
            case 0x10200073: /* sret */
                if (priv != 1) {
                    do_trap(CAUSE_ILLEGAL, inst, 0, cur_pc);
                    break;
                }
                pc = sepc;
                priv = (sstatus & MSTATUS_SPP) ? 1 : 0;
                sstatus = (sstatus & ~MSTATUS_SIE) |
                          ((sstatus & MSTATUS_SPIE) ? MSTATUS_SIE : 0);
                sstatus |= MSTATUS_SPIE;
                sstatus &= ~MSTATUS_SPP;
                mstatus = (mstatus & ~SSTATUS_MASK) | sstatus;
                break;
            case 0x30200073: /* mret */
                if (priv != 3 || ((mstatus & MSTATUS_MPP) >> 11) == 2) {
                    do_trap(CAUSE_ILLEGAL, inst, 0, cur_pc);
                    break;
                }
                pc = mepc;
                priv = (mstatus & MSTATUS_MPP) >> 11;
                mstatus = (mstatus & ~(MSTATUS_MIE | MSTATUS_MPIE |
                                       MSTATUS_MPP)) |
                          ((mstatus & MSTATUS_MPIE) ? MSTATUS_MIE : 0) |
                          MSTATUS_MPIE;
                sstatus = mstatus & SSTATUS_MASK;
                break;
            case 0x10500073: /* wfi */
                update_time();
                if (sleep_wfi && !interrupt_pending()) {
                    struct timespec ts = { .tv_sec = 0, .tv_nsec = 100000 };
                    nanosleep(&ts, NULL);
                }
                break;
            default:
                if ((inst & 0xfe007fffu) == 0x12000073u) {
                    unsigned rs1 = (inst >> 15) & 0x1fu;
                    unsigned rs2 = (inst >> 20) & 0x1fu;

                    if (priv == 0) {
                        do_trap(CAUSE_ILLEGAL, inst, 0, cur_pc);
                        break;
                    }
                    tlb_invalidate(rs1 ? regs[rs1] : 0, rs1 != 0,
                                   rs2 ? regs[rs2] : 0, rs2 != 0);
                    reservation = ~0u;
                    break;
                }
                do_trap(CAUSE_ILLEGAL, inst, 0, cur_pc);
                break;
            }
            break;
        }
        if (funct3 == 1 || funct3 == 2 || funct3 == 3 ||
            funct3 == 5 || funct3 == 6 || funct3 == 7) {
            uint32_t csr = inst >> 20;
            uint32_t src = (funct3 & 4u) ? rs1 : regs[rs1];
            int write;

            old = csr_read(csr);
            switch (funct3) {
            case 1:
            case 5:
                val = src;
                write = 1;
                break;
            case 2:
            case 6:
                val = old | src;
                write = src != 0;
                break;
            default:
                val = old & ~src;
                write = src != 0;
                break;
            }
            if (!csr_valid(csr) || priv < csr_min_priv(csr) ||
                (write && csr_readonly(csr)) ||
                (priv == 0 && (!csr_user_enabled(csr) ||
                               (write && !csr_user_writable(csr))))) {
                do_trap(CAUSE_ILLEGAL, inst, 0, cur_pc);
                break;
            }
            if (write)
                csr_write(csr, val);
            set_reg(rd, old);
            break;
        }
        do_trap(CAUSE_ILLEGAL, inst, 0, cur_pc);
        break;
    default:
        do_trap(CAUSE_ILLEGAL, inst, 0, cur_pc);
        break;
    }
}

static void fetch_fault(uint32_t cause, uint32_t tval)
{
    if (debug_mmu)
        fprintf(stderr, "mmu: fetch trap pc=0x%08x cause=%u stvec=0x%08x\n",
                pc, cause, stvec);
    do_trap(cause, tval, 0, pc);
    if (debug_mmu)
        fprintf(stderr, "mmu: redirected pc=0x%08x\n", pc);
}

static int fetch_insn(uint32_t *inst, unsigned *len)
{
    uint32_t lo_pa, hi_pa;
    uint16_t lo, hi;

    mem_fault = 0;
    mmu_fault_cause = 0;
    if (!translate_addr(pc, ACCESS_FETCH, &lo_pa) ||
        !phys_in_ram_n(lo_pa, 2)) {
        fetch_fault(mmu_fault_cause ? mmu_fault_cause : CAUSE_INST_PF, pc);
        return 0;
    }
    if (!pmp_check(lo_pa, 2, ACCESS_FETCH)) {
        fetch_fault(CAUSE_INST_FAULT, pc);
        return 0;
    }
    lo = (uint16_t)(phys_load8(lo_pa) |
                    ((uint16_t)phys_load8(lo_pa + 1) << 8));
    if (mem_fault) {
        fetch_fault(CAUSE_INST_PF, pc);
        return 0;
    }
    if ((lo & 3u) != 3u) {
        if (!rv32c_expand(lo, inst)) {
            fetch_fault(CAUSE_ILLEGAL, lo);
            return 0;
        }
        *len = 2;
        return 1;
    }
    if (!translate_addr(pc + 2, ACCESS_FETCH, &hi_pa) ||
        !phys_in_ram_n(hi_pa, 2)) {
        fetch_fault(mmu_fault_cause ? mmu_fault_cause : CAUSE_INST_PF, pc);
        return 0;
    }
    if (!pmp_check(hi_pa, 2, ACCESS_FETCH)) {
        fetch_fault(CAUSE_INST_FAULT, pc);
        return 0;
    }
    hi = (uint16_t)(phys_load8(hi_pa) |
                    ((uint16_t)phys_load8(hi_pa + 1) << 8));
    if (mem_fault) {
        fetch_fault(CAUSE_INST_PF, pc);
        return 0;
    }
    *inst = (uint32_t)lo | ((uint32_t)hi << 16);
    *len = 4;
    return 1;
}

struct BootImage {
    const char *path;
    uint32_t addr;
    FILE *file;
    size_t size;
};

static int open_boot_image(struct BootImage *image)
{
    long size;

    image->file = fopen(image->path, "rb");
    if (!image->file) {
        fprintf(stderr, "tern-linux: cannot open %s: %s\n", image->path, strerror(errno));
        return -1;
    }
    if (fseek(image->file, 0, SEEK_END) != 0 ||
        (size = ftell(image->file)) < 0 || fseek(image->file, 0, SEEK_SET) != 0) {
        fprintf(stderr, "tern-linux: cannot determine size of %s: %s\n",
                image->path, strerror(errno));
        return -1;
    }
    if (image->addr < RAM_BASE || image->addr >= RAM_BASE + RAM_SIZE ||
        (uint64_t)size > RAM_BASE + RAM_SIZE - image->addr) {
        fprintf(stderr, "tern-linux: %s does not fit in RAM at 0x%08x\n",
                image->path, image->addr);
        return -1;
    }
    image->size = (size_t)size;
    return 0;
}

static int load_boot_images(struct BootImage *images, size_t count)
{
    int result = -1;

    for (size_t i = 0; i < count; i++) {
        struct BootImage *a = &images[i];

        if (open_boot_image(a) != 0)
            goto out;
        for (size_t j = 0; j < i; j++) {
            const struct BootImage *b = &images[j];

            if (a->size && b->size && a->addr < b->addr + b->size &&
                b->addr < a->addr + a->size) {
                fprintf(stderr, "tern-linux: %s at 0x%08x overlaps %s at 0x%08x\n",
                        a->path, a->addr, b->path, b->addr);
                goto out;
            }
        }
    }
    ram = calloc(1, RAM_SIZE);
    if (!ram) {
        fprintf(stderr, "tern-linux: cannot allocate %u bytes of RAM\n", RAM_SIZE);
        goto out;
    }
    for (size_t i = 0; i < count; i++) {
        const struct BootImage *image = &images[i];

        if (fread(ram + (image->addr - RAM_BASE), 1, image->size, image->file) != image->size) {
            fprintf(stderr, "tern-linux: short read of %s\n", image->path);
            free(ram);
            ram = NULL;
            goto out;
        }
    }
    result = 0;
out:
    for (size_t i = 0; i < count; i++) {
        if (images[i].file)
            fclose(images[i].file);
    }
    return result;
}

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static void put_be32(uint8_t *p, uint32_t v)
{
    p[0] = v >> 24;
    p[1] = v >> 16;
    p[2] = v >> 8;
    p[3] = v;
}

static void fdt_patch_u32(uint32_t dtb, size_t dtb_size, const char *prop, uint32_t value)
{
    uint8_t *base = ram + (dtb - RAM_BASE);
    uint32_t total, off_struct, off_strings, pos, end;
    uint32_t len, nameoff;
    const char *name;

    if (dtb_size < 40) {
        if (debug_sbi)
            fprintf(stderr, "fdt: truncated header at 0x%08x\n", dtb);
        return;
    }
    if (be32(base) != FDT_MAGIC) {
        if (debug_sbi)
            fprintf(stderr, "fdt: bad magic 0x%08x\n", be32(base));
        return;
    }
    total = be32(base + 4);
    off_struct = be32(base + 8);
    off_strings = be32(base + 12);
    if (total > dtb_size || off_struct >= total || off_strings >= total)
        return;
    pos = off_struct;
    end = total;
    while (pos + 4 <= end) {
        uint32_t token = be32(base + pos);
        pos += 4;
        if (token == FDT_BEGIN_NODE) {
            while (pos < end && base[pos] != 0)
                pos++;
            pos++;
            pos = (pos + 3u) & ~3u;
        } else if (token == FDT_END_NODE || token == FDT_NOP) {
            continue;
        } else if (token == FDT_PROP) {
            if (pos + 8 > end)
                return;
            len = be32(base + pos);
            nameoff = be32(base + pos + 4);
            pos += 8;
            if (len > end - pos)
                return;
            if (nameoff < end - off_strings &&
                memchr(base + off_strings + nameoff, '\0', end - off_strings - nameoff)) {
                name = (const char *)(base + off_strings + nameoff);
                if (debug_sbi && !strcmp(name, prop))
                    fprintf(stderr, "fdt: patch %s=0x%x\n", prop, value);
                if (len == 4 && !strcmp(name, prop))
                    put_be32(base + pos, value);
            }
            pos = (pos + len + 3u) & ~3u;
        } else if (token == FDT_END) {
            return;
        } else {
            return;
        }
    }
}

static void usage(const char *prog)
{
    fprintf(stderr,
            "Usage: %s --kernel Image [options]\n"
            "  --kernel-addr ADDR  load kernel at physical address (default 0x%08x)\n"
            "  --dtb FILE          load DTB at 0x%08x\n"
            "  --initrd FILE       load initramfs at 0x%08x\n"
            "  --dtb-addr ADDR     override the DTB load address\n"
            "  --initrd-addr ADDR  override the initramfs load address\n"
            "  --load ADDR FILE    load raw file at physical address\n"
            "  --pc ADDR           override entry point (default 0x%08x)\n"
            "  --max-insn N        stop after N instructions (0 = none)\n"
            "  --trace N           trace first N instructions\n"
            "  --sample N          print pc/ra every N instructions\n"
            "  --timebase HZ       guest timebase (default %u)\n"
            "  --time-inst         derive guest time from instruction count\n"
             "  --no-sleep          do not sleep in wfi\n"
             "  --debug-sbi         log SBI calls\n"
             "  --debug-mmu         log Sv32 translation faults and satp writes\n"
             "  --stats             print instruction and MMU counters at exit\n"
             "  --quiet             suppress emulator diagnostics\n",
            prog, KERNEL_ADDR, DTB_ADDR, INITRD_ADDR, KERNEL_ADDR, timebase);
}

static int parse_boot_addr(const char *arg, uint32_t *addr)
{
    char *end;
    unsigned long long value;

    errno = 0;
    value = strtoull(arg, &end, 0);
    if (errno || arg[0] < '0' || arg[0] > '9' || *end || value > UINT32_MAX) {
        fprintf(stderr, "tern-linux: invalid address: %s\n", arg);
        return -1;
    }
    *addr = (uint32_t)value;
    return 0;
}

int main(int argc, char **argv)
{
    const char *kernel = NULL;
    const char *dtb = NULL;
    const char *initrd = NULL;
    uint32_t dtb_addr = DTB_ADDR;
    uint32_t initrd_addr = INITRD_ADDR;
    uint32_t kernel_addr = KERNEL_ADDR;
    uint32_t entry = KERNEL_ADDR;
    struct timespec ts;
    struct BootImage *images = calloc((size_t)argc + 3, sizeof(*images));
    size_t image_count = 0, dtb_index = 0, initrd_index = 0;
    int i;

    if (!images) {
        fprintf(stderr, "tern-linux: cannot allocate boot image list\n");
        return 2;
    }
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--kernel") && i + 1 < argc) {
            kernel = argv[++i];
        } else if (!strcmp(argv[i], "--kernel-addr") && i + 1 < argc) {
            if (parse_boot_addr(argv[++i], &kernel_addr) != 0)
                goto invalid_args;
            entry = kernel_addr;
        } else if (!strcmp(argv[i], "--dtb") && i + 1 < argc) {
            dtb = argv[++i];
        } else if (!strcmp(argv[i], "--initrd") && i + 1 < argc) {
            initrd = argv[++i];
        } else if (!strcmp(argv[i], "--load") && i + 2 < argc) {
            uint32_t addr;

            if (parse_boot_addr(argv[++i], &addr) != 0)
                goto invalid_args;
            images[image_count++] = (struct BootImage){ .path = argv[++i], .addr = addr };
        } else if (!strcmp(argv[i], "--pc") && i + 1 < argc) {
            if (parse_boot_addr(argv[++i], &entry) != 0)
                goto invalid_args;
        } else if (!strcmp(argv[i], "--dtb-addr") && i + 1 < argc) {
            if (parse_boot_addr(argv[++i], &dtb_addr) != 0)
                goto invalid_args;
        } else if (!strcmp(argv[i], "--initrd-addr") && i + 1 < argc) {
            if (parse_boot_addr(argv[++i], &initrd_addr) != 0)
                goto invalid_args;
        } else if (!strcmp(argv[i], "--max-insn") && i + 1 < argc) {
            max_insn = strtoull(argv[++i], NULL, 0);
        } else if (!strcmp(argv[i], "--trace") && i + 1 < argc) {
            trace_count = strtoull(argv[++i], NULL, 0);
        } else if (!strcmp(argv[i], "--sample") && i + 1 < argc) {
            sample_count = strtoull(argv[++i], NULL, 0);
        } else if (!strcmp(argv[i], "--timebase") && i + 1 < argc) {
            timebase = (uint32_t)strtoul(argv[++i], NULL, 0);
            if (!timebase)
                timebase = 10000000u;
        } else if (!strcmp(argv[i], "--time-inst")) {
            time_inst_mode = 1;
        } else if (!strcmp(argv[i], "--no-sleep")) {
            sleep_wfi = 0;
        } else if (!strcmp(argv[i], "--debug-sbi")) {
            debug_sbi = 1;
        } else if (!strcmp(argv[i], "--debug-mmu")) {
            debug_mmu = 1;
        } else if (!strcmp(argv[i], "--stats")) {
            stats_enabled = 1;
        } else if (!strcmp(argv[i], "--quiet")) {
            quiet = 1;
        } else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
            usage(argv[0]);
            free(images);
            return 0;
        } else {
            fprintf(stderr, "tern-linux: unknown argument %s\n", argv[i]);
            usage(argv[0]);
            goto invalid_args;
        }
    }

    if (!kernel) {
        usage(argv[0]);
        goto invalid_args;
    }

    images[image_count++] = (struct BootImage){ .path = kernel, .addr = kernel_addr };
    if (dtb) {
        dtb_index = image_count;
        images[image_count++] = (struct BootImage){ .path = dtb, .addr = dtb_addr };
    }
    if (initrd) {
        initrd_index = image_count;
        images[image_count++] = (struct BootImage){ .path = initrd, .addr = initrd_addr };
    }
    if (load_boot_images(images, image_count) != 0)
        goto invalid_args;
    if (initrd && dtb) {
        fdt_patch_u32(dtb_addr, images[dtb_index].size, "linux,initrd-start", initrd_addr);
        fdt_patch_u32(dtb_addr, images[dtb_index].size, "linux,initrd-end",
                      initrd_addr + (uint32_t)images[initrd_index].size);
    }
    free(images);

    clock_gettime(CLOCK_MONOTONIC, &ts);
    boot_ns = (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
    if (stats_enabled)
        atexit(print_stats);
    guest_time = 0;
    pmp_reset();
    regs[10] = 0;
    regs[11] = dtb ? dtb_addr : 0;
    priv = 1;
    pc = entry;

    if (!quiet) {
        fprintf(stderr, "tern-linux: kernel 0x%08x dtb 0x%08x entry 0x%08x\n",
                kernel_addr, regs[11], pc);
    }

    for (;;) {
        uint32_t cur_pc, inst;
        unsigned len;

        if (max_insn && instret >= max_insn) {
            fprintf(stderr, "\ntern-linux: instruction limit %" PRIu64
                    " reached (pc=0x%08x)\n", max_insn, pc);
            return 2;
        }

        if (maybe_interrupt())
            continue;

        cur_pc = pc;
        if (!fetch_insn(&inst, &len))
            continue;
        pc = cur_pc + len;
        instret++;
        cycle++;

        if (trace_count > 0) {
            fprintf(stderr, "pc=0x%08x inst=0x%08x ra=0x%08x sp=0x%08x\n",
                    cur_pc, inst, regs[1], regs[2]);
            trace_count--;
        }
        if (sample_count && (instret % sample_count) == 0) {
            {
                int saved = mem_fault;
                uint32_t caller_ra = 0;
                if (in_ram(regs[8] + 12))
                    caller_ra = mem_load32(regs[8] + 12);
                mem_fault = saved;
                fprintf(stderr, "sample: pc=0x%08x ra=0x%08x caller=0x%08x sp=0x%08x s0=0x%08x a0=0x%08x time=%" PRIu64 " instret=%" PRIu64 "\n",
                        cur_pc, regs[1], caller_ra, regs[2], regs[8], regs[10], guest_time, instret);
            }
        }

        execute(inst, cur_pc);
    }

invalid_args:
    free(images);
    return 2;
}

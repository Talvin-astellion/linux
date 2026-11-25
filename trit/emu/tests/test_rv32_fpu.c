#include <assert.h>
#include <fenv.h>
#include <inttypes.h>
#include <math.h>
#include <string.h>

#define main rv32_unused_main
#include "../rv32.c"
#undef main

#define TEST_PC 0x80010000u
#define VEC     0x80020000u
#define ADDR1   0x80400000u

static void put32(uint32_t pa, uint32_t val)
{
    unsigned i;

    for (i = 0; i < 4; i++)
        ram[pa - RAM_BASE + i] = (uint8_t)(val >> (8 * i));
}

static void put64(uint32_t pa, uint64_t val)
{
    put32(pa, (uint32_t)val);
    put32(pa + 4, (uint32_t)(val >> 32));
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

    for (i = 0; i < 32; i++) {
        regs[i] = 0;
        fpr[i] = 0;
    }
    satp = 0;
    priv = 1;
    mstatus = 0;
    sstatus = 0;
    stvec = VEC;
    fcsr = 0;
    pc = 0;
    scause = 0;
    stval = 0;
    sepc = 0;
    mem_fault = 0;
    mmu_fault_cause = 0;
    pmp_reset();
}

static void run(uint32_t inst)
{
    pc = TEST_PC + 4;
    execute(inst, TEST_PC);
}

static uint32_t opfp(uint32_t funct7, uint32_t rs2, uint32_t rs1,
                     uint32_t funct3, uint32_t rd)
{
    return insn_r(funct7, rs2, rs1, funct3, rd, 0x53u);
}

static uint32_t opfp4(uint32_t opcode, uint32_t rs3, uint32_t rs2,
                      uint32_t rs1, uint32_t rm, uint32_t rd)
{
    return (rs3 << 27) | (rs2 << 20) | (rs1 << 15) | (rm << 12) |
           (rd << 7) | opcode;
}

static void test_fp_load_store(void)
{
    reset_cpu();
    regs[1] = ADDR1;
    put32(ADDR1 + 4, f32_bits(1.5f));
    run(insn_i(4, 1, 2, 8, 0x07u));
    assert(scause == 0);
    assert((uint32_t)fpr[8] == f32_bits(1.5f));
    assert(mstatus & MSTATUS_FS);

    reset_cpu();
    regs[1] = ADDR1;
    fpr[9] = f32_bits(-2.25f);
    run(insn_s_fp(8, 9, 1, 2));
    assert(scause == 0);
    assert(get32(ADDR1 + 8) == f32_bits(-2.25f));

    reset_cpu();
    regs[1] = ADDR1;
    fpr[8] = f64_bits(3.5);
    run(insn_s_fp(16, 8, 1, 3));
    assert(scause == 0);
    run(insn_i(16, 1, 3, 10, 0x07u));
    assert(scause == 0);
    assert(fpr[10] == f64_bits(3.5));
}

static void test_fp_arithmetic(void)
{
    reset_cpu();
    fpr[8] = f32_bits(1.5f);
    fpr[9] = f32_bits(2.0f);
    run(opfp(0x00, 9, 8, 0, 10));
    assert(scause == 0);
    assert((uint32_t)fpr[10] == f32_bits(3.5f));

    reset_cpu();
    fpr[8] = f64_bits(5.0);
    fpr[9] = f64_bits(2.0);
    run(opfp(0x0d, 9, 8, 0, 10));
    assert(scause == 0);
    assert(fpr[10] == f64_bits(2.5));

    reset_cpu();
    fpr[8] = f32_bits(9.0f);
    run(opfp(0x2c, 0, 8, 0, 10));
    assert(scause == 0);
    assert((uint32_t)fpr[10] == f32_bits(3.0f));

    reset_cpu();
    fpr[8] = f32_bits(2.0f);
    fpr[9] = f32_bits(3.0f);
    fpr[10] = f32_bits(5.0f);
    run(0x43u | (4u << 7) | (8u << 15) | (9u << 20) | (10u << 27));
    assert(scause == 0);
    assert((uint32_t)fpr[4] == f32_bits(11.0f));
}

static uint32_t fuzz_next(uint32_t *state)
{
    uint32_t x = *state;

    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static uint32_t host_fp_flags(void)
{
    int e = fetestexcept(FE_INVALID | FE_DIVBYZERO | FE_OVERFLOW |
                         FE_UNDERFLOW | FE_INEXACT);
    uint32_t flags = 0;

    if (e & FE_INVALID) flags |= 0x10u;
    if (e & FE_DIVBYZERO) flags |= 0x08u;
    if (e & FE_OVERFLOW) flags |= 0x04u;
    if (e & FE_UNDERFLOW) flags |= 0x02u;
    if (e & FE_INEXACT) flags |= 0x01u;
    return flags;
}

static void host_set_round(uint32_t rm)
{
    switch (rm) {
    case 0: case 4: fesetround(FE_TONEAREST); break;
    case 1: fesetround(FE_TOWARDZERO); break;
    case 2: fesetround(FE_DOWNWARD); break;
    default: fesetround(FE_UPWARD); break;
    }
}

static uint32_t oracle_rmm32(float rn, __float128 x)
{
    uint32_t bits = f32_bits(rn);
    float up, down;
    __float128 rv, lower, upper;

    if (!isfinite(rn) || !finiteq(x))
        return bits;
    rv = (__float128)rn;
    if (x == rv)
        return bits;
    up = nextafterf(rn, INFINITY);
    down = nextafterf(rn, -INFINITY);
    if (isinf(up) || isinf(down))
        return bits;
    lower = x < rv ? (__float128)down : rv;
    upper = x < rv ? rv : (__float128)up;
    if (x != (lower + upper) / 2)
        return bits;
    return f32_bits(x > 0 ? up : down);
}

static uint32_t oracle_f32(uint32_t a_bits, uint32_t b_bits, uint32_t rm,
                           uint32_t funct7, uint32_t *flags)
{
    float a = bits_f32(a_bits);
    float b = bits_f32(b_bits);
    __float128 aq = a;
    __float128 bq = b;
    __float128 xq;
    float v;
    uint32_t result;

    feclearexcept(FE_ALL_EXCEPT);
    host_set_round(rm);
    switch (funct7) {
    case 0x00: v = a + b; break;
    case 0x04: v = a - b; break;
    case 0x08: v = a * b; break;
    case 0x0c: v = a / b; break;
    default: v = sqrtf(a); break;
    }
    *flags = host_fp_flags();
    fesetround(FE_TONEAREST);
    if (isnan(v))
        return 0x7fc00000u;
    result = f32_bits(v);
    if (rm != 4)
        return result;
    feclearexcept(FE_ALL_EXCEPT);
    switch (funct7) {
    case 0x00: xq = aq + bq; break;
    case 0x04: xq = aq - bq; break;
    case 0x08: xq = aq * bq; break;
    case 0x0c: xq = aq / bq; break;
    default: xq = sqrtq(aq); break;
    }
    return oracle_rmm32(v, xq);
}

static uint32_t oracle_fma32(uint32_t a_bits, uint32_t b_bits,
                             uint32_t c_bits, uint32_t opcode, uint32_t rm,
                             uint32_t *flags)
{
    float a = bits_f32(a_bits);
    float b = bits_f32(b_bits);
    float c = bits_f32(c_bits);
    __float128 aq = a;
    __float128 bq = b;
    __float128 cq = c;
    __float128 xq;
    float v;

    feclearexcept(FE_ALL_EXCEPT);
    host_set_round(rm);
    switch (opcode) {
    case 0x43: v = fmaf(a, b, c); break;
    case 0x47: v = fmaf(a, b, -c); break;
    case 0x4b: v = fmaf(-a, b, c); break;
    default: v = fmaf(-a, b, -c); break;
    }
    *flags = host_fp_flags();
    fesetround(FE_TONEAREST);
    if (isnan(v))
        return 0x7fc00000u;
    if (rm != 4)
        return f32_bits(v);
    feclearexcept(FE_ALL_EXCEPT);
    switch (opcode) {
    case 0x43: xq = fmaq(aq, bq, cq); break;
    case 0x47: xq = fmaq(aq, -bq, cq); break;
    case 0x4b: xq = fmaq(-aq, bq, cq); break;
    default: xq = fmaq(-aq, -bq, cq); break;
    }
    return oracle_rmm32(v, xq);
}

static uint64_t fuzz_next64(uint32_t *state)
{
    return ((uint64_t)fuzz_next(state) << 32) | fuzz_next(state);
}

static uint64_t oracle_rmm64(double rn, __float128 x)
{
    uint64_t bits = f64_bits(rn);
    double up, down;
    __float128 rv, lower, upper;

    if (!isfinite(rn) || !finiteq(x))
        return bits;
    rv = (__float128)rn;
    if (x == rv)
        return bits;
    up = nextafter(rn, INFINITY);
    down = nextafter(rn, -INFINITY);
    if (isinf(up) || isinf(down))
        return bits;
    lower = x < rv ? (__float128)down : rv;
    upper = x < rv ? rv : (__float128)up;
    if (x != (lower + upper) / 2)
        return bits;
    return f64_bits(x > 0 ? up : down);
}

static uint64_t oracle_f64(uint64_t a_bits, uint64_t b_bits, uint32_t rm,
                           uint32_t funct7, uint32_t *flags)
{
    double a = bits_f64(a_bits);
    double b = bits_f64(b_bits);
    __float128 aq = a;
    __float128 bq = b;
    __float128 xq;
    double v;
    uint64_t result;

    feclearexcept(FE_ALL_EXCEPT);
    host_set_round(rm);
    switch (funct7) {
    case 0x01: v = a + b; break;
    case 0x05: v = a - b; break;
    case 0x09: v = a * b; break;
    case 0x0d: v = a / b; break;
    default: v = sqrt(a); break;
    }
    *flags = host_fp_flags();
    fesetround(FE_TONEAREST);
    if (isnan(v))
        return 0x7ff8000000000000ULL;
    result = f64_bits(v);
    if (rm != 4)
        return result;
    feclearexcept(FE_ALL_EXCEPT);
    switch (funct7) {
    case 0x01: xq = aq + bq; break;
    case 0x05: xq = aq - bq; break;
    case 0x09: xq = aq * bq; break;
    case 0x0d: xq = aq / bq; break;
    default: xq = sqrtq(aq); break;
    }
    return oracle_rmm64(v, xq);
}

static void test_fp_diff_sweep(void)
{
    static const uint32_t ops[] = {0x00u, 0x04u, 0x08u, 0x0cu, 0x2cu};
    uint32_t seed = 0x9e3779b9u;
    unsigned op, rm, i;

    for (op = 0; op < sizeof(ops) / sizeof(ops[0]); op++) {
        for (rm = 0; rm < 5; rm++) {
            for (i = 0; i < 160; i++) {
                uint32_t a = fuzz_next(&seed);
                uint32_t b = ops[op] == 0x2cu ? 0u : fuzz_next(&seed);
                uint32_t rs2 = ops[op] == 0x2cu ? 0u : 9u;
                uint32_t flags;
                uint32_t expected = oracle_f32(a, b, rm, ops[op], &flags);

                reset_cpu();
                fpr[8] = a;
                fpr[9] = b;
                run(opfp(ops[op], rs2, 8, rm, 10));
                if (scause != 0 || (uint32_t)fpr[10] != expected ||
                    (fcsr & 0x1fu) != flags) {
                    fprintf(stderr,
                            "fp diff: op=%02x rm=%u a=%08x b=%08x "
                            "got=%08x/%02x want=%08x/%02x scause=%u\n",
                            ops[op], rm, a, b, (uint32_t)fpr[10],
                            fcsr & 0x1fu, expected, flags, scause);
                }
                assert(scause == 0);
                assert((uint32_t)fpr[10] == expected);
                assert((fcsr & 0x1fu) == flags);
            }
        }
    }

    {
        static const uint32_t fma_ops[] = {0x43u, 0x47u, 0x4bu, 0x4fu};
        unsigned f;

        for (f = 0; f < sizeof(fma_ops) / sizeof(fma_ops[0]); f++) {
            for (rm = 0; rm < 5; rm++) {
                for (i = 0; i < 96; i++) {
                    uint32_t a = fuzz_next(&seed);
                    uint32_t b = fuzz_next(&seed);
                    uint32_t c = fuzz_next(&seed);
                    uint32_t flags;
                    uint32_t expected =
                        oracle_fma32(a, b, c, fma_ops[f], rm, &flags);

                    reset_cpu();
                    fpr[8] = a;
                    fpr[9] = b;
                    fpr[10] = c;
                    run(opfp4(fma_ops[f], 10, 9, 8, rm, 11));
                    if (scause != 0 || (uint32_t)fpr[11] != expected ||
                        (fcsr & 0x1fu) != flags) {
                        fprintf(stderr,
                                "fma diff: op=%02x rm=%u a=%08x b=%08x "
                                "c=%08x got=%08x/%02x want=%08x/%02x "
                                "scause=%u\n",
                                fma_ops[f], rm, a, b, c, (uint32_t)fpr[11],
                                fcsr & 0x1fu, expected, flags, scause);
                    }
                    assert(scause == 0);
                    assert((uint32_t)fpr[11] == expected);
                    assert((fcsr & 0x1fu) == flags);
                }
            }
        }
    }

    {
        static const uint32_t ops64[] = {0x01u, 0x05u, 0x09u, 0x0du, 0x2du};

        for (op = 0; op < sizeof(ops64) / sizeof(ops64[0]); op++) {
            for (rm = 0; rm < 5; rm++) {
                for (i = 0; i < 160; i++) {
                    uint64_t a = fuzz_next64(&seed);
                    uint64_t b = ops64[op] == 0x2du ? 0u : fuzz_next64(&seed);
                    uint32_t rs2 = ops64[op] == 0x2du ? 0u : 9u;
                    uint32_t flags;
                    uint64_t expected =
                        oracle_f64(a, b, rm, ops64[op], &flags);

                    reset_cpu();
                    fpr[8] = a;
                    fpr[9] = b;
                    run(opfp(ops64[op], rs2, 8, rm, 10));
                    if (scause != 0 || fpr[10] != expected ||
                        (fcsr & 0x1fu) != flags) {
                        fprintf(stderr,
                                "fp64 diff: op=%02x rm=%u a=%016" PRIx64
                                " b=%016" PRIx64 " got=%016" PRIx64
                                "/%02x want=%016" PRIx64 "/%02x scause=%u\n",
                                ops64[op], rm, a, b, fpr[10], fcsr & 0x1fu,
                                expected, flags, scause);
                    }
                    assert(scause == 0);
                    assert(fpr[10] == expected);
                    assert((fcsr & 0x1fu) == flags);
                }
            }
        }
    }
    fesetround(FE_TONEAREST);
}

static void test_fp_rounding(void)
{
    reset_cpu();
    fpr[8] = f32_bits(1.0f);
    fpr[9] = 0x33800000u;
    run(opfp(0x00, 9, 8, 4, 10));
    assert(scause == 0);
    assert((uint32_t)fpr[10] == 0x3f800001u);

    reset_cpu();
    fpr[8] = f64_bits(1.0);
    fpr[9] = 0x3ca0000000000000ULL;
    run(opfp(0x01, 9, 8, 4, 10));
    assert(scause == 0);
    assert(fpr[10] == 0x3ff0000000000001ULL);

    reset_cpu();
    fpr[8] = f32_bits(1.0f);
    fpr[9] = 0x33800000u;
    fpr[10] = f32_bits(1.0f);
    run(opfp4(0x43u, 10, 9, 8, 4, 11));
    assert(scause == 0);
    assert((uint32_t)fpr[11] == 0x3f800001u);

    reset_cpu();
    regs[8] = 0x01000001u;
    run(opfp(0x68, 1, 8, 4, 10));
    assert(scause == 0);
    assert((uint32_t)fpr[10] == 0x4b800001u);

    reset_cpu();
    fcsr = 0x80u;
    fpr[8] = f32_bits(2.5f);
    run(opfp(0x60, 0, 8, 7, 10));
    assert(scause == 0);
    assert(regs[10] == 3);
}

static void test_fp_subnormal(void)
{
    reset_cpu();
    fpr[8] = 0x007fffffu;
    fpr[9] = f32_bits(1.0f);
    run(opfp(0x08, 9, 8, 0, 10));
    assert(scause == 0);
    assert((uint32_t)fpr[10] == 0x007fffffu);
    assert(fcsr == 0);

    reset_cpu();
    fpr[8] = 0x00000001u;
    fpr[9] = 0x00000001u;
    run(opfp(0x00, 9, 8, 0, 10));
    assert(scause == 0);
    assert((uint32_t)fpr[10] == 0x00000002u);
    assert(fcsr == 0);

    reset_cpu();
    fpr[8] = 1;
    fpr[9] = f64_bits(1.0);
    run(opfp(0x09, 9, 8, 0, 10));
    assert(scause == 0);
    assert(fpr[10] == 1);
    assert(fcsr == 0);
}

static void test_fp_sign_minmax(void)
{
    reset_cpu();
    fpr[8] = f32_bits(1.5f);
    fpr[9] = 0x80000000u;
    run(opfp(0x10, 9, 8, 0, 10));
    assert(scause == 0);
    assert((uint32_t)fpr[10] == f32_bits(-1.5f));

    reset_cpu();
    fpr[8] = 0x80000000u;
    fpr[9] = 0;
    run(opfp(0x14, 9, 8, 0, 10));
    assert(scause == 0);
    assert((uint32_t)fpr[10] == 0x80000000u);

    reset_cpu();
    fpr[8] = 0x80000000u;
    fpr[9] = 0;
    run(opfp(0x14, 9, 8, 1, 10));
    assert(scause == 0);
    assert((uint32_t)fpr[10] == 0);
}

static void test_fp_convert(void)
{
    reset_cpu();
    regs[9] = 42;
    run(opfp(0x68, 0, 9, 0, 10));
    assert(scause == 0);
    assert((uint32_t)fpr[10] == f32_bits(42.0f));

    reset_cpu();
    regs[9] = 0xfffffffeu;
    run(opfp(0x68, 1, 9, 0, 10));
    assert(scause == 0);
    assert((uint32_t)fpr[10] == f32_bits(4294967294.0f));

    reset_cpu();
    fpr[9] = f32_bits(3.75f);
    run(opfp(0x60, 0, 9, 1, 10));
    assert(scause == 0);
    assert(regs[10] == 3);

    reset_cpu();
    fpr[9] = f64_bits(-3.75);
    run(opfp(0x61, 0, 9, 0, 10));
    assert(scause == 0);
    assert(regs[10] == (uint32_t)-4);

    reset_cpu();
    fpr[9] = f32_bits(INFINITY);
    run(opfp(0x60, 0, 9, 0, 10));
    assert(scause == 0);
    assert(regs[10] == 0x7fffffffu);
    assert(fcsr & 0x10u);

    reset_cpu();
    fpr[9] = f64_bits(1.25);
    run(opfp(0x20, 1, 9, 0, 10));
    assert(scause == 0);
    assert((uint32_t)fpr[10] == f32_bits(1.25f));

    reset_cpu();
    fpr[9] = f32_bits(1.25f);
    run(opfp(0x21, 0, 9, 0, 10));
    assert(scause == 0);
    assert(fpr[10] == f64_bits(1.25));
}

static void test_fp_compare_class(void)
{
    reset_cpu();
    fpr[8] = f32_bits(1.5f);
    fpr[9] = f32_bits(1.5f);
    run(opfp(0x50, 9, 8, 2, 10));
    assert(scause == 0);
    assert(regs[10] == 1);

    reset_cpu();
    fpr[8] = 0x7f800001u;
    fpr[9] = f32_bits(1.0f);
    run(opfp(0x50, 9, 8, 1, 10));
    assert(scause == 0);
    assert(regs[10] == 0);
    assert(fcsr & 0x10u);

    reset_cpu();
    fpr[8] = 0x80000000u;
    run(opfp(0x70, 0, 8, 1, 10));
    assert(scause == 0);
    assert(regs[10] == 1u << 3);

    reset_cpu();
    regs[9] = 0x40490fdbu;
    run(opfp(0x78, 0, 9, 0, 10));
    assert(scause == 0);
    assert((uint32_t)fpr[10] == 0x40490fdbu);

    reset_cpu();
    fpr[9] = 0x40490fdbu;
    run(opfp(0x70, 0, 9, 0, 10));
    assert(scause == 0);
    assert(regs[10] == 0x40490fdbu);
}

static void test_fp_csr_and_illegal(void)
{
    reset_cpu();
    csr_write(0x003, 0x1fu | (3u << 5));
    assert(csr_read(0x001) == 0x1fu);
    assert(csr_read(0x002) == 3u);
    assert(csr_read(0x003) == 0x7fu);
    assert((csr_read(0x301) & 0x28u) == 0x28u);

    reset_cpu();
    fpr[8] = f32_bits(16777217.0f);
    run(opfp(0x68, 0, 8, 5, 10));
    assert(scause == CAUSE_ILLEGAL);
    assert(scause != 0);

    reset_cpu();
    run(opfp(0x2c, 1, 8, 0, 10));
    assert(scause == CAUSE_ILLEGAL);

    reset_cpu();
    run(opfp(0x50, 8, 8, 3, 10));
    assert(scause == CAUSE_ILLEGAL);
}

static void test_compressed_fp(void)
{
    uint32_t inst;

    reset_cpu();
    regs[8] = ADDR1;
    put32(ADDR1 + 4, f32_bits(2.5f));
    assert(rv32c_expand(0x2040, &inst));
    run(inst);
    assert(scause == 0);
    assert((uint32_t)fpr[8] == f32_bits(2.5f));

    reset_cpu();
    regs[8] = ADDR1;
    fpr[9] = f32_bits(6.5f);
    assert(rv32c_expand(0xe044, &inst));
    run(inst);
    assert(scause == 0);
    assert(get32(ADDR1 + 4) == f32_bits(6.5f));

    reset_cpu();
    regs[2] = ADDR1;
    put32(ADDR1 + 4, f32_bits(3.5f));
    assert(rv32c_expand(0x6412, &inst));
    run(inst);
    assert(scause == 0);
    assert((uint32_t)fpr[8] == f32_bits(3.5f));

    reset_cpu();
    regs[2] = ADDR1;
    fpr[9] = f32_bits(7.5f);
    assert(rv32c_expand(0xe226, &inst));
    run(inst);
    assert(scause == 0);
    assert(get32(ADDR1 + 4) == f32_bits(7.5f));

    reset_cpu();
    regs[2] = ADDR1;
    put64(ADDR1, f64_bits(8.5));
    assert(rv32c_expand(0x2402, &inst));
    run(inst);
    assert(scause == 0);
    assert(fpr[8] == f64_bits(8.5));

    reset_cpu();
    regs[2] = ADDR1;
    put32(ADDR1 + 8, f32_bits(3.5f));
    put32(ADDR1 + 64, f32_bits(4.5f));
    assert(rv32c_expand(0x2422, &inst));
    run(inst);
    assert(scause == 0);
    assert((uint32_t)fpr[8] == f32_bits(3.5f));

    reset_cpu();
    regs[2] = ADDR1;
    put32(ADDR1 + 8, f32_bits(3.5f));
    put32(ADDR1 + 64, f32_bits(4.5f));
    assert(rv32c_expand(0x2406, &inst));
    run(inst);
    assert(scause == 0);
    assert((uint32_t)fpr[8] == f32_bits(4.5f));

    reset_cpu();
    regs[2] = ADDR1;
    fpr[8] = f64_bits(9.5);
    assert(rv32c_expand(0xa022, &inst));
    run(inst);
    assert(scause == 0);
    assert(get32(ADDR1) == (uint32_t)f64_bits(9.5));
    assert(get32(ADDR1 + 4) == (uint32_t)(f64_bits(9.5) >> 32));
}

int main(void)
{
    ram = calloc(1, RAM_SIZE);
    assert(ram != NULL);

    test_fp_load_store();
    test_fp_arithmetic();
    test_fp_diff_sweep();
    test_fp_rounding();
    test_fp_subnormal();
    test_fp_sign_minmax();
    test_fp_convert();
    test_fp_compare_class();
    test_fp_csr_and_illegal();
    test_compressed_fp();

    free(ram);
    return 0;
}

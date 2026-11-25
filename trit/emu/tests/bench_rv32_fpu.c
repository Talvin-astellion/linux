#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define main rv32_unused_main
#include "../rv32.c"
#undef main

#define BENCH_PC 0x80010000u

static uint32_t fp_reg_inst(uint32_t funct7, uint32_t rm,
                            uint32_t rs2, uint32_t rs1, uint32_t rd)
{
    return (funct7 << 25) | (rs2 << 20) | (rs1 << 15) |
           (rm << 12) | (rd << 7) | 0x53u;
}

static int benchmark(const char *name, uint32_t rm, uint64_t one,
                     uint64_t half, uint32_t add7, uint32_t sub7, long ops)
{
    uint32_t add = fp_reg_inst(add7, rm, 2, 1, 1);
    uint32_t sub = fp_reg_inst(sub7, rm, 2, 1, 1);
    struct timespec start, end;
    double seconds;
    long i;

    priv = 3;
    pc = BENCH_PC;
    scause = 0;
    stval = 0;
    fcsr = rm << 5;
    fp_rm = rm;
    fpr[1] = one;
    fpr[2] = half;

    if (clock_gettime(CLOCK_MONOTONIC, &start) != 0) {
        perror("clock_gettime");
        return 1;
    }
    for (i = 0; i < ops; i++)
        execute((i & 1) ? sub : add, BENCH_PC);
    if (clock_gettime(CLOCK_MONOTONIC, &end) != 0) {
        perror("clock_gettime");
        return 1;
    }

    seconds = (double)(end.tv_sec - start.tv_sec) +
              (double)(end.tv_nsec - start.tv_nsec) / 1e9;
    if (scause != 0 || fpr[1] != one) {
        fprintf(stderr, "%s: unexpected CPU state\n", name);
        return 1;
    }
    printf("%-10s %ld operations, %.6f s, %.3f Mops/s\n",
           name, ops, seconds, ops / seconds / 1e6);
    return 0;
}

int main(int argc, char **argv)
{
    long ops = 5000000;

    if (argc > 2) {
        fprintf(stderr, "Usage: %s [even-operation-count]\n", argv[0]);
        return 2;
    }
    if (argc == 2) {
        char *end;

        errno = 0;
        ops = strtol(argv[1], &end, 10);
        if (errno != 0 || end == argv[1] || *end != '\0' ||
            ops < 2 || ops > INT_MAX || (ops & 1)) {
            fprintf(stderr, "Operation count must be even and in 2..%d\n",
                    INT_MAX);
            return 2;
        }
    }

    return benchmark("f32-rne", 0, 0x3f800000u, 0x3f000000u,
                     0x00, 0x04, ops) ||
           benchmark("f32-rmm", 4, 0x3f800000u, 0x3f000000u,
                     0x00, 0x04, ops) ||
           benchmark("f64-rne", 0, 0x3ff0000000000000ULL,
                     0x3fe0000000000000ULL, 0x01, 0x05, ops) ||
           benchmark("f64-rmm", 4, 0x3ff0000000000000ULL,
                     0x3fe0000000000000ULL, 0x01, 0x05, ops);
}

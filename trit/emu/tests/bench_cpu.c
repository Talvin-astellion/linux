#include "../cpu.h"
#include "../mem.h"
#include "../taint.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static TriWord makeAdd( TriReg dest, TriReg left, TriReg right )
{
    return OPCODE_ADD | ( (TriWord)dest << 12 ) |
           ( (TriWord)OPMOD_NOP << 18 ) | ( (TriWord)left << 26 ) |
           ( (TriWord)OPMOD_NOP << 32 ) | ( (TriWord)right << 40 );
}

static int benchmark( const char* name, int cycles, int arithmetic,
                      int tainted, int distinct )
{
    const int base = 1000;
    const int body = distinct ? 512 : 63;
    TriCpu cpu;
    struct timespec start, end;
    double seconds;
    int i;
    int64_t additions;

    resetMem();
    resetCPU( &cpu );
    cpu.regs[REG_PC] = ternaryFromInt64( base );
    WriteReg( &cpu, REG_P02, ternaryFromInt64( 1 ) );
    WriteReg( &cpu, REG_P13, ternaryFromInt64( base ) );
    for( i = 0; i < body; ++i )
    {
        TriWord inst = arithmetic ? makeAdd( REG_P01, REG_P01, REG_P02 )
                                  : OPCODE_NOP;
        if( distinct )
        {
            /* NOP ignores its operands; vary them to exceed cache capacity. */
            inst |= ternaryFromInt64( i ) << 12;
        }
        WriteTriWord( &cpu, ternaryFromInt64( base + 3 * i ), inst );
    }
    WriteTriWord( &cpu, ternaryFromInt64( base + 3 * body ),
                  makeAdd( REG_PC, REG_ZERO, REG_P13 ) );
    if( tainted )
    {
        if( taintInit() != 0 )
        {
            fprintf( stderr, "Cannot initialize taint tracking\n" );
            return 1;
        }
        taintMarkReg( REG_P02 );
    }

    if( clock_gettime( CLOCK_MONOTONIC, &start ) != 0 )
    {
        perror( "clock_gettime" );
        taintShutdown();
        return 1;
    }
    runCPU( &cpu, cycles );
    if( clock_gettime( CLOCK_MONOTONIC, &end ) != 0 )
    {
        perror( "clock_gettime" );
        taintShutdown();
        return 1;
    }
    seconds = (double)( end.tv_sec - start.tv_sec ) +
              (double)( end.tv_nsec - start.tv_nsec ) / 1e9;
    additions = (int64_t)cycles - cycles / ( body + 1 );
    if( cpu.halted || triwordToInt( cpu.regs[REG_CLOCK] ) != cycles ||
        triwordToInt( cpu.regs[REG_PC] ) != base + 3 * ( cycles % ( body + 1 ) ) ||
        triwordToInt( cpu.regs[REG_P01] ) != ( arithmetic ? additions : 0 ) ||
        ( tainted && !taintReg( REG_P01 ) ) )
    {
        fprintf( stderr, "%s: unexpected CPU state\n", name );
        taintShutdown();
        return 1;
    }
    taintShutdown();
    printf( "%-12s %d instructions, %.6f s, %.3f Minsn/s\n",
            name, cycles, seconds, cycles / seconds / 1e6 );
    return 0;
}

int main( int argc, char** argv )
{
    long cycles = 1000000;
    if( argc > 2 )
    {
        fprintf( stderr, "Usage: %s [instructions-per-workload]\n", argv[0] );
        return 2;
    }
    if( argc == 2 )
    {
        char* end;
        errno = 0;
        cycles = strtol( argv[1], &end, 10 );
        if( errno != 0 || end == argv[1] || *end != '\0' ||
            cycles < 1 || cycles > INT_MAX )
        {
            fprintf( stderr, "Instruction count must be in 1..%d\n", INT_MAX );
            return 2;
        }
    }
    return benchmark( "nop-loop", (int)cycles, 0, 0, 0 ) ||
           benchmark( "add-loop", (int)cycles, 1, 0, 0 ) ||
           benchmark( "add-taint", (int)cycles, 1, 1, 0 ) ||
           benchmark( "decode-churn", (int)cycles, 0, 0, 1 );
}

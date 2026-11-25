#include "../cpu.h"
#include "../mem.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static int number( const char* text, int64_t min, int64_t max, int64_t* value )
{
    char* end;
    long long parsed;
    errno = 0;
    parsed = strtoll( text, &end, 10 );
    if( errno != 0 || end == text || *end != '\0' || parsed < min || parsed > max )
    {
        return -1;
    }
    *value = parsed;
    return 0;
}

int main( int argc, char** argv )
{
    int64_t expected, runs = 5, limit = 1000000;
    int64_t instructions = 0;
    double seconds = 0;
    FILE* file;
    int i;

    if( argc < 3 || argc > 5 ||
        number( argv[2], -3812798742493LL, 3812798742493LL, &expected ) != 0 ||
        ( argc > 3 && number( argv[3], 1, INT_MAX, &runs ) != 0 ) ||
        ( argc > 4 && number( argv[4], 1, INT_MAX, &limit ) != 0 ) )
    {
        fprintf( stderr, "Usage: %s PROGRAM.trt EXPECTED_P01 [RUNS [INSTRUCTION_LIMIT]]\n"
                         "Numeric arguments are decimal; runs and limit must be in 1..%d.\n",
                 argv[0], INT_MAX );
        return 2;
    }
    file = fopen( argv[1], "r" );
    if( file == NULL )
    {
        perror( argv[1] );
        return 1;
    }
    for( i = 0; i < runs; ++i )
    {
        TriCpu cpu;
        struct timespec start, end;
        int64_t count;

        /* Reload outside the timed region, so each run starts from the same image. */
        rewind( file );
        resetMem();
        if( readFileIntoMem( file, argv[1], 0 ) != 0 || ferror( file ) )
        {
            fprintf( stderr, "%s: cannot load program\n", argv[1] );
            fclose( file );
            return 1;
        }
        resetCPU( &cpu );
        if( clock_gettime( CLOCK_MONOTONIC, &start ) != 0 )
        {
            perror( "clock_gettime" );
            fclose( file );
            return 1;
        }
        runCPU( &cpu, (int)limit );
        if( clock_gettime( CLOCK_MONOTONIC, &end ) != 0 )
        {
            perror( "clock_gettime" );
            fclose( file );
            return 1;
        }
        count = triwordToInt( cpu.regs[REG_CLOCK] );
        if( !cpu.halted || cpu.trap != 0 ||
            triwordToInt( cpu.regs[REG_P01] ) != expected ||
            ( i > 0 && count != instructions / i ) )
        {
            fprintf( stderr, "%s: run %d failed: halted=%d trap=%d P01=%"PRIi64
                             " (expected %"PRIi64"), instructions=%"PRIi64"\n",
                     argv[1], i + 1, cpu.halted, cpu.trap,
                     triwordToInt( cpu.regs[REG_P01] ), expected, count );
            fclose( file );
            return 1;
        }
        instructions += count;
        seconds += (double)( end.tv_sec - start.tv_sec ) +
                   (double)( end.tv_nsec - start.tv_nsec ) / 1e9;
    }
    fclose( file );
    printf( "%s: %"PRIi64" runs, %"PRIi64" instructions, %.6f s, %.3f Minsn/s, P01=%"PRIi64"\n",
            argv[1], runs, instructions, seconds, instructions / seconds / 1e6, expected );
    return 0;
}

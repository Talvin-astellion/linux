#include "../cpu.h"
#include "../mem.h"
#include "../symbolic.h"
#include "../taint.h"
#include "../trace.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int failures = 0;

#define CHECK(cond) do { \
    if( !(cond) ) { \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        ++failures; \
    } \
} while( 0 )

static void setup( TriCpu* cpu )
{
    resetMem();
    resetCPU( cpu );
    cpu->regs[REG_PC] = 0;
}

static TriWord makeInstruction( TriOpcode opcode, TriReg r1, TriReg r2, TriReg r3 )
{
    TriWord inst = opcode;
    inst |= (TriWord)r1 << 12;
    inst |= (TriWord)OPMOD_NOP << 18;
    inst |= (TriWord)r2 << 26;
    inst |= (TriWord)OPMOD_NOP << 32;
    inst |= (TriWord)r3 << 40;
    return inst;
}

static void testTaintAdd( void )
{
    TriCpu cpu;
    TriWord inst;
    setup( &cpu );
    CHECK( taintInit() == 0 );
    taintMarkReg( REG_P02 );
    inst = makeInstruction( OPCODE_ADD, REG_P01, REG_P02, REG_P03 );
    WriteTriWord( &cpu, 0, inst );
    runCPU( &cpu, 1 );
    CHECK( taintReg( REG_P01 ) == 1 );
    CHECK( taintReg( REG_P03 ) == 0 );
    taintShutdown();
}

static void testTaintPendingList( void )
{
    TriCpu cpu;
    setup( &cpu );
    CHECK( taintInit() == 0 );
    taintMarkReg( REG_P02 );
    WriteTriWord( &cpu, 0,
                  makeInstruction( OPCODE_RTL, REG_P01, REG_P02, REG_P03 ) );
    WriteTriWord( &cpu, ternaryFromInt64( 3 ), OPCODE_NOP );

    runCPU( &cpu, 1 );
    CHECK( taintReg( REG_P01 ) == 1 );
    CHECK( taintReg( REG_P02 ) == 1 );
    CHECK( taintReg( REG_P03 ) == 0 );

    /* The next instruction must not replay the previous pending writes. */
    runCPU( &cpu, 1 );
    CHECK( taintReg( REG_P01 ) == 1 );
    CHECK( taintReg( REG_P04 ) == 0 );
    taintShutdown();
}

static void testTaintMemory( void )
{
    TriCpu cpu;
    TriWord inst;
    setup( &cpu );
    CHECK( taintInit() == 0 );
    taintMarkReg( REG_P02 );

    inst = OPCODE_ST1;
    inst |= (TriWord)REG_ZERO << 12;
    inst |= (TriWord)OPMOD_NOP << 18;
    inst |= (TriWord)REG_P02 << 26;
    WriteTriWord( &cpu, 0, inst );
    runCPU( &cpu, 1 );
    CHECK( taintMem( 0 ) == 1 );

    inst = OPCODE_LD1;
    inst |= (TriWord)REG_P01 << 12;
    inst |= (TriWord)OPMOD_NOP << 18;
    inst |= (TriWord)REG_ZERO << 26;
    WriteTriWord( &cpu, 4, inst );
    runCPU( &cpu, 1 );
    CHECK( taintReg( REG_P01 ) == 1 );
    taintShutdown();
}

static void testTaintAddressAliases( void )
{
    CHECK( taintInit() == 0 );
    taintMarkMem( ternaryFromInt64( -BASE_OFFSET ) );
    CHECK( taintMem( ternaryFromInt64( -BASE_OFFSET - RAM_SIZE ) ) == 1 );
    CHECK( taintMem( ternaryFromInt64( BASE_OFFSET ) ) == 0 );
    taintMarkMem( ternaryFromInt64( RAM_SIZE + BASE_OFFSET ) );
    CHECK( taintMem( ternaryFromInt64( BASE_OFFSET ) ) == 1 );
    CHECK( taintMemCount() == 2 );
    taintMarkMem( ternaryFromInt64( BASE_OFFSET ) );
    CHECK( taintMemCount() == 2 );
    taintShutdown();
}

static void testTraceFile( void )
{
    TriCpu cpu;
    char path[256];
    FILE* file;
    char line[512];
    int lines = 0;

    snprintf( path, sizeof( path ), "/tmp/trace_test_%ld.jsonl", (long)getpid() );
    remove( path );
    CHECK( traceInit( path ) == 0 );
    setup( &cpu );
    runCPU( &cpu, 2 );
    traceShutdown();

    file = fopen( path, "r" );
    CHECK( file != NULL );
    if( file != NULL )
    {
        while( fgets( line, sizeof( line ), file ) != NULL )
        {
            if( strstr( line, "\"event\":\"insn\"" ) != NULL )
            {
                ++lines;
            }
        }
        fclose( file );
    }
    CHECK( lines == 2 );
    remove( path );
}

static void testSymbolic( void )
{
    TriCpu cpu;
    TriWord inst;
    char path[256];
    FILE* file;
    char line[512];
    int found = 0;

    snprintf( path, sizeof( path ), "/tmp/sym_test_%ld.jsonl", (long)getpid() );
    remove( path );
    CHECK( symInit( path ) == 0 );
    setup( &cpu );
    inst = makeInstruction( OPCODE_ADS, REG_P01, REG_P02, REG_P03 );
    WriteTriWord( &cpu, 0, inst );
    runCPU( &cpu, 1 );
    CHECK( symCount() == 1 );
    symShutdown();

    file = fopen( path, "r" );
    CHECK( file != NULL );
    if( file != NULL )
    {
        while( fgets( line, sizeof( line ), file ) != NULL )
        {
            if( strstr( line, "\"event\":\"constraint\"" ) != NULL )
            {
                found = 1;
            }
        }
        fclose( file );
    }
    CHECK( found == 1 );
    remove( path );
}

int main( void )
{
    testTaintAdd();
    testTaintPendingList();
    testTaintMemory();
    testTaintAddressAliases();
    testTraceFile();
    testSymbolic();

    if( failures != 0 )
    {
        printf( "%d test(s) failed\n", failures );
        return 1;
    }
    printf( "all phase-2 tests passed\n" );
    return 0;
}

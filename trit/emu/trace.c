#include "trace.h"
#include "disassem.h"

#include <string.h>

static FILE* traceFile = NULL;
static unsigned long traceSeq = 0;
static unsigned long lastTaintMask = 0;
static unsigned long lastTaintMem = 0;

int traceInit( const char* path )
{
    if( path == NULL || path[0] == '\0' )
    {
        return -1;
    }
    traceFile = fopen( path, "w" );
    if( traceFile == NULL )
    {
        return -1;
    }
    traceSeq = 0;
    lastTaintMask = 0;
    lastTaintMem = 0;
    return 0;
}

void traceShutdown( void )
{
    if( traceFile != NULL )
    {
        fflush( traceFile );
        fclose( traceFile );
        traceFile = NULL;
    }
}

int traceEnabled( void )
{
    return traceFile != NULL;
}

void traceInstruction( TriCpu* cpu, TriWord inst )
{
    TriOpInstruction fields;
    if( traceFile == NULL )
    {
        return;
    }
    parseInstruction( &fields, inst );
    fprintf( traceFile,
             "{\"seq\":%lu,\"event\":\"insn\",\"pc\":%"PRIi64","
             "\"inst\":%"PRIi64",\"opcode\":%u,\"r1\":%u,\"r2\":%u,"
             "\"r2mod\":%u,\"r3\":%u,\"r3mod\":%u,\"imm11\":%"PRIi64","
             "\"imm9_1\":%"PRIi64",\"imm9_2\":%"PRIi64"}\n",
             traceSeq++,
             (int64_t)ternary2int( cpu->regs[REG_PC] ),
             (int64_t)inst,
             (unsigned)fields.opcode,
             (unsigned)fields.r1,
             (unsigned)fields.r2,
             (unsigned)fields.r2mod,
             (unsigned)fields.r3,
             (unsigned)fields.r3mod,
             (int64_t)fields.imm11,
             (int64_t)fields.imm9_1,
             (int64_t)fields.imm9_2 );
}

void traceMemory( int isWrite, TriWord addr, Tryte value )
{
    if( traceFile == NULL )
    {
        return;
    }
    fprintf( traceFile,
             "{\"seq\":%lu,\"event\":\"mem\",\"rw\":\"%s\","
             "\"addr\":%"PRIi64",\"value\":%"PRIi64"}\n",
             traceSeq++,
             isWrite ? "w" : "r",
             (int64_t)ternary2int( addr ),
             (int64_t)ternary2int( (trint32_t)value ) );
}

void traceSetTaint( unsigned long regMask, unsigned long memCount )
{
    if( traceFile == NULL )
    {
        return;
    }
    if( regMask == lastTaintMask && memCount == lastTaintMem )
    {
        return;
    }
    lastTaintMask = regMask;
    lastTaintMem = memCount;
    fprintf( traceFile,
             "{\"seq\":%lu,\"event\":\"taint\",\"reg_mask\":%lu,"
             "\"mem_count\":%lu}\n",
             traceSeq++, regMask, memCount );
}

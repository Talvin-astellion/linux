#include "symbolic.h"
#include "disassem.h"

#include <string.h>

static FILE* symFile = NULL;
static int symOn = 0;
static unsigned long symSeq = 0;
static unsigned long symTotal = 0;
static SymConstraint constraints[SYM_MAX_CONSTRAINTS];

int symInit( const char* path )
{
    symShutdown();
    if( path != NULL && path[0] != '\0' )
    {
        symFile = fopen( path, "w" );
        if( symFile == NULL )
        {
            return -1;
        }
    }
    symOn = 1;
    symSeq = 0;
    symTotal = 0;
    return 0;
}

void symShutdown( void )
{
    if( symFile != NULL )
    {
        fflush( symFile );
        fclose( symFile );
        symFile = NULL;
    }
    symOn = 0;
}

int symEnabled( void )
{
    return symOn;
}

void symStep( TriCpu* cpu, TriWord inst )
{
    TriOpInstruction fields;
    FlagReg flags;
    SymConstraint c;

    if( !symOn )
    {
        return;
    }
    parseInstruction( &fields, inst );
    if( fields.opcode != OPCODE_ADS && fields.opcode != OPCODE_ADSI )
    {
        return;
    }

    flags.val = cpu->regs[REG_FLAGS];
    memset( &c, 0, sizeof( c ) );
    c.seq = symTotal;
    c.opcode = (int)fields.opcode;
    c.r1 = fields.r1;
    c.r2 = fields.r2;
    c.r3 = fields.r3;
    c.immediate = ( fields.opcode == OPCODE_ADS ) ? 0 : fields.imm9_1;
    c.sign = flags.flags.S;
    c.taken = ( c.sign == 1 ) ? 1 : ( ( c.sign == N ) ? -1 : 0 );
    snprintf( c.kind, sizeof( c.kind ), "%s",
              fields.opcode == OPCODE_ADS ? "ADS" : "ADSI" );

    constraints[symTotal % SYM_MAX_CONSTRAINTS] = c;
    if( symFile != NULL )
    {
        fprintf( symFile,
                 "{\"seq\":%lu,\"event\":\"constraint\",\"kind\":\"%s\","
                 "\"pc\":%"PRIi64",\"r1\":%u,\"r2\":%u,\"r3\":%u,"
                 "\"imm\":%"PRIi64",\"sign\":%d,\"taken\":%d}\n",
                 symSeq++,
                 c.kind,
                 (int64_t)ternary2int( cpu->regs[REG_PC] ),
                 (unsigned)c.r1,
                 (unsigned)c.r2,
                 (unsigned)c.r3,
                 (int64_t)c.immediate,
                 c.sign,
                 c.taken );
    }
    ++symTotal;
}

unsigned long symCount( void )
{
    return symTotal < SYM_MAX_CONSTRAINTS ? symTotal : SYM_MAX_CONSTRAINTS;
}

const SymConstraint* symGet( unsigned long index )
{
    if( index >= symCount() )
    {
        return NULL;
    }
    {
        unsigned long start = symTotal > SYM_MAX_CONSTRAINTS
                              ? symTotal - SYM_MAX_CONSTRAINTS : 0;
        return &constraints[( start + index ) % SYM_MAX_CONSTRAINTS];
    }
}

void symPrint( FILE* out )
{
    unsigned long i;
    unsigned long count;
    if( out == NULL )
    {
        return;
    }
    if( !symOn )
    {
        fprintf( out, "symbolic: disabled\n" );
        return;
    }
    count = symCount();
    fprintf( out, "symbolic: %lu conditional constraints\n", count );
    for( i = 0; i < count; ++i )
    {
        const SymConstraint* c = symGet( i );
        fprintf( out,
                 "  [%lu] %s r1=%u r2=%u r3=%u sign=%d taken=%d\n",
                 c->seq, c->kind, (unsigned)c->r1, (unsigned)c->r2,
                 (unsigned)c->r3, c->sign, c->taken );
    }
}

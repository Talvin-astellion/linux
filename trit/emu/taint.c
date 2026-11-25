#include "taint.h"
#include "disassem.h"
#include "mem.h"

#include <stdlib.h>

typedef struct
{
    int set;
    uint8_t taint;
} PendingReg;

typedef struct
{
    int64_t index;
    uint8_t taint;
} PendingMem;

static int taintOn = 0;
static uint8_t* regTaint = NULL;
static uint8_t* memTaint = NULL;
static unsigned long memTaintCount = 0;
static PendingReg pendingReg[NUM_TRIREGS];
static Tryte pendingRegIndex[8];
static unsigned pendingRegCount = 0;
static PendingMem pendingMem[8];
static unsigned pendingMemCount = 0;

static int validReg( Tryte reg )
{
    unsigned i;
    if( reg >= NUM_TRIREGS )
    {
        return 0;
    }
    for( i = 0; i < 3; ++i )
    {
        if( ( ( reg >> ( i * BITS_PER_TRIT ) ) & TRIT_MASK ) == 2 )
        {
            return 0;
        }
    }
    return 1;
}

int taintInit( void )
{
    taintShutdown();
    regTaint = (uint8_t*)calloc( NUM_TRIREGS, sizeof( uint8_t ) );
    memTaint = (uint8_t*)calloc( RAM_SIZE, sizeof( uint8_t ) );
    if( regTaint == NULL || memTaint == NULL )
    {
        taintShutdown();
        return -1;
    }
    memTaintCount = 0;
    taintOn = 1;
    return 0;
}

void taintShutdown( void )
{
    free( regTaint );
    free( memTaint );
    regTaint = NULL;
    memTaint = NULL;
    memTaintCount = 0;
    taintOn = 0;
}

int taintEnabled( void )
{
    return taintOn;
}

void taintEnable( int enabled )
{
    taintOn = enabled != 0;
}

void taintMarkReg( Tryte reg )
{
    if( taintOn && regTaint != NULL && validReg( reg ) && reg != 0 )
    {
        regTaint[reg] = 1;
    }
}

void taintMarkMem( TriWord addr )
{
    int64_t index;
    if( taintOn && memTaint != NULL && ramIndex( addr, &index ) == 0 )
    {
        if( !memTaint[index] )
        {
            ++memTaintCount;
        }
        memTaint[index] = 1;
    }
}

int taintReg( Tryte reg )
{
    if( !taintOn || regTaint == NULL || !validReg( reg ) )
    {
        return 0;
    }
    return regTaint[reg] != 0;
}

int taintMem( TriWord addr )
{
    int64_t index;
    if( !taintOn || memTaint == NULL || ramIndex( addr, &index ) != 0 )
    {
        return 0;
    }
    return memTaint[index] != 0;
}

unsigned long taintRegMask( void )
{
    unsigned long mask = 0;
    unsigned i;
    if( !taintOn || regTaint == NULL )
    {
        return 0;
    }
    for( i = 0; i < NUM_TRIREGS; ++i )
    {
        if( regTaint[i] )
        {
            mask |= 1UL << i;
        }
    }
    return mask;
}

unsigned long taintMemCount( void )
{
    return taintOn ? memTaintCount : 0;
}

static uint8_t regT( TriCpu* cpu, Tryte reg )
{
    (void)cpu;
    return (uint8_t)taintReg( reg );
}

static uint8_t memT( TriWord addr )
{
    int64_t index;
    if( !taintOn || memTaint == NULL || ramIndex( addr, &index ) != 0 )
    {
        return 0;
    }
    return memTaint[index];
}

static uint8_t memRangeT( TriWord addr, int count )
{
    uint8_t taint = memT( addr );
    if( count >= 2 )
    {
        taint |= memT( ternaryADD( addr, 0b0001 ) );
    }
    if( count >= 3 )
    {
        taint |= memT( ternaryADD( addr, 0b0111 ) );
    }
    return taint;
}

static void pendReg( Tryte reg, uint8_t taint )
{
    if( validReg( reg ) && reg != 0 )
    {
        if( !pendingReg[reg].set )
        {
            if( pendingRegCount < sizeof( pendingRegIndex ) / sizeof( pendingRegIndex[0] ) )
            {
                pendingRegIndex[pendingRegCount++] = reg;
            }
            pendingReg[reg].set = 1;
        }
        pendingReg[reg].taint = taint;
    }
}

static void pendMem( TriWord addr, uint8_t taint )
{
    int64_t index;
    if( pendingMemCount >= sizeof( pendingMem ) / sizeof( pendingMem[0] ) )
    {
        return;
    }
    if( ramIndex( addr, &index ) == 0 )
    {
        pendingMem[pendingMemCount].index = index;
        pendingMem[pendingMemCount].taint = taint;
        ++pendingMemCount;
    }
}

static void pendMemRange( TriWord addr, int count, uint8_t taint )
{
    pendMem( addr, taint );
    if( count >= 2 )
    {
        pendMem( ternaryADD( addr, 0b0001 ), taint );
    }
    if( count >= 3 )
    {
        pendMem( ternaryADD( addr, 0b0111 ), taint );
    }
}

void taintStepBefore( TriCpu* cpu, TriWord inst )
{
    TriOpInstruction fields;
    uint8_t taint;
    TriWord addr;

    if( !taintOn || regTaint == NULL || memTaint == NULL )
    {
        return;
    }
    pendingRegCount = 0;
    pendingMemCount = 0;
    parseInstruction( &fields, inst );

    switch( fields.opcode )
    {
        case OPCODE_LD1:
            addr = ternaryADD( ReadModReg( cpu, fields.r2, fields.r2mod ), fields.imm11 );
            pendReg( fields.r1, memRangeT( addr, 1 ) );
            break;
        case OPCODE_LD2:
            addr = ternaryADD( ReadModReg( cpu, fields.r2, fields.r2mod ), fields.imm11 );
            pendReg( fields.r1, memRangeT( addr, 2 ) );
            break;
        case OPCODE_LD3:
            addr = ternaryADD( ReadModReg( cpu, fields.r2, fields.r2mod ), fields.imm11 );
            pendReg( fields.r1, memRangeT( addr, 3 ) );
            break;
        case OPCODE_POP:
            addr = ReadModReg( cpu, fields.r2, fields.r2mod );
            pendReg( fields.r1, memRangeT( addr, 3 ) );
            pendReg( fields.r2, regT( cpu, fields.r2 ) );
            break;
        case OPCODE_ST1:
            addr = ternaryADD( ReadModReg( cpu, fields.r1, OPMOD_NOP ), fields.imm11 );
            pendMemRange( addr, 1, regT( cpu, fields.r2 ) );
            break;
        case OPCODE_ST2:
            addr = ternaryADD( ReadModReg( cpu, fields.r1, OPMOD_NOP ), fields.imm11 );
            pendMemRange( addr, 2, regT( cpu, fields.r2 ) );
            break;
        case OPCODE_ST3:
        case OPCODE_PSH:
            addr = ternaryADD( ReadModReg( cpu, fields.r1, OPMOD_NOP ), fields.imm11 );
            pendMemRange( addr, 3, regT( cpu, fields.r2 ) );
            if( fields.opcode == OPCODE_PSH )
            {
                pendReg( fields.r1, regT( cpu, fields.r1 ) );
            }
            break;
        case OPCODE_ADC:
        case OPCODE_ADD:
        case OPCODE_ADS:
            taint = regT( cpu, fields.r1 ) | regT( cpu, fields.r2 ) | regT( cpu, fields.r3 );
            pendReg( fields.r1, taint );
            break;
        case OPCODE_ADCI:
        case OPCODE_ADDI:
        case OPCODE_ADSI:
            pendReg( fields.r1, regT( cpu, fields.r1 ) );
            break;
        case OPCODE_MUL:
        case OPCODE_MULU:
            pendReg( fields.r1, regT( cpu, fields.r2 ) | regT( cpu, fields.r3 ) );
            break;
        case OPCODE_MULI:
            pendReg( fields.r1, regT( cpu, fields.r2 ) );
            break;
        case OPCODE_RTL:
            taint = regT( cpu, fields.r1 ) | regT( cpu, fields.r2 ) | regT( cpu, fields.r3 );
            pendReg( fields.r1, taint );
            pendReg( fields.r2, taint );
            break;
        case OPCODE_TAND:
        case OPCODE_TOR:
        case OPCODE_TMAJ:
        case OPCODE_TADD:
        case OPCODE_TMUL:
        case OPCODE_TMIN:
        case OPCODE_TMAX:
            pendReg( fields.r1, regT( cpu, fields.r2 ) | regT( cpu, fields.r3 ) );
            break;
        case OPCODE_RTLI:
            taint = regT( cpu, fields.r1 ) | regT( cpu, fields.r2 );
            pendReg( fields.r1, taint );
            pendReg( fields.r2, taint );
            break;
        case OPCODE_TANDI:
        case OPCODE_TORI:
        case OPCODE_TMAJI:
        case OPCODE_TADDI:
        case OPCODE_TMULI:
        case OPCODE_TMINI:
        case OPCODE_TMAXI:
            pendReg( fields.r1, regT( cpu, fields.r2 ) );
            break;
        case OPCODE_NOP:
        default:
            break;
    }
}

void taintStepAfter( TriCpu* cpu )
{
    unsigned i;
    (void)cpu;
    if( !taintOn || regTaint == NULL || memTaint == NULL )
    {
        return;
    }
    for( i = 0; i < pendingRegCount; ++i )
    {
        Tryte reg = pendingRegIndex[i];
        regTaint[reg] = pendingReg[reg].taint;
        pendingReg[reg].set = 0;
    }
    pendingRegCount = 0;
    for( i = 0; i < pendingMemCount; ++i )
    {
        int64_t index = pendingMem[i].index;
        uint8_t taint = pendingMem[i].taint;
        if( taint && !memTaint[index] )
        {
            ++memTaintCount;
        }
        else if( !taint && memTaint[index] )
        {
            --memTaintCount;
        }
        memTaint[index] = taint;
    }
    pendingMemCount = 0;
}

void taintPrint( FILE* out )
{
    unsigned i;
    unsigned shown = 0;
    if( out == NULL )
    {
        return;
    }
    if( !taintOn || regTaint == NULL )
    {
        fprintf( out, "taint: disabled\n" );
        return;
    }
    fprintf( out, "taint: %lu tainted registers, %lu tainted trytes\n",
             taintRegMask() == 0 ? 0UL : (unsigned long)__builtin_popcountl( taintRegMask() ),
             memTaintCount );
    for( i = 0; i < NUM_TRIREGS; ++i )
    {
        if( regTaint[i] )
        {
            fprintf( out, "  reg %u\n", i );
        }
    }
    for( i = 0; i < RAM_SIZE && shown < 20; ++i )
    {
        if( memTaint[i] )
        {
            TriWord addr = ternaryFromInt64( (int64_t)i - BASE_OFFSET );
            fprintf( out, "  mem %"PRIi64"\n", (int64_t)addr );
            ++shown;
        }
    }
}

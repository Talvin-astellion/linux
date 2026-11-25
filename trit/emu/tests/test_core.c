#include "../cpu.h"
#include "../disassem.h"
#include "../mem.h"

#include <stdio.h>

static int failures = 0;

#define CHECK(cond) do { \
    if( !(cond) ) { \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        ++failures; \
    } \
} while( 0 )

static void testIntegerRoundTrip( void )
{
    static const int64_t values[] = {
        -1000000, -81, -13, -1, 0, 1, 13, 81, 1000000,
        926510094425920LL, -926510094425920LL
    };
    unsigned i;
    for( i = 0; i < sizeof( values ) / sizeof( values[0] ); ++i )
    {
        trint32_t tri = ternaryFromInt64( values[i] );
        CHECK( ternary2int( tri ) == values[i] );
    }
}

static void testParse( void )
{
    trint32_t value;
    CHECK( ternaryParse( "0", &value ) == 1 );
    CHECK( ternary2int( value ) == 0 );
    CHECK( ternaryParse( "10-", &value ) == 3 );
    CHECK( ternary2int( value ) == 8 );
    CHECK( ternaryParse( "-1", &value ) == 2 );
    CHECK( ternary2int( value ) == -2 );
    CHECK( ternaryParse( "", &value ) < 0 );
    CHECK( ternaryParse( "x@y", &value ) < 0 );
}

static void testMulHi( void )
{
    trint32_t a = ternaryFromInt64( 4782969LL );
    CHECK( ternary2int( ternaryMULHI( a, a ) ) == 3 );
    CHECK( ternary2int( ternaryMULHI( 0, a ) ) == 0 );
}

static void testRotatePair( void )
{
    trint32_t high = ternaryFromInt64( 3 );
    trint32_t low = ternaryFromInt64( 1 );
    ternaryROL( &high, &low, ternaryFromInt64( 27 ), TRITS_PER_WORD );
    CHECK( ternary2int( high ) == 1 );
    CHECK( ternary2int( low ) == 3 );
}

static void testRegisterValidation( void )
{
    TriCpu cpu;
    resetMem();
    resetCPU( &cpu );

    cpu.regs[REG_P01] = ternaryFromInt64( 42 );
    CHECK( ternary2int( ReadModReg( &cpu, REG_P01, OPMOD_NOP ) ) == 42 );

    WriteReg( &cpu, 0x02, ternaryFromInt64( 7 ) );
    CHECK( ternary2int( cpu.regs[2] ) == 0 );
    CHECK( ternary2int( ReadModReg( &cpu, 0x02, OPMOD_NOP ) ) == 0 );

    /* The NOP gate maps undefined trits to zero; the fast path must skip. */
    cpu.regs[REG_P01] = (TriWord)2 << ( 2 * BITS_PER_TRIT );
    CHECK( ReadModReg( &cpu, REG_P01, OPMOD_NOP ) == 0 );

    cpu.regs[REG_P01] = ( (TriWord)2 << ( 2 * BITS_PER_TRIT ) ) |
                        ( (TriWord)1 << ( 4 * BITS_PER_TRIT ) );
    CHECK( ReadModReg( &cpu, REG_P01, OPMOD_NOP ) ==
           ( (TriWord)1 << ( 4 * BITS_PER_TRIT ) ) );

    /* The shift select runs before the identity check. */
    cpu.regs[REG_P01] = ( (TriWord)2 << ( 20 * BITS_PER_TRIT ) ) |
                        ternaryFromInt64( 3 );
    CHECK( ReadModReg( &cpu, REG_P01,
                       OPMOD_NOP | ( (Tryte)N << ( 3 * BITS_PER_TRIT ) ) ) == 0 );
}

static void testMemory( void )
{
    TriWord value = ternaryFromInt64( 12345 );
    resetMem();
    CHECK( ternary2int( ReadAddr( 0 ) ) == 0 );
    WriteAddr( 0, value );
    CHECK( ReadAddr( 0 ) == value );
    resetMem();
    CHECK( ternary2int( ReadAddr( 0 ) ) == 0 );
}

static void testRamAddressDecoder( void )
{
    uint32_t bits;
    int64_t index;
    TriWord low = ternaryFromInt64( -BASE_OFFSET );
    TriWord high = ternaryFromInt64( BASE_OFFSET );

    /* Exhaust every 22-bit encoding, including undefined trits, against the old decoder. */
    for( bits = 0; bits <= RAM_ADDR_MASK; ++bits )
    {
        int64_t expected = BASE_OFFSET + ternary2int( bits );
        int valid = expected >= 0 && expected < RAM_SIZE;
        CHECK( ramIndex( bits, &index ) == ( valid ? 0 : -1 ) );
        if( valid )
        {
            CHECK( index == expected );
        }
        CHECK( ramIndex( bits | ~(TriWord)RAM_ADDR_MASK, &index ) == ( valid ? 0 : -1 ) );
        if( valid )
        {
            CHECK( index == expected );
        }
    }

    resetMem();
    WriteAddr( low, 1 );
    WriteAddr( high, N );
    CHECK( ReadAddr( low ) == 1 );
    CHECK( ReadAddr( high ) == N );
    CHECK( ReadRamTryte( ternaryFromInt64( -BASE_OFFSET - RAM_SIZE ) ) == 1 );
    CHECK( ReadRamTryte( ternaryFromInt64( BASE_OFFSET + RAM_SIZE ) ) == N );

    /* An MMIO address's RAM alias must remain RAM, not access the device. */
    WriteAddr( ternaryFromInt64( RAM_SIZE - 26 ), 1 );
    CHECK( ReadRamTryte( ADDR_UART_STATUS ) == 1 );
    CHECK( ReadAddr( ternaryFromInt64( RAM_SIZE - 26 ) ) == 1 );
}

static TriWord makeRegInstruction( TriOpcode opcode, TriReg r1, TriReg r2,
                                   TriReg r3 )
{
    TriWord inst = opcode;
    inst |= (TriWord)r1 << 12;
    inst |= (TriWord)OPMOD_NOP << 18;
    inst |= (TriWord)r2 << 26;
    inst |= (TriWord)OPMOD_NOP << 32;
    inst |= (TriWord)r3 << 40;
    return inst;
}

static void testDecode( void )
{
    TriOpInstruction fields;
    TriWord inst;
    int pass, i;

    /* Zero is a real instruction word, including on a cold cache. */
    parseInstruction( &fields, 0 );
    CHECK( fields.opcode == OPCODE_NOP );
    CHECK( fields.r1 == REG_ZERO );
    CHECK( fields.r2 == REG_ZERO );
    CHECK( fields.r3 == REG_ZERO );
    CHECK( fields.r2mod == 0 );
    CHECK( fields.r3mod == 0 );
    CHECK( fields.imm11 == 0 );
    CHECK( fields.imm9_1 == 0 );
    CHECK( fields.imm9_2 == 0 );

    inst = OPCODE_ADSI | ( (TriWord)REG_P01 << 12 ) |
           ( UINT64_C(0b00010101) << 18 ) | ( UINT64_C(0b00111111) << 36 );
    parseInstruction( &fields, inst );
    CHECK( fields.imm9_1 == ternaryFromInt64( 3812798742493LL ) );
    CHECK( fields.imm9_2 == TRIWORD_MASK );
    fields.imm9_1 = 0;
    parseInstruction( &fields, inst );
    CHECK( fields.imm9_1 == ternaryFromInt64( 3812798742493LL ) );

    /* Exercise more distinct words than the cache can hold, then revisit them. */
    for( pass = 0; pass < 2; ++pass )
    {
        for( i = 0; i < 4096; ++i )
        {
            int n = pass ? 4095 - i : i;
            TriReg r1 = (TriReg)ternaryFromInt64( n % 27 - 13 );
            TriReg r2 = (TriReg)ternaryFromInt64( n / 27 % 27 - 13 );
            TriReg r3 = (TriReg)ternaryFromInt64( n / 729 % 27 - 13 );
            r1 &= 0x3F;
            r2 &= 0x3F;
            r3 &= 0x3F;
            inst = makeRegInstruction( OPCODE_ADD, r1, r2, r3 );
            parseInstruction( &fields, inst );
            CHECK( fields.opcode == OPCODE_ADD );
            CHECK( fields.r1 == r1 );
            CHECK( fields.r2 == r2 );
            CHECK( fields.r3 == r3 );
            CHECK( fields.r2mod == OPMOD_NOP );
            CHECK( fields.r3mod == OPMOD_NOP );
        }
    }
}

static void testInstructionRewrite( void )
{
    TriCpu cpu;
    TriWord pc = ternaryFromInt64( 100 );
    TriWord inst = makeRegInstruction( OPCODE_ADD, REG_P01, REG_P02, REG_P03 );
    TriWord replacement;
    TriWord store;

    resetMem();
    resetCPU( &cpu );
    WriteReg( &cpu, REG_P02, ternaryFromInt64( 7 ) );
    WriteReg( &cpu, REG_P03, ternaryFromInt64( 11 ) );
    WriteReg( &cpu, REG_P04, ternaryFromInt64( 13 ) );
    WriteTriWord( &cpu, pc, inst );
    cpu.regs[REG_PC] = pc;
    runCPU( &cpu, 1 );
    CHECK( triwordToInt( cpu.regs[REG_P01] ) == 18 );

    /* Patching only the high tryte through a RAM alias must change decoding. */
    replacement = makeRegInstruction( OPCODE_ADD, REG_P01, REG_P02, REG_P04 );
    WriteAddr( ternaryFromInt64( 102 + RAM_SIZE ),
               (Tryte)( replacement >> ( 2 * BITS_PER_TRYTE ) ) );
    cpu.regs[REG_PC] = pc;
    runCPU( &cpu, 1 );
    CHECK( triwordToInt( cpu.regs[REG_P01] ) == 20 );
    cpu.regs[REG_PC] = ternaryFromInt64( 100 + RAM_SIZE );
    runCPU( &cpu, 1 );
    CHECK( triwordToInt( cpu.regs[REG_P01] ) == 20 );

    /* A guest ST3 can replace a previously executed instruction as well. */
    replacement = makeRegInstruction( OPCODE_MUL, REG_P01, REG_P02, REG_P03 );
    WriteReg( &cpu, REG_P05, pc );
    WriteReg( &cpu, REG_P06, replacement );
    store = OPCODE_ST3 | ( (TriWord)REG_P05 << 12 ) |
            ( (TriWord)OPMOD_NOP << 18 ) | ( (TriWord)REG_P06 << 26 );
    WriteTriWord( &cpu, ternaryFromInt64( 103 ), store );
    cpu.regs[REG_PC] = ternaryFromInt64( 103 );
    runCPU( &cpu, 1 );
    cpu.regs[REG_PC] = pc;
    runCPU( &cpu, 1 );
    CHECK( triwordToInt( cpu.regs[REG_P01] ) == 77 );
    CHECK( cpu.halted == 0 );
    CHECK( cpu.trap == 0 );

    resetMem();
    resetCPU( &cpu );
    cpu.regs[REG_PC] = pc;
    runCPU( &cpu, 1 );
    CHECK( triwordToInt( cpu.regs[REG_P01] ) == 0 );
    CHECK( triwordToInt( cpu.regs[REG_CLOCK] ) == 1 );
}

static void testHaltAndClock( void )
{
    TriCpu cpu;
    TriWord a0 = ternaryFromInt64( 0 );
    TriWord a1 = ternaryFromInt64( 3 );
    TriWord a2 = ternaryFromInt64( 6 );
    resetMem();
    resetCPU( &cpu );
    CHECK( triwordToInt( cpu.regs[REG_CLOCK] ) == 0 );

    WriteTriWord( &cpu, a0, OPCODE_NOP );
    WriteTriWord( &cpu, a1, OPCODE_HALT );
    WriteTriWord( &cpu, a2, OPCODE_NOP );
    cpu.regs[REG_PC] = a0;
    runCPU( &cpu, 5 );

    CHECK( cpu.halted == 1 );
    CHECK( cpu.trap == 0 );
    CHECK( triwordToInt( cpu.regs[REG_CLOCK] ) == 2 );

    runCPU( &cpu, 1 );
    CHECK( triwordToInt( cpu.regs[REG_CLOCK] ) == 2 );

    resetCPU( &cpu );
    CHECK( cpu.halted == 0 );
    CHECK( cpu.trap == 0 );
}

static void testTraps( void )
{
    TriCpu cpu;
    TriWord invalidRegister;

    resetMem();
    resetCPU( &cpu );
    WriteTriWord( &cpu, 0, 0b111111101010 );
    cpu.regs[REG_PC] = 0;
    runCPU( &cpu, 1 );
    CHECK( cpu.halted == 1 );
    CHECK( cpu.trap == TRAP_INVALID_OPCODE );

    resetMem();
    resetCPU( &cpu );
    invalidRegister = makeRegInstruction( OPCODE_ADD, REG_P01, 0x02, REG_ZERO );
    WriteTriWord( &cpu, 0, invalidRegister );
    cpu.regs[REG_PC] = 0;
    runCPU( &cpu, 1 );
    CHECK( cpu.halted == 1 );
    CHECK( cpu.trap == TRAP_INVALID_REGISTER );
}

static void testWordWidthAndFlags( void )
{
    TriCpu cpu;
    FlagReg flags;
    TriWord inst;

    resetMem();
    resetCPU( &cpu );
    cpu.regs[REG_PC] = 0;

    WriteReg( &cpu, REG_P02, ternaryFromInt64( -3 ) );
    CHECK( triwordToInt( cpu.regs[REG_P02] ) == -3 );

    inst = makeRegInstruction( OPCODE_ADD, REG_P01, REG_ZERO, REG_P02 );
    WriteTriWord( &cpu, 0, inst );
    runCPU( &cpu, 1 );

    CHECK( triwordToInt( cpu.regs[REG_P01] ) == -3 );
    CHECK( triwordToInt( cpu.regs[REG_CLOCK] ) == 1 );
    flags.val = cpu.regs[REG_FLAGS];
    CHECK( flags.flags.S == N );
    CHECK( flags.flags.C == 0 );
    CHECK( flags.flags.V == 0 );
    CHECK( flags.flags.P == N );
}

static trint32_t referenceAdd( trint32_t op1, trint32_t op2 )
{
    static const uint8_t sums[7] = { 0, 1, 3, 0, 1, 3, 0 };
    static const int8_t carries[7] = { -1, -1, 0, 0, 0, 1, 1 };
    trint32_t result = 0;
    int carry = 0;
    unsigned shift;

    for( shift = 0; shift < BITS_PER_TRINT32_T; shift += BITS_PER_TRIT )
    {
        unsigned a = ( op1 >> shift ) & TRIT_MASK;
        unsigned b = ( op2 >> shift ) & TRIT_MASK;
        int sum;
        if( a == 2 || b == 2 )
        {
            result |= (trint32_t)2 << shift;
            carry = 0;
            continue;
        }
        sum = ( ( a == N ) ? -1 : (int)a ) +
              ( ( b == N ) ? -1 : (int)b ) + carry;
        result |= (trint32_t)sums[sum + 3] << shift;
        carry = carries[sum + 3];
    }
    return result;
}

static uint64_t nextRandom( uint64_t* state )
{
    uint64_t x = *state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *state = x;
    return x;
}

static void testAddFastPath( void )
{
    static const trint32_t edge[] = {
        0, 1, 2, TRIWORD_MASK, UINT64_MAX,
        UINT64_C(0xAAAAAAAAAAAAAAAA), UINT64_C(0x5555555555555555),
        UINT64_C(0xFFFFFFFFFFFFFFFF), UINT64_C(0x4000000000000000),
    };
    static const int64_t values[] = {
        0, 1, -1, 3, -3, 3812798742493LL, -3812798742493LL
    };
    static const trint32_t constants[] = { 0, 1, 2, 4, 7, 13, -1, -4 };
    uint64_t state = UINT64_C(0x9e3779b97f4a7c15);
    unsigned i, j;

    for( i = 0; i < sizeof( edge ) / sizeof( edge[0] ); ++i )
    {
        for( j = 0; j < sizeof( edge ) / sizeof( edge[0] ); ++j )
        {
            CHECK( ternaryADD( edge[i], edge[j] ) == referenceAdd( edge[i], edge[j] ) );
        }
    }
    for( i = 0; i < sizeof( values ) / sizeof( values[0] ); ++i )
    {
        trint32_t a = ternaryFromInt64( values[i] );
        for( j = 0; j < sizeof( edge ) / sizeof( edge[0] ); ++j )
        {
            CHECK( ternaryADD( a, edge[j] ) == referenceAdd( a, edge[j] ) );
            CHECK( ternaryADD( edge[j], a ) == referenceAdd( edge[j], a ) );
        }
    }
    for( i = 0; i < 200000; ++i )
    {
        trint32_t a = nextRandom( &state );
        trint32_t b = nextRandom( &state );
        CHECK( ternaryADD( a, b ) == referenceAdd( a, b ) );
        CHECK( ternaryADD( a, ternaryFromInt64( constants[i % 8] ) ) ==
               referenceAdd( a, ternaryFromInt64( constants[i % 8] ) ) );
        CHECK( ternaryADD( ternaryFromInt64( constants[( i + 3 ) % 8] ), b ) ==
               referenceAdd( ternaryFromInt64( constants[( i + 3 ) % 8] ), b ) );
    }
    /* Long carry chains must still propagate past the fast-path check. */
    {
        int64_t pow3 = 1;
        for( i = 1; i <= TRITS_PER_TRINT32_T; ++i )
        {
            trint32_t ones;
            trint32_t minusOnes;
            pow3 *= 3;
            ones = ternaryFromInt64( ( pow3 - 1 ) / 2 );
            minusOnes = ternaryFromInt64( -( ( pow3 - 1 ) / 2 ) );
            CHECK( ternaryADD( ones, ternaryFromInt64( 1 ) ) ==
                   referenceAdd( ones, ternaryFromInt64( 1 ) ) );
            CHECK( ternaryADD( minusOnes, ternaryFromInt64( -1 ) ) ==
                   referenceAdd( minusOnes, ternaryFromInt64( -1 ) ) );
        }
    }
}

static TriWord referenceExpandImmediate( TriWord input )
{
    if( 0b00111111 == input )
    {
        return (-1LL) & TRIWORD_MASK;
    }
    if( 0b00010101 == input )
    {
        return 0b00010101010101010101010101010101010101010101010101010101ULL;
    }
    {
        int shift = ( ternary2int( input & 0b00111111 ) + 13 ) * 2;
        input >>= 6;
        return ( input << shift ) & TRIWORD_MASK;
    }
}

static void testImmediateExpansion( void )
{
    TriOpInstruction fields;
    uint64_t state = UINT64_C(0x13198a2e03707344);
    unsigned i;

    for( i = 0; i < 50000; ++i )
    {
        TriWord base = nextRandom( &state );
        TriWord field = nextRandom( &state ) & UINT64_C(0x003FFFFF);
        TriWord trits = 0;
        unsigned k;

        for( k = 0; k < 3; ++k )
        {
            unsigned choice = (unsigned)( nextRandom( &state ) % 3 );
            trit_t t = ( choice == 2 ) ? N : (trit_t)choice;
            trits |= (TriWord)t << ( k * BITS_PER_TRIT );
        }
        field = ( field & ~UINT64_C(0x3F) ) | trits;

        {
            TriWord inst = ( base & ~( UINT64_C(0x3FFFFF) << 32 ) ) |
                           ( field << 32 );
            parseInstruction( &fields, inst );
            CHECK( fields.imm11 == referenceExpandImmediate( field ) );
        }
        {
            TriWord field18 = field & UINT64_C(0x3FFFF);
            TriWord inst = ( base & ~( UINT64_C(0x3FFFF) << 18 ) ) |
                           ( field18 << 18 );
            parseInstruction( &fields, inst );
            CHECK( fields.imm9_1 == referenceExpandImmediate( field18 ) );
        }
        {
            TriWord field18 = field & UINT64_C(0x3FFFF);
            TriWord inst = ( base & ~( UINT64_C(0x3FFFF) << 36 ) ) |
                           ( field18 << 36 );
            parseInstruction( &fields, inst );
            CHECK( fields.imm9_2 == referenceExpandImmediate( field18 ) );
        }
    }
}

static trint32_t referenceAdc( trint32_t op1, trint32_t op2, trit_t* carry )
{
    static const uint8_t sums[7] = { 0, 1, 3, 0, 1, 3, 0 };
    static const int8_t carries[7] = { -1, -1, 0, 0, 0, 1, 1 };
    trint32_t result = 0;
    int carryin = ( *carry == N ) ? -1 : (int)*carry;
    unsigned shift;

    if( carryin == 2 )
    {
        carryin = 0;
    }
    for( shift = 0; shift < BITS_PER_TRINT32_T; shift += BITS_PER_TRIT )
    {
        unsigned a = ( op1 >> shift ) & TRIT_MASK;
        unsigned b = ( op2 >> shift ) & TRIT_MASK;
        int sum;
        if( a == 2 || b == 2 )
        {
            result |= (trint32_t)2 << shift;
            carryin = 0;
            continue;
        }
        sum = ( ( a == N ) ? -1 : (int)a ) +
              ( ( b == N ) ? -1 : (int)b ) + carryin;
        result |= (trint32_t)sums[sum + 3] << shift;
        carryin = carries[sum + 3];
    }
    *carry = ( carryin < 0 ) ? N : (trit_t)carryin;
    return result;
}

static void checkAdc( trint32_t a, trint32_t b, trit_t carry )
{
    trit_t actualCarry = carry;
    trit_t expectedCarry = carry;
    trint32_t actual = ternaryADC( a, b, &actualCarry );
    trint32_t expected = referenceAdc( a, b, &expectedCarry );
    CHECK( actual == expected );
    CHECK( actualCarry == expectedCarry );
}

static void testAdcFastPath( void )
{
    static const trint32_t edge[] = {
        0, 1, 2, TRIWORD_MASK, UINT64_MAX,
        UINT64_C(0xAAAAAAAAAAAAAAAA), UINT64_C(0x5555555555555555),
        UINT64_C(0xFFFFFFFFFFFFFFFF), UINT64_C(0x4000000000000000),
    };
    static const trit_t carryValues[] = { 0, 1, N, 2 };
    static const trint32_t constants[] = { 0, 1, 2, 4, 7, 13, -1, -4 };
    uint64_t state = UINT64_C(0x452821e638d01377);
    unsigned i, j, k;

    for( i = 0; i < sizeof( edge ) / sizeof( edge[0] ); ++i )
    {
        for( j = 0; j < sizeof( edge ) / sizeof( edge[0] ); ++j )
        {
            for( k = 0; k < sizeof( carryValues ) / sizeof( carryValues[0] ); ++k )
            {
                checkAdc( edge[i], edge[j], carryValues[k] );
            }
        }
    }
    for( i = 0; i < 200000; ++i )
    {
        trint32_t a = nextRandom( &state );
        trint32_t b = nextRandom( &state );
        trit_t carry = carryValues[i % 4];
        checkAdc( a, b, carry );
        checkAdc( a, ternaryFromInt64( constants[i % 8] ), carry );
        checkAdc( ternaryFromInt64( constants[( i + 3 ) % 8] ), b, carry );
    }

    {
        int64_t pow3 = 1;
        for( i = 1; i <= TRITS_PER_TRINT32_T; ++i )
        {
            trint32_t ones;
            trint32_t minusOnes;
            pow3 *= 3;
            ones = ternaryFromInt64( ( pow3 - 1 ) / 2 );
            minusOnes = ternaryFromInt64( -( ( pow3 - 1 ) / 2 ) );
            checkAdc( ones, ones, 0 );
            checkAdc( ones, ones, 1 );
            checkAdc( minusOnes, minusOnes, N );
        }
    }
}

static trit_t referenceParity( TriWord val )
{
    int nonzero = 0;
    int i;
    if( ( val & TRIWORD_MASK ) == 0 )
    {
        return 0;
    }
    for( i = 0; i < TRITS_PER_WORD; ++i )
    {
        if( ( ( val >> ( i * BITS_PER_TRIT ) ) & TRIT_MASK ) != 0 )
        {
            nonzero = !nonzero;
        }
    }
    return nonzero ? N : 1;
}

static TriWord randomDefinedTriWord( uint64_t* state )
{
    TriWord val = 0;
    int i;
    for( i = 0; i < TRITS_PER_WORD; ++i )
    {
        unsigned choice = (unsigned)( nextRandom( state ) % 3 );
        trit_t t = ( choice == 2 ) ? N : (trit_t)choice;
        val |= (TriWord)t << ( i * BITS_PER_TRIT );
    }
    return val;
}

static void testParityFlag( void )
{
    TriCpu cpu;
    TriWord inst = makeRegInstruction( OPCODE_ADD, REG_P01, REG_P02, REG_P03 );
    uint64_t state = UINT64_C(0x243f6a8885a308d3);
    unsigned i;

    resetMem();
    resetCPU( &cpu );
    WriteTriWord( &cpu, 0, inst );

    for( i = 0; i < 20000; ++i )
    {
        FlagReg flags;
        TriWord v = randomDefinedTriWord( &state );
        cpu.regs[REG_PC] = 0;
        cpu.regs[REG_FLAGS] = 0;
        cpu.regs[REG_P02] = v;
        cpu.regs[REG_P03] = 0;
        runCPU( &cpu, 1 );
        flags.val = cpu.regs[REG_FLAGS];
        CHECK( flags.flags.P == referenceParity( v ) );
    }

    {
        static const TriWord values[] = {
            0, 1, TRIWORD_MASK,
            ( (TriWord)1 << ( 2 * BITS_PER_TRIT ) ) |
            ( (TriWord)1 << ( 5 * BITS_PER_TRIT ) ),
        };
        for( i = 0; i < sizeof( values ) / sizeof( values[0] ); ++i )
        {
            FlagReg flags;
            cpu.regs[REG_PC] = 0;
            cpu.regs[REG_FLAGS] = 0;
            cpu.regs[REG_P02] = values[i];
            cpu.regs[REG_P03] = 0;
            runCPU( &cpu, 1 );
            flags.val = cpu.regs[REG_FLAGS];
            CHECK( flags.flags.P == referenceParity( values[i] ) );
        }
    }
}

static void testAddCarry( void )
{
    TriCpu cpu;
    TriWord maxv = ternaryFromInt64( 3812798742493LL );
    TriWord inst;
    FlagReg flags;

    resetMem();
    resetCPU( &cpu );

    inst = OPCODE_ADD;
    inst |= (TriWord)REG_P01 << 12;
    inst |= (TriWord)OPMOD_NOP << 18;
    inst |= (TriWord)REG_P02 << 26;
    inst |= (TriWord)OPMOD_NOP << 32;
    inst |= (TriWord)REG_P03 << 40;
    WriteTriWord( &cpu, 0, inst );

    cpu.regs[REG_PC] = 0;
    cpu.regs[REG_P02] = maxv;
    cpu.regs[REG_P03] = maxv;
    cpu.regs[REG_FLAGS] = 0;
    runCPU( &cpu, 1 );

    flags.val = cpu.regs[REG_FLAGS];
    CHECK( flags.flags.C == 1 );
    CHECK( flags.flags.V == 1 );
    CHECK( flags.flags.S == N );
    CHECK( triwordToInt( cpu.regs[REG_P01] ) == -1 );
}

int main( void )
{
    testIntegerRoundTrip();
    testParse();
    testMulHi();
    testRotatePair();
    testRegisterValidation();
    testMemory();
    testRamAddressDecoder();
    testDecode();
    testImmediateExpansion();
    testInstructionRewrite();
    testHaltAndClock();
    testTraps();
    testWordWidthAndFlags();
    testAddFastPath();
    testAdcFastPath();
    testParityFlag();
    testAddCarry();

    if( failures != 0 )
    {
        printf( "%d test(s) failed\n", failures );
        return 1;
    }
    printf( "all tests passed\n" );
    return 0;
}

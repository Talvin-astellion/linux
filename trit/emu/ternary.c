/****************************************************************************
 * Copyright (C) 2020 by Ted Kotz                                           *
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
 * @file    ternary.c
 * @author  Ted Kotz <ted@kotz.us>
 * @version 0.1
 *
 * This defines  basic ternary logical and mathematical operations.
 * The approach to this is not to translate the values to binary and then
 * do binary arithmatic, but to define lookup tables for the basic ternary gates
 * then combine those to build up the higher level math funtions.
 *
 */


/* Includes ******************************************************************/
#include "ternary.h"

#include <stdio.h>
/* Defines *******************************************************************/
#define TRINT32_MST_MASK (3ULL << (BITS_PER_TRINT32_T-BITS_PER_TRIT))

/* Types *********************************************************************/
/* Interfaces ****************************************************************/
/* Data **********************************************************************/

//                    A     X 1 0-1
#define TRIGATE_INC   (0b0010110100)
#define TRIGATE_DEC   (0b0010001101)
#define TRIGATE_NOT   (0b0010110001)
#define TRIGATE_FLT   (0b0010010000)
#define TRIGATE_LAX   (0b0010010100)
#define TRIGATE_ABS   (0b0010010001)

//                                       A 0  0  0  0    1  1  1  1   XXXXXXX   -1 -1 -1 -1
//                                       B 0  1  X -1    0  1  X -1              0  1  X -1
static const trit_t trigate_min[4][4] = {{ 0, 0, 2, N},{ 0, 1, 2, N},{2,2,2,2},{ N, N, 2, N}};
static const trit_t trigate_max[4][4] = {{ 0, 1, 2, 0},{ 1, 1, 2, 1},{2,2,2,2},{ 0, 1, 2, N}};
static const trit_t trigate_and[4][4] = {{ N, 0, 2, N},{ 0, 1, 2, N},{2,2,2,2},{ N, N, 2, N}};
static const trit_t trigate_orr[4][4] = {{ 1, 1, 2, 0},{ 1, 1, 2, 1},{2,2,2,2},{ 0, 1, 2, N}};
static const trit_t trigate_mul[4][4] = {{ 0, 0, 2, 0},{ 0, 1, 2, N},{2,2,2,2},{ 0, N, 2, 1}};
static const trit_t trigate_add[4][4] = {{ 0, 1, 2, N},{ 1, N, 2, 0},{2,2,2,2},{ N, 0, 2, 1}};
static const trit_t trigate_set[4][4] = {{ 0, 1, 2, N},{ 1, 1, 2, N},{2,2,2,2},{ N, 1, 2, N}};
static const trit_t trigate_imp[4][4] = {{ 1, 1, 2, 0},{ 0, 1, 2, N},{2,2,2,2},{ 1, 1, 2, 1}};
static const trit_t trigate_unm[4][4] = {{ 0, 0, 2, 0},{ 0, 1, 2, 0},{2,2,2,2},{ 0, 0, 2, N}};



/* Functions *****************************************************************/



/**
 * [Description]
 *
 * @param
 * @return
 */
int trit2int( trit_t x )
{
    return (x==N) ? -1 : x;
}

trit_t int2trit( int x )
{
    return (x==-1) ? N : x;
}

trit_t char2trit ( char c )
{
    switch(c)
    {
        case '-':
        case '_':
        case 'n':
        case 'N':
            return N;
            break;

        case '0':
        case ')':
        case 'z':
        case 'Z':
            return 0;
            break;

        case '1':
        case '!':
        case '+':
        case '=':
        case 'p':
        case 'P':
            return 1;
            break;

        default:
            break;
    }
    //return invalid value to indicate invalid character
    return 2;
}


int64_t ternary2int( trint32_t x )
{
    int64_t total=0;
    int i;
    for(i=BITS_PER_TRINT32_T-BITS_PER_TRIT; i>=0; i-=BITS_PER_TRIT)
    {
        total *= 3;
        total += trit2int( (x >> i) & TRIT_MASK );
    }
    return total;
}

trint32_t ternaryFromInt64( int64_t x )
{
    trint32_t result=0;
    int i;
    for(i=0; i<TRITS_PER_TRINT32_T; ++i)
    {
        int r = (int)( x % 3 );
        x /= 3;
        if( r == 2 )
        {
            r = -1;
            x += 1;
        }
        else if( r == -2 )
        {
            r = 1;
            x -= 1;
        }
        result |= (trint32_t)( ( r < 0 ) ? N : (trit_t)r ) << ( i * BITS_PER_TRIT );
    }
    return result;
}

int ternaryParse( const char* s, trint32_t* x )
{
    trint32_t dst=0;
    int count=0;
    int started=0;

    if( !s || !x )
    {
        return -1;
    }

    for( ; *s; ++s )
    {
        trit_t t = char2trit( *s );
        if( 2 != t )
        {
            if( 1 == started && count >= TRITS_PER_TRINT32_T )
            {
                break;
            }
            dst = (dst << BITS_PER_TRIT) | ( t & TRIT_MASK );
            ++count;
            started=1;
        }
        else if( started )
        {
            break;
        }
    }

    if( 0 == count )
    {
        return -1;
    }
    *x=dst;
    return count;
}

//trint32_t int2ternary( int64_t x, int lvl );
//{
//	trint32_t retVal=0;
//
//	return retVal;
//}

void ternaryPrint( trint32_t x, int_fast8_t width)
{
    int_fast8_t started;
    int_fast8_t i;
    if(width < 1)
    {
        i=BITS_PER_TRINT32_T-2;
        started=0;
    }
    else if(width < TRITS_PER_TRINT32_T)
    {
        i=(width*2)-2;
        started=1;
    }
    else
    {
        i=BITS_PER_TRINT32_T-2;
        started=1;
    }
    for(; i>=0; i-=2)
    {
        switch((x >> i) & TRIT_MASK)
        {
            case N:
                printf("-");
                started=1;
                break;

            case 0:
                if(started || !i) printf("0");
                break;

            case 1:
                printf("1");
                started=1;
                break;

            default:
                printf("X");
                break;
        }
    }
}

void ternaryScan( trint32_t* x )
{
    trint32_t dst=0;
    int_fast8_t state=2, count=TRITS_PER_TRINT32_T;
    char c;
    trit_t t;

    while( state && count )
    {
        if( scanf("%c",&c) != 1 )
        {
            break;
        }
        t=char2trit(c) & TRIT_MASK;
        if( 2 != t )
        {
            state=1;
            --count;
            dst = (dst<<BITS_PER_TRIT)|t;
        }
        else if(state==1)
        {
            state=0;
        }
    }
    *x=dst;
}

// BITWISE 1 OP
static trit_t apply_trit_gate1 ( uint_fast8_t gate,  trit_t op1 )
{
    uint_fast8_t shift = BITS_PER_TRIT * ((op1 + 1) & TRIT_MASK);
    return (gate >> shift) & TRIT_MASK;
}

trint32_t apply_tritwise_gate1 ( uint_fast8_t gate,  trint32_t op1 )
{
    trint32_t dst = 0;
    uint_fast8_t i;
    for(i=0; i<BITS_PER_TRINT32_T; i+=BITS_PER_TRIT)
    {
        dst |= ((trint32_t)apply_trit_gate1(gate, (op1 >> i) & TRIT_MASK))<<i;
    }
    return dst;
}

trint32_t ternaryINCB( trint32_t op1 )
{
    return apply_tritwise_gate1(TRIGATE_INC, op1);
}

trint32_t ternaryDECB( trint32_t op1 )
{
    return apply_tritwise_gate1(TRIGATE_DEC, op1);
}

trint32_t ternaryNEGB( trint32_t op1 )
{
    return apply_tritwise_gate1(TRIGATE_NOT, op1);
}

trint32_t ternaryFLTB( trint32_t op1 )
{
    return apply_tritwise_gate1(TRIGATE_FLT, op1);
}

trint32_t ternaryABSB( trint32_t op1 )
{
    return apply_tritwise_gate1(TRIGATE_ABS, op1);
}

// BITWISE 2 OPS
static trint32_t apply_tritwise_gate2 ( const trit_t gate[4][4],  trint32_t op1, trint32_t op2 )
{
    trint32_t dst = 0;
    uint_fast8_t i;
    for(i=0; i<BITS_PER_TRINT32_T; i+=BITS_PER_TRIT)
    {
        dst |= ((trint32_t)gate[(op1 >> i)&TRIT_MASK][(op2 >> i)&3])<<i;
    }
    return dst;
}


trint32_t ternaryADDB ( trint32_t op1, trint32_t op2 )
{
    return apply_tritwise_gate2(trigate_add, op1, op2);
}

trint32_t ternaryMULB ( trint32_t op1, trint32_t op2 )
{
    return apply_tritwise_gate2(trigate_mul, op1, op2);
}

trint32_t ternaryANDB ( trint32_t op1, trint32_t op2 )
{
    return apply_tritwise_gate2(trigate_and, op1, op2);
}

trint32_t ternaryORRB ( trint32_t op1, trint32_t op2 )
{
    return apply_tritwise_gate2(trigate_orr, op1, op2);
}

trint32_t ternaryMINB ( trint32_t op1, trint32_t op2 )
{
    return apply_tritwise_gate2(trigate_min, op1, op2);
}

trint32_t ternaryMAXB ( trint32_t op1, trint32_t op2 )
{
    return apply_tritwise_gate2(trigate_max, op1, op2);
}

trint32_t ternarySETB ( trint32_t op1, trint32_t op2 )
{
    return apply_tritwise_gate2(trigate_set, op1, op2);
}

trint32_t ternaryIMPB ( trint32_t op1, trint32_t op2 )
{
    return apply_tritwise_gate2(trigate_imp, op1, op2);
}

trint32_t ternaryUNMB ( trint32_t op1, trint32_t op2 )
{
    return apply_tritwise_gate2(trigate_unm, op1, op2);
}


// ARITHMETIC 1 OP
trint32_t ternarySGN  ( trint32_t op1 )
{
    trint32_t sign = 0;
    while( op1 )
    {
        if(op1 & 3)
        {
            sign = op1 & 3;
        }
        op1 >>= BITS_PER_TRIT;
    }
    return sign;
}

// ARITHMETIC 2 OPS

trint32_t ternarySHL  ( trint32_t op1, trint32_t op2 )
{
    static const int pow3[6] = {1, 3, 9, 27, 81, 243};
    int shift=0;
    int i;

    if(op2 >> (6 * BITS_PER_TRIT))
    {
        return 0;
    }

    for(i=0; i<6; i++)
    {
        shift += trit2int((op2 >> (i * BITS_PER_TRIT)) & TRIT_MASK) * pow3[i];
    }

    if(shift>=TRITS_PER_TRINT32_T || shift<=-TRITS_PER_TRINT32_T)
    {
        return 0;
    }

    shift *= BITS_PER_TRIT;
    if(shift<0)
    {
        return op1 >> (unsigned)(-shift);
    }
    else
    {
        return op1 << (unsigned)shift;
    }
}

void      ternaryROL  ( trint32_t* op1, trint32_t* op2, trint32_t rotate, int triwordsize )
{
    unsigned word_bits;
    unsigned total_bits;
    uint64_t word_mask;
    unsigned __int128 pair;
    unsigned __int128 mask;
    unsigned rotate_bits;
    int shift;

    if( triwordsize <= 0 )
    {
        return;
    }

    word_bits = (unsigned)triwordsize * BITS_PER_TRIT;
    total_bits = word_bits * 2;
    word_mask = (word_bits >= 64) ? ~(uint64_t)0 : ((1ULL << word_bits) - 1);
    mask = (total_bits >= 128) ? ~(unsigned __int128)0 : (((unsigned __int128)1 << total_bits) - 1);

    pair = (((unsigned __int128)((*op1) & word_mask)) << word_bits) |
           (unsigned __int128)((*op2) & word_mask);

    shift = (int)(ternary2int(rotate) % (triwordsize * 2));
    if( shift < 0 )
    {
        shift += triwordsize * 2;
    }
    rotate_bits = (unsigned)shift * BITS_PER_TRIT;

    if( rotate_bits )
    {
        pair = ((pair << rotate_bits) |
                (pair >> (total_bits - rotate_bits))) & mask;
    }

    *op1 = (trint32_t)((pair >> word_bits) & word_mask);
    *op2 = (trint32_t)(pair & word_mask);
}

//trint32_t ternaryRCL  ( trint32_t op1, trint32_t op2, trit_t* carry )
//{
//    int shift=(ternary2int(op2) % (TRITS_PER_TRINT32_T + 1))*2;
//    trint32_t wrap;
//        op1 = (TRINT32_T_MASK & op1) | ((trint32_t)(*carry&3) << BITS_PER_TRINT32_T);
//    if(shift<0)
//    {
//        shift = (-shift) % (BITS_PER_TRINT32_T + 2);
//        wrap= op1 & ((1 << shift) -1);
//        op1=(op1 >> shift) | (wrap << ((BITS_PER_TRINT32_T+2) - shift));
//    }
//    else
//    if(shift!=0)
//    {
//        shift = shift % (BITS_PER_TRINT32_T + 2);
//        wrap=((op1 >> ((BITS_PER_TRINT32_T + 2) - shift)) & ((1 << shift) -1));
//        op1=(op1 << shift) | wrap;
//    }
//    *carry = (op1 >> BITS_PER_TRINT32_T) & 3;
//    op1 &= TRINT32_T_MASK;
//    return op1;
//}

static const uint8_t ternary_add_sum[7] = {0, 1, 3, 0, 1, 3, 0};
static const int8_t ternary_add_carry[7] = {-1, -1, 0, 0, 0, 1, 1};

trint32_t ternaryADD  ( trint32_t op1, trint32_t op2 )
{
    trint32_t result=0;
    int carry=0;
    unsigned shift;

    for(shift=0; shift<BITS_PER_TRINT32_T; shift+=BITS_PER_TRIT)
    {
        unsigned a;
        unsigned b;
        int sum;

        /* Once the carry is clear and op2 has no remaining bits, the high
         * trits of op1 pass through unchanged (including undefined trits). */
        if( carry == 0 && ( op2 >> shift ) == 0 )
        {
            result |= op1 & ~( ( (trint32_t)1 << shift ) - 1 );
            break;
        }

        a=(op1 >> shift) & TRIT_MASK;
        b=(op2 >> shift) & TRIT_MASK;

        if(a==2 || b==2)
        {
            result |= (trint32_t)2 << shift;
            carry=0;
            continue;
        }

        sum=((a==N) ? -1 : (int)a) +
            ((b==N) ? -1 : (int)b) + carry;
        result |= (trint32_t)ternary_add_sum[sum+3] << shift;
        carry=ternary_add_carry[sum+3];
    }
    return result;
}

trint32_t ternaryADC  ( trint32_t op1, trint32_t op2, trit_t* carry )
{
    trint32_t result=0;
    int carryin=(*carry == N) ? -1 : (int)*carry;
    unsigned shift;

    if(carryin == 2)
    {
        carryin=0;
    }

    for(shift=0; shift<BITS_PER_TRINT32_T; shift+=BITS_PER_TRIT)
    {
        unsigned a;
        unsigned b;
        int sum;

        /* Same shortcut as ternaryADD: a clear carry and no remaining op2
         * bits leave the high trits of op1 untouched. */
        if( carryin == 0 && ( op2 >> shift ) == 0 )
        {
            result |= op1 & ~( ( (trint32_t)1 << shift ) - 1 );
            break;
        }

        a=(op1 >> shift) & TRIT_MASK;
        b=(op2 >> shift) & TRIT_MASK;

        if(a==2 || b==2)
        {
            result |= (trint32_t)2 << shift;
            carryin=0;
            continue;
        }

        sum=((a==N) ? -1 : (int)a) +
            ((b==N) ? -1 : (int)b) + carryin;
        result |= (trint32_t)ternary_add_sum[sum+3] << shift;
        carryin=ternary_add_carry[sum+3];
    }

    *carry = (carryin < 0) ? N : (trit_t)carryin;
    return result;
}
// 000 => 0
// --- => -
// +++ => +
// --+ => 0
// --0 => -
//

trint32_t ternaryMUL  ( trint32_t op1, trint32_t op2 )
{
    trint32_t product=0;
    while(op2)
    {
        switch(op2 & 3)
        {
            case 1:
                product = ternaryADD( product, op1 );
                break;

            case N:
                product = ternaryADD( product, ternaryNEGB(op1) );
                break;
        }
        op1 <<= BITS_PER_TRIT;
        op2 >>= BITS_PER_TRIT;
    }
    return product;
}

/*
 * High part of a multiplication: (op1 * op2) >> 27 trits.  The full product
 * needs more than 32 trits, so use a 128-bit intermediate value.
 */
trint32_t ternaryMULHI( trint32_t op1, trint32_t op2 )
{
    static const int64_t THREE_POW_27 = 7625597484987LL;
    __int128 product = (__int128)ternary2int( op1 ) * (__int128)ternary2int( op2 );
    __int128 high = product / THREE_POW_27;

    if( high > INT64_MAX || high < INT64_MIN )
    {
        return 0;
    }
    return ternaryFromInt64( (int64_t)high );
}

//trint32_t ternaryDIVMOD  ( trint32_t op1, trint32_t op2 )
//{
//}

// This main can be built with make tern-test
#if TERNARY_MATH_MAIN

int main( int argc, char** argv )
{
    //int64_t i;
    trint32_t A, B, C, D, E;
    trit_t carry=N;

    printf("A: ");
    ternaryScan( &A );
    printf("B: ");
    ternaryScan( &B );

    // Run through a bunch or rotate tests first.
    E = 0b11110000;
    while( E != 0b01010000 )
    {
        printf("\nROL( A, B, ");
        ternaryPrint(E, 5);
        printf(")=");
        C=A;
        D=B;
        ternaryROL(&C, &D, E, 15);
        ternaryPrint(C, 30);
        printf(" ");
        ternaryPrint(D, 30);
        E = ternaryADD( E, 1);
    }

    printf("\nA = %"PRIi64"  ", ternary2int(A) );  ternaryPrint( A, 0);
    printf("\nB = %"PRIi64"  ", ternary2int(B));  ternaryPrint( B, 0);

    printf("\n\nTritwise unary operations:\nINC(A)=");  ternaryPrint( ternaryINCB(A), 0 );
    printf("\nDEC(A)=");  ternaryPrint( ternaryDECB(A), 0 );
    printf("\nNEG(A)=");  ternaryPrint( ternaryNEGB(A), 0 );
    printf("\nFLT(A)=");  ternaryPrint( ternaryFLTB(A), 0 );
    printf("\nABS(A)=");  ternaryPrint( ternaryABSB(A), 0 );

    printf("\n\nTritwise 2 input operations:\nMUL(A, B)=");  ternaryPrint( ternaryMULB(A, B), 0 );
    printf("\nADD(A, B)=");  ternaryPrint( ternaryADDB(A, B), 0 );

    printf("\nORR(A, B)=");  ternaryPrint( ternaryORRB(A, B), 0 );
    printf("\nAND(A, B)=");  ternaryPrint( ternaryANDB(A, B), 0 );
    printf("\nSET(A, B)=");  ternaryPrint( ternarySETB(A, B), 0 );
    printf("\nIMP(A, B)=");  ternaryPrint( ternaryIMPB(A, B), 0 );
    printf("\nUNM(A, B)=");  ternaryPrint( ternaryUNMB(A, B), 0 );

    printf("\n\nSHL(A, +--)=");  ternaryPrint( ternarySHL(A, 0b011111), 0 );
    printf("\nSHL(A, 0--)=");  ternaryPrint( ternarySHL(A, 0b001111), 0 );
    printf("\nSHL(DEC(A), -++-)=");  ternaryPrint( ternarySHL(ternaryDECB(A), 0b0011010111), 0 );
//    printf("\nRCL(A, +--, -)=");  ternaryPrint( ternaryRCL(A, 0b011111, &carry), 0 ); carry=N;
//    printf("\nRCL(A, 0--, -)=");  ternaryPrint( ternaryRCL(A, 0b001111, &carry), 0 ); carry=N;

    printf("\n\nArithmetic operations:\nADD(A, B)=%"PRIi64"  ", ternary2int(ternaryADD(A, B)));  ternaryPrint( ternaryADD(A, B), 0 );
    printf("\nSUB(A, B)=%"PRIi64"  ", ternary2int(ternaryADD(A, ternaryNEGB(B))));  ternaryPrint( ternaryADD(A, ternaryNEGB(B)), 0 );
    carry=N; C=ternaryADC(A, B, &carry);
    printf("\nADC(A, B, -)=%"PRIi64" c-out=%d ", ternary2int(C), trit2int(carry)); ternaryPrint( C, 0 );
    carry=0; C=ternaryADC(A, B, &carry);
    printf("\nADC(A, B, 0)=%"PRIi64" c-out=%d ", ternary2int(C), trit2int(carry)); ternaryPrint( C, 0 );
    carry=1; C=ternaryADC(A, B, &carry);
    printf("\nADC(A, B, 1)=%"PRIi64" c-out=%d ", ternary2int(C), trit2int(carry)); ternaryPrint( C, 0 );
    printf("\nMUL(A, B)=%"PRIi64"  ", ternary2int(ternaryMUL(A, B)));  ternaryPrint( ternaryMUL(A, B), 0 );

    printf("\ndone.\n");
    return 0;
}
#endif //TERNARY_MATH_MAIN


/*****************************************************************************/

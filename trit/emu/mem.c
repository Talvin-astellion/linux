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
 * @file    mem.c
 * @author  Ted Kotz <ted@kotz.us>
 * @version 0.1
 *
 * [Description]
 *
 */


/* Includes ******************************************************************/
#include "mem.h"
#include "tml.h"

#include <stdio.h>
#include <errno.h>
#include <ctype.h>
#include <string.h>
#include <poll.h>
#include <unistd.h>

/* Defines *******************************************************************/
#define MAX_STRING_SIZE 256

#define strcpy_s( A, B, C ) strcpy( A, C)
/* Types *********************************************************************/
/* Interfaces ****************************************************************/
/* Data **********************************************************************/
Tryte ram[RAM_SIZE];
static int uart_eof = 0;

static int uartReady( void )
{
    struct pollfd descriptor;
    int rc;
    if( uart_eof )
    {
        return 0;
    }
    descriptor.fd = STDIN_FILENO;
    descriptor.events = POLLIN;
    descriptor.revents = 0;
    do
    {
        rc = poll( &descriptor, 1, 0 );
    } while( rc < 0 && errno == EINTR );
    return rc > 0 && ( descriptor.revents & ( POLLIN | POLLHUP | POLLERR ) ) != 0;
}

/* Functions *****************************************************************/

/**
 * Convert an ASCII byte to the five-trit character encoding used by the
 * emulator: (ascii - 0x1F), with NUL kept at zero.
 */
static int ascii2tryte( int c )
{
    if( c == 0 )
    {
        return 0;
    }
    return (Tryte)ternaryFromInt64( c - 0x1F );
}

/**
 * Inverse of ascii2tryte().  Returns '?' for values outside the encodable
 * range.
 */
static int tryte2ascii( Tryte val )
{
    int x = (int)ternary2int( (trint32_t)val );
    int c;
    if( x == 0 )
    {
        return 0;
    }
    c = x + 0x1F;
    if( c < 0 || c > 0xFF )
    {
        return '?';
    }
    return c;
}

/**
 * Map an address to an index in the RAM array.  Only the low eleven trits are
 * used as an offset, mirroring the address decoder.
 */
int ramIndex( TriWord addr, int64_t* index )
{
    /* Three-trit chunks, including the legacy numeric value +2 for code 10. */
    static const int8_t chunk[64] = {
          0,   1,   2,  -1,   3,   4,   5,   2,   6,   7,   8,   5,  -3,  -2,  -1,  -4,
          9,  10,  11,   8,  12,  13,  14,  11,  15,  16,  17,  14,   6,   7,   8,   5,
         18,  19,  20,  17,  21,  22,  23,  20,  24,  25,  26,  23,  15,  16,  17,  14,
         -9,  -8,  -7, -10,  -6,  -5,  -4,  -7,  -3,  -2,  -1,  -4, -12, -11, -10, -13
    };
    int64_t offset = chunk[addr & 0x3F] +
                     27 * chunk[( addr >> 6 ) & 0x3F] +
                     729 * chunk[( addr >> 12 ) & 0x3F] +
                     19683 * chunk[( addr >> 18 ) & 0x0F];
    int64_t idx = BASE_OFFSET + offset;
    if( idx < 0 || idx >= RAM_SIZE )
    {
        return -1;
    }
    *index = idx;
    return 0;
}

/*
 * See Header
 *
 */
Tryte ReadAddr(TriWord addr)
{
    int64_t index;
    Tryte tmlValue;
    if( tmlReadAddr( addr, &tmlValue ) )
    {
        return tmlValue;
    }
    switch(addr)
    {
        case ADDR_UART:
            if( !uartReady() )
            {
                return 0;
            }
            {
                int c = fgetc( stdin );
                if( c == EOF )
                {
                    uart_eof = 1;
                    return N;
                }
                return (Tryte)ascii2tryte( c );
            }
        case ADDR_UART_STATUS:
            if( uart_eof )
            {
                return N;
            }
            return uartReady() ? 1 : 0;
        case ADDR_RTC2:
        case ADDR_RTC1:
        case ADDR_RTC0:
            // return int2TriWord(time(null));
            return 0;
        default:
            if( ramIndex( addr, &index ) != 0 )
            {
                return 0;
            }
            return ram[index];
    }
}

Tryte ReadRamTryte(TriWord addr)
{
    int64_t index;
    if( ramIndex( addr, &index ) != 0 )
    {
        return 0;
    }
    return ram[index];
}

/*
 * See Header
 *
 */
void WriteAddr(TriWord addr, Tryte val)
{
    int64_t index;
    if( tmlWriteAddr( addr, val ) )
    {
        return;
    }
    switch(addr)
    {
        case ADDR_UART:
            if( !tmlAllowActuation( tmlActive() ) )
            {
                break;
            }
            putchar( tryte2ascii( val ) );
            break;
        case ADDR_RTC2:
        case ADDR_RTC1:
        case ADDR_RTC0:
            break;
        default:
            if( ramIndex( addr, &index ) == 0 )
            {
                ram[index]=val;
            }
    }

}


/*
 * See Header
 *
 */
void resetMem( void )
{
    memset( ram, 0, sizeof( ram ) );
}


/*
 * See Header
 *
 */
int readFileIntoMem(FILE* fin, const char* fname, int verbose)
{
    TriWord addr=0;
    TriWord accum=0;
    trit_t t;
    int line=1;
    int pos=0;
    int number;
    char filename[MAX_STRING_SIZE] = "-";
    char tmp[MAX_STRING_SIZE];
    int c;
    int i;
    strcpy_s(filename, MAX_STRING_SIZE, fname);
    for(;;)
    {
        ++pos;
        c=fgetc(fin);
        switch(c)
        {
            case EOF:
                return 0;

            case ';':
                WriteAddr(addr, accum & TRYTE_MASK);
                if (verbose > 0)
                {
                    ternaryPrint(addr, TRITS_PER_WORD);
                    printf(" : ");
                    ternaryPrint(accum, TRITS_PER_TRYTE);
                    printf(";\n");
                }
                accum=0;
                addr= ternaryADD(addr, 1);
                break;

            case ':':
                addr=accum;
                accum=0;
                break;

            case '#':
                // comments, maybe pragmas and line updates
                // can we find a line number directive
                // Dump white space
                c=fgetc(fin);
                while( c==' ' || c =='\t' )
                {
                    c=fgetc(fin);
                }
                if( c=='l' )
                {
                    c=fgetc(fin);
                    if( c=='i' )
                    {
                        c=fgetc(fin);
                        if( c=='n' )
                        {
                            c=fgetc(fin);
                            if( c=='e' )
                            {
                                c=fgetc(fin);
                                while( c==' ' || c =='\t' )
                                {
                                    c=fgetc(fin);
                                }
                            }
                        }
                    }
                }
                number=0;
                // Check for number
                while( c>='0' && c <= '9' )
                {
                    number = number * 10 + c - '0';
                    c=fgetc(fin);
                }
                if(number != 0 )
                {
                    // throw out white space
                    while( c==' ' || c =='\t' )
                    {
                        c=fgetc(fin);
                    }
                    if(c=='\n')
                    {
                        // a number by itself is a line override
                        line = number-1;
                    }
                    else if(c=='"')
                    {
                        //extract filename
                        i=0;
                        c=fgetc(fin);
                        while( (i < (MAX_STRING_SIZE - 1)) &&
                            (c != '\"') && (c != '\n') && (c != EOF))
                        {
                            tmp[i++]=c;
                            c=fgetc(fin);
                        }
                        if(c=='\"')
                        {
                            tmp[i++]=0;
                            line=number-1;
                            strcpy_s(filename, MAX_STRING_SIZE, tmp);
                        }
                    }
                }
                __attribute__((fallthrough));
            case '/':
                // C++ style comments
                while( c!=EOF && c !='\n' )
                {
                    c=fgetc(fin);
                }
                __attribute__((fallthrough));
            case '\n':
                if( accum != 0 )
                {
                    printf("%s:%d:%d: ERROR: End of line with non-zero accumulator.\n", filename, line, pos);
                    return -1;
                }
                else
                {
                    ++line;
                    pos=0; //Assume carriage return on newline
                }
                break;

            case '\r':
                pos=0;
                break;

            default:
                if (isspace(c))
                {
                    // ignore other whitespace
                }
                else
                {
                    t=char2trit (c);
                    if( 2 != t )
                    {
                        accum = (accum<<BITS_PER_TRIT)|t;
                    }
                    else
                    {
                        printf("%s:%d:%d: ERROR: Unexpected '%c' character.\n", filename, line, pos, c);
                        return -2;
                    }
                }
                break;
        }
    }
}
/*****************************************************************************/


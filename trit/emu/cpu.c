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
 * @file    cpu.c
 * @author  Ted Kotz <ted@kotz.us>
 * @version 0.1
 *
 * [Description]
 *
 */


/* Includes ******************************************************************/
#include "cpu.h"
#include "disassem.h"
#include "mem.h"
#include "symbolic.h"
#include "taint.h"
#include "trace.h"

#include <stdio.h>
/* Defines *******************************************************************/
/* Types *********************************************************************/
/* Interfaces ****************************************************************/
/* Data **********************************************************************/

static const char* reg_names[] =
{
    "GP_13",
    "GP_12",
    "GP_11",
    "GP_10",
    "GP_09",
    "GP_08",
    "GP_07",
    "GP_06",
    "GP_05",
    "RSRVD",
    "CLOCK",
    "FLAGS",
    "PC   ",
    "ZERO ",
    "GP01 ",
    "GP02 ",
    "GP03 ",
    "GP04 ",
    "GP05 ",
    "GP06 ",
    "GP07 ",
    "GP08 ",
    "GP09 ",
    "GP10 ",
    "GP11 ",
    "GP12 ",
    "GP13 ",
};

/* Functions *****************************************************************/

/**
 * A register field is three trits encoded in six bits.  Binary 10 is the
 * undefined trit value and must never be used as a register index.
 */
static int validRegCode( Tryte reg )
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

/**
 * Convert a balanced-ternary register number (-13..13) into its three-trit
 * register code.
 */
static Tryte regCodeFromInt( int v )
{
    Tryte code = 0;
    int i;
    for( i = 0; i < 3; ++i )
    {
        int r = v % 3;
        int q = v / 3;
        if( r == 2 )
        {
            r = -1;
            q += 1;
        }
        else if( r == -2 )
        {
            r = 1;
            q -= 1;
        }
        code |= (Tryte)( ( r < 0 ) ? N : (trit_t)r ) << ( i * BITS_PER_TRIT );
        v = q;
    }
    return code;
}

static TriWord signExtendTriWord( TriWord val )
{
    val &= TRIWORD_MASK;
    if( ( ( val >> ( BITS_PER_TRIWORD - BITS_PER_TRIT ) ) & TRIT_MASK ) == N )
    {
        val |= ~TRIWORD_MASK;
    }
    return val;
}

int64_t triwordToInt( TriWord val )
{
    return ternary2int( signExtendTriWord( val ) );
}

static trit_t addOverflow( TriWord full )
{
    return signExtendTriWord( full & TRIWORD_MASK ) == full ? 0 : 1;
}

static trit_t tritParity( TriWord val )
{
    /* Collapse each 2-bit trit to its low bit, which is set exactly for a
     * nonzero trit, then take the parity of the 27 resulting bits. */
    uint64_t nonzero = ( val | ( val >> 1 ) ) &
                       UINT64_C(0x5555555555555555) & TRIWORD_MASK;
    if( nonzero == 0 )
    {
        return 0;
    }
    return __builtin_parityll( nonzero ) ? N : 1;
}

static void setResultFlags( TriCpu* cpu, TriWord val, trit_t carry,
                            trit_t overflow )
{
    FlagReg flags;
    flags.val = cpu->regs[REG_FLAGS] & TRIWORD_MASK;
    flags.flags.S = ternarySGN( signExtendTriWord( val ) );
    flags.flags.C = carry;
    flags.flags.V = overflow;
    flags.flags.P = tritParity( val );
    cpu->regs[REG_FLAGS] = flags.val & TRIWORD_MASK;
}

/**
 * [Description]
 *
 * @param
 * @return
 */
TriWord ReadTriWord( TriCpu* cpu, TriWord addr )
{
    (void)cpu;
    TriWord tmp = ReadAddr(addr);
    tmp |= (TriWord)ReadAddr(ternaryADD(addr, 0b0001)) << (BITS_PER_TRYTE);
    tmp |= (TriWord)ReadAddr(ternaryADD(addr, 0b0111)) << (2 * BITS_PER_TRYTE);
    return tmp & TRIWORD_MASK;
}


/**
 * [Description]
 *
 * @param
 * @return
 */
void WriteTriWord( TriCpu* cpu, TriWord addr , TriWord val )
{
    (void)cpu;
    val &= TRIWORD_MASK;
    WriteAddr(addr , val & TRYTE_MASK);
    WriteAddr(ternaryADD(addr, 0b0001), (val >>     (BITS_PER_TRYTE)) & TRYTE_MASK );
    WriteAddr(ternaryADD(addr, 0b0111), (val >> (2 * BITS_PER_TRYTE)) & TRYTE_MASK );
}


/**
 * [Description]
 *
 * @param
 * @return
 */
Tryte ReadTryte( TriCpu* cpu, TriWord addr )
{
    (void)cpu;
    return ReadAddr(addr);
}


/**
 * [Description]
 *
 * @param
 * @return
 */
void WriteTryte( TriCpu* cpu, TriWord addr , Tryte val )
{
    (void)cpu;
    WriteAddr(addr , val );
}


/**
 * [Description]
 *
 * @param
 * @return
 */
static int hasUndefinedTrit( TriWord val )
{
    /* A 2-bit trit field is undefined exactly when its high bit is set and
     * its low bit is clear (the encoding 10). */
    return ( ( ( val >> 1 ) & ~val & UINT64_C(0x5555555555555555) ) != 0 );
}

/**
 * [Description]
 *
 * @param
 * @return
 */
TriWord ReadModReg( TriCpu* cpu, Tryte reg, Tryte mod )
{
    TriWord val=0;
    if( 0 != reg && validRegCode( reg ) )
    {
        val = cpu->regs[reg];
    }

    // choose shift
    switch ( ( mod >> (3 * BITS_PER_TRIT) ) & TRIT_MASK )
    {
        case N:
            val = val >> (9 * BITS_PER_TRIT);
            break;

        case 0:
            // Do nothing
            break;

        case 1:
            val = val << (9 * BITS_PER_TRIT);
            break;

        default:
            // Undefined shift modifier: leave the value unchanged.
            break;
    }

    /* OPMOD_NOP is the identity gate for defined trits.  Skip the 32-trit
     * walk when the value has no undefined trits; those map to zero. */
    if( ( mod & ( ( 1 << (3 * BITS_PER_TRIT) ) - 1 ) ) ==
        ( OPMOD_NOP & ( ( 1 << (3 * BITS_PER_TRIT) ) - 1 ) ) &&
        !hasUndefinedTrit( val ) )
    {
        return signExtendTriWord( val );
    }

    return signExtendTriWord(
        apply_tritwise_gate1( mod & ((1<<(3 * BITS_PER_TRIT)) - 1), val ) );
}

/**
 * [Description]
 *
 * @param
 * @return
 */
void SetSignTrit( TriCpu* cpu, TriWord val )
{
    setResultFlags( cpu, val, 0, 0 );
}


/**
 * [Description]
 *
 * @param
 * @return
 */
void WriteReg( TriCpu* cpu, Tryte reg, TriWord val )
{
    if( 0 != reg && validRegCode( reg ) )
    {
        cpu->regs[reg] = val & TRIWORD_MASK;
    }
}

/*
 * See Header
 *
 */
void printCpuState(TriCpu* cpu)
{
    TriWord inst;
    int x;
    printf("             876543210876543210876543210\n");
    for( x = -13; x <= 13; ++x )
    {
        Tryte reg = regCodeFromInt( x );
        printf(" %s %03d : ", reg_names[x+13], x);
        ternaryPrint(cpu->regs[reg], TRITS_PER_WORD);
        printf("(%"PRIi64")\n", triwordToInt(cpu->regs[reg]));
    }
    printf("             876543210876543210876543210\n");
    printf("            |    Immed 18     |R1|OpCode|\n");
    printf("            | Immed 11 |R2+Mod|R1|OpCode|\n");
    printf("            |Im4|R3+Mod|R2+Mod|R1|OpCode|\n Next INST > ");
    inst = ReadRamTryte(cpu->regs[REG_PC]);
    inst |= (TriWord)ReadRamTryte(ternaryADD(cpu->regs[REG_PC], 0b0001))
            << BITS_PER_TRYTE;
    inst |= (TriWord)ReadRamTryte(ternaryADD(cpu->regs[REG_PC], 0b0111))
            << (2 * BITS_PER_TRYTE);
    ternaryPrint(inst, TRITS_PER_WORD);
    printf("  ");
    printInstruction(inst);
    printf("\n");
    if( cpu->halted )
    {
        printf(" CPU halted (trap %d; press c to reset)\n", cpu->trap);
    }
}


/*
 * See Header
 *
 */
void resetCPU(TriCpu* cpu)
{
    int i;
    for( i=0; i<NUM_TRIREGS; ++i)
    {
        cpu->regs[i]=0;
    }
    cpu->halted = 0;
    cpu->trap = 0;
    cpu->regs[REG_PC]=ReadTriWord(cpu, ADDR_PC_START0) & TRIWORD_MASK;
}

static int validModCode( Tryte mod )
{
    return ( ( mod >> ( 3 * BITS_PER_TRIT ) ) & TRIT_MASK ) != 2;
}

static int knownOpcode( TriOpcode opcode )
{
    switch( opcode )
    {
        case OPCODE_LD1      :
        case OPCODE_LD2      :
        case OPCODE_LD3      :
        case OPCODE_ST1      :
        case OPCODE_ST2      :
        case OPCODE_ST3      :
        case OPCODE_POP      :
        case OPCODE_PSH      :
        case OPCODE_HALT     :
        case OPCODE_ADC      :
        case OPCODE_ADD      :
        case OPCODE_ADS      :
        case OPCODE_ADCI     :
        case OPCODE_ADDI     :
        case OPCODE_ADSI     :
        case OPCODE_MUL      :
        case OPCODE_MULU     :
        case OPCODE_MULI     :
        case OPCODE_RTL      :
        case OPCODE_TAND     :
        case OPCODE_TOR      :
        case OPCODE_TMAJ     :
        case OPCODE_TADD     :
        case OPCODE_TMUL     :
        case OPCODE_TMIN     :
        case OPCODE_TMAX     :
        case OPCODE_RTLI     :
        case OPCODE_TANDI    :
        case OPCODE_TORI     :
        case OPCODE_TMAJI    :
        case OPCODE_TADDI    :
        case OPCODE_TMULI    :
        case OPCODE_TMINI    :
        case OPCODE_TMAXI    :
        case OPCODE_NOP      :
            return 1;
        default:
            return 0;
    }
}

static int validInstruction( const TriOpInstruction* inst )
{
    switch( inst->opcode )
    {
        case OPCODE_NOP:
        case OPCODE_HALT:
            return 1;
        case OPCODE_ADSI:
            return validRegCode( inst->r1 );
        case OPCODE_LD1      :
        case OPCODE_LD2      :
        case OPCODE_LD3      :
        case OPCODE_ST1      :
        case OPCODE_ST2      :
        case OPCODE_ST3      :
        case OPCODE_POP      :
        case OPCODE_PSH      :
        case OPCODE_ADCI     :
        case OPCODE_ADDI     :
        case OPCODE_MULI     :
        case OPCODE_RTLI     :
        case OPCODE_TANDI    :
        case OPCODE_TORI     :
        case OPCODE_TMAJI    :
        case OPCODE_TADDI    :
        case OPCODE_TMULI    :
        case OPCODE_TMINI    :
        case OPCODE_TMAXI    :
            return validRegCode( inst->r1 ) &&
                   validRegCode( inst->r2 ) && validModCode( inst->r2mod );
        case OPCODE_ADC      :
        case OPCODE_ADD      :
        case OPCODE_ADS      :
        case OPCODE_MUL      :
        case OPCODE_MULU     :
        case OPCODE_RTL      :
        case OPCODE_TAND     :
        case OPCODE_TOR      :
        case OPCODE_TMAJ     :
        case OPCODE_TADD     :
        case OPCODE_TMUL     :
        case OPCODE_TMIN     :
        case OPCODE_TMAX     :
            return validRegCode( inst->r1 ) &&
                   validRegCode( inst->r2 ) && validModCode( inst->r2mod ) &&
                   validRegCode( inst->r3 ) && validModCode( inst->r3mod );
        default:
            return 0;
    }
}

/*
 * See Header
 *
 */
void runCPU( TriCpu* cpu, int cycles )
{
    TriWord addr;
    TriWord val;
    TriWord val2;
    TriOpInstruction inst;
    trit_t carry;
    trit_t overflow;
    FlagReg flags;

    if( cpu->halted )
    {
        return;
    }

    while( cycles-- > 0 && !cpu->halted )
    {
        TriWord rawInst = ReadTriWord(cpu, cpu->regs[REG_PC]);
        parseInstruction( &inst, rawInst );
        traceInstruction( cpu, rawInst );
        taintStepBefore( cpu, rawInst );
        flags.val = cpu->regs[REG_FLAGS] & TRIWORD_MASK;
        cpu->regs[REG_CLOCK] =
            ternaryADD( cpu->regs[REG_CLOCK], 0b0001 ) & TRIWORD_MASK;
        cpu->regs[REG_PC] = ternaryADD(cpu->regs[REG_PC], 0b0100)
                            & TRIWORD_MASK;

        if( !validInstruction( &inst ) )
        {
            cpu->trap = knownOpcode( inst.opcode )
                        ? TRAP_INVALID_REGISTER : TRAP_INVALID_OPCODE;
            cpu->halted = 1;
            printf("\nCPU trap %d at PC ", cpu->trap);
            ternaryPrint( ternaryADD(cpu->regs[REG_PC],
                                     ternaryFromInt64( -4 )), 0 );
            printf("\n");
            break;
        }

        switch( inst.opcode )
        {
            case OPCODE_NOP       :  // NOP
                break;
            case OPCODE_HALT      :
                cpu->halted = 1;
                break;
            case OPCODE_LD1       :  // Load Rd <- (Rs+immediate) 9 trits
                val = ReadTryte(cpu, ternaryADD(ReadModReg(cpu, inst.r2, inst.r2mod), inst.imm11));
                setResultFlags(cpu, val, 0, 0);
                WriteReg(cpu,inst.r1,val);
                break;
            case OPCODE_LD2       :  // Load Rd <- (Rs+immediate) 18 trits
                addr = ternaryADD(ReadModReg(cpu, inst.r2, inst.r2mod), inst.imm11);
                val=ReadTryte(cpu, addr);
                val|=((TriWord)ReadTryte(cpu, ternaryADD(addr, 0b0001))<<BITS_PER_TRYTE) ;
                setResultFlags(cpu, val, 0, 0);
                WriteReg(cpu,inst.r1,val);
                break;
            case OPCODE_LD3       :  // Load Rd <- (Rs+immediate) 27 trits
                val=ReadTriWord(cpu, ternaryADD(ReadModReg(cpu, inst.r2, inst.r2mod), inst.imm11));
                setResultFlags(cpu, val, 0, 0);
                WriteReg(cpu,inst.r1,val);
                break;
            case OPCODE_ST1       :  // Store (Rs1+immediate) <- Rs2 9 trits
                addr = ternaryADD(ReadModReg(cpu, inst.r1, OPMOD_NOP), inst.imm11);
                val = ReadModReg(cpu, inst.r2, inst.r2mod) & TRYTE_MASK ;
                setResultFlags(cpu, val, 0, 0);
                WriteTryte(cpu,addr,val);
                break;
            case OPCODE_ST2       :  // Store (Rs1+immediate) <- Rs2 18 trits
                addr = ternaryADD(ReadModReg(cpu, inst.r1, OPMOD_NOP), inst.imm11);
                val = ReadModReg(cpu, inst.r2, inst.r2mod) & ((1ULL << (2*BITS_PER_TRYTE))-1);
                setResultFlags(cpu, val, 0, 0);
                WriteTryte(cpu,addr,val & TRYTE_MASK);
                WriteTryte(cpu,ternaryADD(addr, 0b0001),(val >> BITS_PER_TRYTE)& TRYTE_MASK);
                break;
            case OPCODE_ST3       :  // Store (Rs1+immediate) <- Rs2 27 trits
                addr = ternaryADD(ReadModReg(cpu, inst.r1, OPMOD_NOP), inst.imm11);
                val = ReadModReg(cpu, inst.r2, inst.r2mod);
                setResultFlags(cpu, val, 0, 0);
                WriteTriWord(cpu,addr,val);
                break;
            case OPCODE_POP       :  // Load Rd <- (Rs) ; Rs <- Rs + immediate
                addr = ReadModReg(cpu, inst.r2, inst.r2mod);
                val=ReadTriWord(cpu, addr);
                setResultFlags(cpu, val, 0, 0);
                WriteReg(cpu,inst.r1,val);
                WriteReg(cpu,inst.r2,ternaryADD(addr, inst.imm11));
                break;
            case OPCODE_PSH       :  // Store Rd += immediate; (Rd)  <- Rs
                addr = ternaryADD(ReadModReg(cpu, inst.r1, OPMOD_NOP), inst.imm11);
                val = ReadModReg(cpu, inst.r2, inst.r2mod);
                setResultFlags(cpu, val, 0, 0);
                WriteTriWord(cpu,addr,val);
                WriteReg(cpu,inst.r1,addr);
                break;
//          case OPCODE_          :  //
            case OPCODE_ADC       :  // C:Rd = Rs1 + Rs2 + C
                carry=flags.flags.C;
                val=ternaryADC( ReadModReg(cpu, inst.r2, inst.r2mod), ReadModReg(cpu, inst.r3, inst.r3mod), &carry);
                overflow = addOverflow( val );
                setResultFlags(cpu, val, overflow, overflow);
                WriteReg(cpu,inst.r1,val);
                break;
            case OPCODE_ADD       :  // C:Rd = Rs1 + Rs2
                carry=0;
                val=ternaryADC( ReadModReg(cpu, inst.r2, inst.r2mod), ReadModReg(cpu, inst.r3, inst.r3mod), &carry);
                overflow = addOverflow( val );
                setResultFlags(cpu, val, overflow, overflow);
                WriteReg(cpu,inst.r1,val);
                break;
            case OPCODE_ADS       :  // if S==1 then C:Rd = Rd + Rs1; if S==- then C:Rd = Rd + Rs2
                switch(flags.flags.S)
                {
                    case 1:
                        carry=0;
                        val=ternaryADC( ReadModReg(cpu, inst.r1, OPMOD_NOP), ReadModReg(cpu, inst.r2, inst.r2mod), &carry);
                        overflow = addOverflow( val );
                        setResultFlags(cpu, val, overflow, overflow);
                        WriteReg(cpu,inst.r1,val);
                        break;
                    case N:
                        carry=0;
                        val=ternaryADC( ReadModReg(cpu, inst.r1, OPMOD_NOP), ReadModReg(cpu, inst.r3, inst.r3mod), &carry);
                        overflow = addOverflow( val );
                        setResultFlags(cpu, val, overflow, overflow);
                        WriteReg(cpu,inst.r1,val);
                        break;
                }
                break;
            case OPCODE_ADCI      :  // C:Rd = Rs1 + immediate + C
                carry=flags.flags.C;
                val=ternaryADC( ReadModReg(cpu, inst.r2, inst.r2mod), inst.imm11, &carry);
                overflow = addOverflow( val );
                setResultFlags(cpu, val, overflow, overflow);
                WriteReg(cpu,inst.r1,val);
                break;
            case OPCODE_ADDI      :  // C:Rd = Rs1 + immediate
                carry=0;
                val=ternaryADC( ReadModReg(cpu, inst.r2, inst.r2mod), inst.imm11, &carry);
                overflow = addOverflow( val );
                setResultFlags(cpu, val, overflow, overflow);
                WriteReg(cpu,inst.r1,val);
                break;
            case OPCODE_ADSI       :  // if S==1 then C:Rd = Rd + imm1; if S==- then C:Rd = Rd + imm2
                switch(flags.flags.S)
                {
                    case 1:
                        carry=0;
                        val=ternaryADC( ReadModReg(cpu, inst.r1, OPMOD_NOP), inst.imm9_1, &carry);
                        overflow = addOverflow( val );
                        setResultFlags(cpu, val, overflow, overflow);
                        WriteReg(cpu,inst.r1,val);
                        break;
                    case N:
                        carry=0;
                        val=ternaryADC( ReadModReg(cpu, inst.r1, OPMOD_NOP), inst.imm9_2, &carry);
                        overflow = addOverflow( val );
                        setResultFlags(cpu, val, overflow, overflow);
                        WriteReg(cpu,inst.r1,val);
                        break;
                }
                break;
            case OPCODE_MUL       :  // Rd = Rs1 * Rs2
                val=ternaryMUL( ReadModReg(cpu, inst.r2, inst.r2mod), ReadModReg(cpu, inst.r3, inst.r3mod));
                setResultFlags(cpu, val, 0, 0);
                WriteReg(cpu,inst.r1,val);
                break;
            case OPCODE_MULU      :  // Rd = (Rs1 * Rs2) >> 27
                val=ternaryMULHI( ReadModReg(cpu, inst.r2, inst.r2mod), ReadModReg(cpu, inst.r3, inst.r3mod));
                setResultFlags(cpu, val, 0, 0);
                WriteReg(cpu,inst.r1,val);
                break;
            case OPCODE_MULI      :  // Rd = Rs1 * immediate
                val=ternaryMUL( ReadModReg(cpu, inst.r2, inst.r2mod), inst.imm11);
                setResultFlags(cpu, val, 0, 0);
                WriteReg(cpu,inst.r1,val);
                break;

            case OPCODE_RTL       :  // R1:R2 = R1:R2 Rotate Left by R3
                val = ReadModReg(cpu, inst.r1, OPMOD_NOP);
                val2 = ReadModReg(cpu, inst.r2, inst.r2mod);
                ternaryROL( &val, &val2, ReadModReg(cpu, inst.r3, inst.r3mod), TRITS_PER_WORD );
                WriteReg(cpu,inst.r1,val);
                WriteReg(cpu,inst.r2,val2);
                break;
//          case OPCODE_          :  //
            case OPCODE_TAND      :  // Rd = Tritwise Rs1 & Rs2
                val=ternaryANDB( ReadModReg(cpu, inst.r2, inst.r2mod), ReadModReg(cpu, inst.r3, inst.r3mod));
                setResultFlags(cpu, val, 0, 0);
                WriteReg(cpu,inst.r1,val);
                break;
            case OPCODE_TOR       :  // Rd = Tritwise Rs1 | Rs2
                val=ternaryORRB( ReadModReg(cpu, inst.r2, inst.r2mod), ReadModReg(cpu, inst.r3, inst.r3mod));
                setResultFlags(cpu, val, 0, 0);
                WriteReg(cpu,inst.r1,val);
                break;
            case OPCODE_TMAJ      :  // Rd = Tritwise Majority
                val=ternaryUNMB( ReadModReg(cpu, inst.r2, inst.r2mod), ReadModReg(cpu, inst.r3, inst.r3mod));
                setResultFlags(cpu, val, 0, 0);
                WriteReg(cpu,inst.r1,val);
                break;
            case OPCODE_TADD      :  // Rd = Tritwise Rs1 + Rs2
                val=ternaryADDB( ReadModReg(cpu, inst.r2, inst.r2mod), ReadModReg(cpu, inst.r3, inst.r3mod));
                setResultFlags(cpu, val, 0, 0);
                WriteReg(cpu,inst.r1,val);
                break;
            case OPCODE_TMUL      :  // Rd = Tritwise Rs1 * Rs2
                val=ternaryMULB( ReadModReg(cpu, inst.r2, inst.r2mod), ReadModReg(cpu, inst.r3, inst.r3mod));
                setResultFlags(cpu, val, 0, 0);
                WriteReg(cpu,inst.r1,val);
                break;
            case OPCODE_TMIN      :  // Rd = Tritwise MIN(Rs1, Rs2)
                val=ternaryMINB( ReadModReg(cpu, inst.r2, inst.r2mod), ReadModReg(cpu, inst.r3, inst.r3mod));
                setResultFlags(cpu, val, 0, 0);
                WriteReg(cpu,inst.r1,val);
                break;
            case OPCODE_TMAX      :  // Rd = Tritwise MAX(Rs1, Rs2)
                val=ternaryMAXB( ReadModReg(cpu, inst.r2, inst.r2mod), ReadModReg(cpu, inst.r3, inst.r3mod));
                setResultFlags(cpu, val, 0, 0);
                WriteReg(cpu,inst.r1,val);
                break;

            case OPCODE_RTLI      :  // R1:R2 = R1:R2 Rotate Left by immed
                val = ReadModReg(cpu, inst.r1, OPMOD_NOP);
                val2 = ReadModReg(cpu, inst.r2, inst.r2mod);
                ternaryROL( &val, &val2, inst.imm11, TRITS_PER_WORD );
                WriteReg(cpu,inst.r1,val);
                WriteReg(cpu,inst.r2,val2);
                break;
//          case OPCODE_          :  //
            case OPCODE_TANDI     :  // Rd = Tritwise Rs1 & immediate
                val=ternaryANDB( ReadModReg(cpu, inst.r2, inst.r2mod), inst.imm11 );
                setResultFlags(cpu, val, 0, 0);
                WriteReg(cpu,inst.r1,val);
                break;
            case OPCODE_TORI      :  // Rd = Tritwise Rs1 | immediate
                val=ternaryORRB( ReadModReg(cpu, inst.r2, inst.r2mod), inst.imm11 );
                setResultFlags(cpu, val, 0, 0);
                WriteReg(cpu,inst.r1,val);
                break;
            case OPCODE_TMAJI     :  // Rd = Tritwise Majority
                val=ternaryUNMB( ReadModReg(cpu, inst.r2, inst.r2mod), inst.imm11 );
                setResultFlags(cpu, val, 0, 0);
                WriteReg(cpu,inst.r1,val);
                break;
            case OPCODE_TADDI     :  // Rd = Tritwise Rs1 + immediate
                val=ternaryADDB( ReadModReg(cpu, inst.r2, inst.r2mod), inst.imm11 );
                setResultFlags(cpu, val, 0, 0);
                WriteReg(cpu,inst.r1,val);
                break;
            case OPCODE_TMULI     :  // Rd = Tritwise Rs1 * immediate
                val=ternaryMULB( ReadModReg(cpu, inst.r2, inst.r2mod), inst.imm11 );
                setResultFlags(cpu, val, 0, 0);
                WriteReg(cpu,inst.r1,val);
                break;
            case OPCODE_TMINI     :  // Rd = Tritwise MIN(Rs1, immediate)
                val=ternaryMINB( ReadModReg(cpu, inst.r2, inst.r2mod), inst.imm11 );
                setResultFlags(cpu, val, 0, 0);
                WriteReg(cpu,inst.r1,val);
                break;
            case OPCODE_TMAXI     :  // Rd = Tritwise MAX(Rs1, immediate)
                val=ternaryMAXB( ReadModReg(cpu, inst.r2, inst.r2mod), inst.imm11 );
                setResultFlags(cpu, val, 0, 0);
                WriteReg(cpu,inst.r1,val);
                break;
            default:
                break;
        }

        taintStepAfter( cpu );
        if( traceEnabled() )
        {
            traceSetTaint( taintRegMask(), taintMemCount() );
        }
        symStep( cpu, rawInst );
    }
}

/*****************************************************************************/

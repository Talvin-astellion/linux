#ifndef TERNARY_TAINT_H
#define TERNARY_TAINT_H

#include "cpu.h"

#include <stdio.h>

int taintInit( void );
void taintShutdown( void );
int taintEnabled( void );
void taintEnable( int enabled );

void taintMarkReg( Tryte reg );
void taintMarkMem( TriWord addr );
int taintReg( Tryte reg );
int taintMem( TriWord addr );
unsigned long taintRegMask( void );
unsigned long taintMemCount( void );

void taintStepBefore( TriCpu* cpu, TriWord inst );
void taintStepAfter( TriCpu* cpu );
void taintPrint( FILE* out );

#endif /* TERNARY_TAINT_H */

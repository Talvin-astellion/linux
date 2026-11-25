#ifndef TERNARY_TRACE_H
#define TERNARY_TRACE_H

#include "cpu.h"

#include <stdio.h>

int traceInit( const char* path );
void traceShutdown( void );
int traceEnabled( void );
void traceInstruction( TriCpu* cpu, TriWord inst );
void traceMemory( int isWrite, TriWord addr, Tryte value );
void traceSetTaint( unsigned long regMask, unsigned long memCount );

#endif /* TERNARY_TRACE_H */

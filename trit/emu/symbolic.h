#ifndef TERNARY_SYMBOLIC_H
#define TERNARY_SYMBOLIC_H

#include "cpu.h"

#include <stdio.h>

#define SYM_MAX_CONSTRAINTS 1024

typedef struct
{
    unsigned long seq;
    char kind[16];
    int opcode;
    Tryte r1;
    Tryte r2;
    Tryte r3;
    TriWord immediate;
    int sign;
    int taken;
} SymConstraint;

int symInit( const char* path );
void symShutdown( void );
int symEnabled( void );
void symStep( TriCpu* cpu, TriWord inst );
unsigned long symCount( void );
const SymConstraint* symGet( unsigned long index );
void symPrint( FILE* out );

#endif /* TERNARY_SYMBOLIC_H */

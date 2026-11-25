#ifndef TERNARY_TML_LOG_H
#define TERNARY_TML_LOG_H

#include "cpu.h"

#include <stdio.h>

#define TML_HASH_HEX 65
#define TML_PATH_MAX 512

typedef struct
{
    FILE* file;
    char path[TML_PATH_MAX];
    char prev_hash[TML_HASH_HEX];
    char head_hash[TML_HASH_HEX];
    TriWord head_tryte;
    unsigned long count;
    int enabled;
} TmlLog;

/* Verify an existing emulator JSONL log and restore its head before appending. */
int tmlLogOpen( TmlLog* log, const char* path );
void tmlLogClose( TmlLog* log );
int tmlLogAppend( TmlLog* log,
                  const char* event,
                  const char* detail,
                  int verdict,
                  int risk,
                  int state,
                  TriWord action,
                  const TriWord args[3] );

#endif /* TERNARY_TML_LOG_H */

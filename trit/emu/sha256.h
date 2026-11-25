#ifndef TERNARY_SHA256_H
#define TERNARY_SHA256_H

#include <stddef.h>
#include <stdint.h>

typedef struct
{
    uint32_t state[8];
    uint64_t bitlen;
    uint8_t data[64];
    size_t datalen;
} Sha256;

void sha256Init( Sha256* ctx );
void sha256Update( Sha256* ctx, const void* data, size_t len );
void sha256Final( Sha256* ctx, uint8_t hash[32] );
void sha256Hex( const void* data, size_t len, char out[65] );

#endif /* TERNARY_SHA256_H */

#include "tml_log.h"
#include "sha256.h"

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct
{
    unsigned long seq;
    char event[128];
    char detail[512];
    int64_t state, verdict, risk, action, args[3];
    char prev_hash[TML_HASH_HEX];
    char hash[TML_HASH_HEX];
} LogRecord;

static void jsonEscape( const char* src, char* dst, size_t size )
{
    size_t out = 0;
    if( size == 0 )
    {
        return;
    }
    for( ; src && *src && out + 1 < size; ++src )
    {
        unsigned char c = (unsigned char)*src;
        if( c == '"' || c == '\\' )
        {
            if( out + 2 >= size )
            {
                break;
            }
            dst[out++] = '\\';
            dst[out++] = (char)c;
        }
        else if( c == '\n' )
        {
            if( out + 2 >= size )
            {
                break;
            }
            dst[out++] = '\\';
            dst[out++] = 'n';
        }
        else if( c < 0x20 )
        {
            dst[out++] = ' ';
        }
        else
        {
            dst[out++] = (char)c;
        }
    }
    dst[out] = '\0';
}

static Tryte hashToTryte( const uint8_t hash[32] )
{
    uint32_t value = ( (uint32_t)hash[0] << 8 ) | hash[1];
    return (Tryte)( (int)( value % 19683 ) - 9841 );
}

static void utcTimestamp( char* out, size_t size )
{
    time_t now = time( NULL );
    struct tm tm_utc;
    if( gmtime_r( &now, &tm_utc ) == NULL )
    {
        snprintf( out, size, "1970-01-01T00:00:00Z" );
        return;
    }
    strftime( out, size, "%Y-%m-%dT%H:%M:%SZ", &tm_utc );
}

/* Strings in a record are already escaped, just as in the on-disk JSON. */
static int recordHash( const LogRecord* record, char hash[TML_HASH_HEX],
                       uint8_t digest[32] )
{
    static const char hex[] = "0123456789abcdef";
    char canonical[2048];
    Sha256 ctx;
    unsigned i;
    int len = snprintf( canonical, sizeof( canonical ),
                        "%lu|%s|%s|%"PRIi64"|%"PRIi64"|%"PRIi64"|%"PRIi64
                        "|%"PRIi64",%"PRIi64",%"PRIi64"|%s",
                        record->seq, record->event, record->detail,
                        record->verdict, record->risk, record->state, record->action,
                        record->args[0], record->args[1], record->args[2],
                        record->prev_hash );
    if( len < 0 || (size_t)len >= sizeof( canonical ) )
    {
        return -1;
    }
    sha256Init( &ctx );
    sha256Update( &ctx, canonical, (size_t)len );
    sha256Final( &ctx, digest );
    for( i = 0; i < 32; ++i )
    {
        hash[i * 2] = hex[digest[i] >> 4];
        hash[i * 2 + 1] = hex[digest[i] & 0x0F];
    }
    hash[64] = '\0';
    return 0;
}

static int consume( const char** cursor, const char* literal )
{
    size_t len = strlen( literal );
    if( strncmp( *cursor, literal, len ) != 0 )
    {
        return -1;
    }
    *cursor += len;
    return 0;
}

/* Read the bounded JSON string format emitted by jsonEscape(), keeping escapes. */
static int readString( const char** cursor, char* out, size_t size )
{
    const char* p = *cursor;
    size_t n = 0;
    if( *p++ != '"' )
    {
        return -1;
    }
    while( *p != '"' )
    {
        if( (unsigned char)*p < 0x20 || n + 1 >= size )
        {
            return -1;
        }
        if( *p == '\\' )
        {
            out[n++] = *p++;
            if( ( *p != '\\' && *p != '"' && *p != 'n' ) || n + 1 >= size )
            {
                return -1;
            }
        }
        out[n++] = *p++;
    }
    out[n] = '\0';
    *cursor = p + 1;
    return 0;
}

static int readInteger( const char** cursor, int64_t* value )
{
    const char* digits = *cursor + ( **cursor == '-' );
    char* end;
    intmax_t parsed;
    if( *digits < '0' || *digits > '9' ||
        ( *digits == '0' && digits[1] >= '0' && digits[1] <= '9' ) )
    {
        return -1;
    }
    errno = 0;
    parsed = strtoimax( *cursor, &end, 10 );
    if( errno != 0 || parsed < INT64_MIN || parsed > INT64_MAX )
    {
        return -1;
    }
    *value = (int64_t)parsed;
    *cursor = end;
    return 0;
}

static int readRecord( const char* line, LogRecord* record )
{
    const char* p = line;
    char timestamp[32];
    char* end;
    if( consume( &p, "{\"seq\":" ) != 0 || *p < '0' || *p > '9' ||
        ( *p == '0' && p[1] >= '0' && p[1] <= '9' ) )
    {
        return -1;
    }
    errno = 0;
    record->seq = strtoul( p, &end, 10 );
    if( errno != 0 )
    {
        return -1;
    }
    p = end;
    if( consume( &p, ",\"ts\":" ) != 0 ||
        readString( &p, timestamp, sizeof( timestamp ) ) != 0 ||
        consume( &p, ",\"event\":" ) != 0 ||
        readString( &p, record->event, sizeof( record->event ) ) != 0 ||
        consume( &p, ",\"detail\":" ) != 0 ||
        readString( &p, record->detail, sizeof( record->detail ) ) != 0 ||
        consume( &p, ",\"state\":" ) != 0 || readInteger( &p, &record->state ) != 0 ||
        consume( &p, ",\"verdict\":" ) != 0 || readInteger( &p, &record->verdict ) != 0 ||
        consume( &p, ",\"risk\":" ) != 0 || readInteger( &p, &record->risk ) != 0 ||
        consume( &p, ",\"action\":" ) != 0 || readInteger( &p, &record->action ) != 0 ||
        consume( &p, ",\"args\":[" ) != 0 || readInteger( &p, &record->args[0] ) != 0 ||
        consume( &p, "," ) != 0 || readInteger( &p, &record->args[1] ) != 0 ||
        consume( &p, "," ) != 0 || readInteger( &p, &record->args[2] ) != 0 ||
        consume( &p, "],\"prev_hash\":" ) != 0 ||
        readString( &p, record->prev_hash, sizeof( record->prev_hash ) ) != 0 ||
        consume( &p, ",\"hash\":" ) != 0 ||
        readString( &p, record->hash, sizeof( record->hash ) ) != 0 ||
        consume( &p, "}\n" ) != 0 || *p != '\0' )
    {
        return -1;
    }
    return 0;
}

static void advanceHead( TmlLog* log, const char* hash, const uint8_t digest[32] )
{
    snprintf( log->prev_hash, sizeof( log->prev_hash ), "%s", hash );
    snprintf( log->head_hash, sizeof( log->head_hash ), "%s", hash );
    log->head_tryte = hashToTryte( digest );
    ++log->count;
}

static int openFailed( TmlLog* log, int error )
{
    fclose( log->file );
    memset( log, 0, sizeof( *log ) );
    errno = error;
    return -1;
}

int tmlLogOpen( TmlLog* log, const char* path )
{
    char line[2048];
    if( log == NULL )
    {
        return -1;
    }
    memset( log, 0, sizeof( *log ) );
    if( path == NULL || path[0] == '\0' )
    {
        return 0;
    }
    if( strlen( path ) >= sizeof( log->path ) )
    {
        errno = ENAMETOOLONG;
        return -1;
    }
    snprintf( log->path, sizeof( log->path ), "%s", path );
    log->file = fopen( path, "a+" );
    if( log->file == NULL )
    {
        return -1;
    }
    snprintf( log->prev_hash, sizeof( log->prev_hash ), "%064d", 0 );
    snprintf( log->head_hash, sizeof( log->head_hash ), "%064d", 0 );
    rewind( log->file );
    while( fgets( line, sizeof( line ), log->file ) != NULL )
    {
        LogRecord record;
        char hash[TML_HASH_HEX];
        uint8_t digest[32];
        if( strchr( line, '\n' ) == NULL || readRecord( line, &record ) != 0 ||
            log->count == ULONG_MAX || record.seq != log->count ||
            strcmp( record.prev_hash, log->head_hash ) != 0 ||
            recordHash( &record, hash, digest ) != 0 || strcmp( record.hash, hash ) != 0 )
        {
            return openFailed( log, EILSEQ );
        }
        advanceHead( log, hash, digest );
    }
    if( ferror( log->file ) || fseek( log->file, 0, SEEK_END ) != 0 )
    {
        return openFailed( log, EIO );
    }
    log->enabled = 1;
    return 0;
}

void tmlLogClose( TmlLog* log )
{
    if( log != NULL && log->file != NULL )
    {
        fflush( log->file );
        fclose( log->file );
        log->file = NULL;
        log->enabled = 0;
    }
}

int tmlLogAppend( TmlLog* log,
                  const char* event,
                  const char* detail,
                  int verdict,
                  int risk,
                  int state,
                  TriWord action,
                  const TriWord args[3] )
{
    LogRecord record;
    char hash[TML_HASH_HEX];
    char timestamp[32];
    uint8_t digest[32];

    if( log == NULL || !log->enabled || log->file == NULL ||
        args == NULL || log->count == ULONG_MAX )
    {
        return -1;
    }

    record.seq = log->count;
    jsonEscape( event, record.event, sizeof( record.event ) );
    jsonEscape( detail, record.detail, sizeof( record.detail ) );
    record.verdict = verdict;
    record.risk = risk;
    record.state = state;
    record.action = (int64_t)action;
    record.args[0] = (int64_t)args[0];
    record.args[1] = (int64_t)args[1];
    record.args[2] = (int64_t)args[2];
    snprintf( record.prev_hash, sizeof( record.prev_hash ), "%s", log->prev_hash );
    utcTimestamp( timestamp, sizeof( timestamp ) );

    if( recordHash( &record, hash, digest ) != 0 )
    {
        return -1;
    }

    if( fprintf( log->file,
                 "{\"seq\":%lu,\"ts\":\"%s\",\"event\":\"%s\","
                 "\"detail\":\"%s\",\"state\":%d,\"verdict\":%d,"
                 "\"risk\":%d,\"action\":%"PRIi64","
                 "\"args\":[%"PRIi64",%"PRIi64",%"PRIi64"],"
                 "\"prev_hash\":\"%s\",\"hash\":\"%s\"}\n",
                 log->count,
                 timestamp,
                 record.event,
                 record.detail,
                 state,
                 verdict,
                 risk,
                 (int64_t)action,
                 (int64_t)args[0],
                 (int64_t)args[1],
                 (int64_t)args[2],
                 log->prev_hash,
                 hash ) < 0 || fflush( log->file ) != 0 )
    {
        log->enabled = 0;
        return -1;
    }

    advanceHead( log, hash, digest );
    return 0;
}

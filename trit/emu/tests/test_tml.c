#include "../cpu.h"
#include "../mem.h"
#include "../sha256.h"
#include "../tml.h"
#include "../tml_policy.h"
#include "../taint.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int failures = 0;

#define CHECK(cond) do { \
    if( !(cond) ) { \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        ++failures; \
    } \
} while( 0 )

static void testSha256( void )
{
    char hex[65];
    sha256Hex( "abc", 3, hex );
    CHECK( 0 == strcmp( hex,
           "ba7816bf8f01cfea414140de5dae2223"
           "b00361a396177a9cb410ff61f20015ad" ) );
}

static void testPolicy( void )
{
    TmlPolicy policy;
    tmlPolicyDefaults( &policy );
    CHECK( policy.tau_pause == 60 );
    CHECK( policy.tau_refuse == 85 );
    CHECK( tmlPolicyVerdict( &policy, 10 ) == TML_PROCEED );
    CHECK( tmlPolicyVerdict( &policy, 60 ) == TML_SACRED_ZERO );
    CHECK( tmlPolicyVerdict( &policy, 85 ) == TML_REFUSE );
}

static int fileContains( const char* path, const char* needle )
{
    FILE* file = fopen( path, "r" );
    char line[2048];
    int found = 0;
    if( file == NULL )
    {
        return 0;
    }
    while( fgets( line, sizeof( line ), file ) != NULL )
    {
        if( strstr( line, needle ) != NULL )
        {
            found = 1;
            break;
        }
    }
    fclose( file );
    return found;
}

static long fileLines( const char* path )
{
    FILE* file = fopen( path, "r" );
    char line[2048];
    long count = 0;
    if( file == NULL )
    {
        return 0;
    }
    while( fgets( line, sizeof( line ), file ) != NULL )
    {
        ++count;
    }
    fclose( file );
    return count;
}

static void testTmlLifecycle( void )
{
    TmlContext tml;
    TmlPolicy policy;
    char path[256];
    Tryte value;

    snprintf( path, sizeof( path ), "/tmp/tml_test_%ld.jsonl", (long)getpid() );
    remove( path );
    tmlPolicyDefaults( &policy );
    snprintf( policy.log_path, sizeof( policy.log_path ), "%s", path );

    CHECK( tmlInit( &tml, &policy, 1 ) == 0 );
    CHECK( tmlBootCheck( &tml ) == 0 );
    CHECK( tml.state == TML_FSM_ACTIVE );

    CHECK( tmlWriteAddr( ADDR_TML_RISK, ternaryFromInt64( 10 ) ) == 1 );
    CHECK( tmlWriteAddr( ADDR_TML_ACTION, ternaryFromInt64( 1 ) ) == 1 );
    CHECK( tml.verdict == TML_PROCEED );
    CHECK( tmlAllowActuation( &tml ) == 1 );

    CHECK( tmlWriteAddr( ADDR_TML_RISK, ternaryFromInt64( 70 ) ) == 1 );
    CHECK( tmlWriteAddr( ADDR_TML_ACTION, ternaryFromInt64( 2 ) ) == 1 );
    CHECK( tml.verdict == TML_SACRED_ZERO );
    CHECK( tml.state == TML_FSM_SACRED_PAUSE );
    CHECK( tmlAllowActuation( &tml ) == 0 );

    CHECK( tmlOverride( &tml, TML_PROCEED ) == 0 );
    CHECK( tml.state == TML_FSM_ACTIVE );
    CHECK( tmlAllowActuation( &tml ) == 1 );

    CHECK( tmlWriteAddr( ADDR_TML_RISK, ternaryFromInt64( 95 ) ) == 1 );
    CHECK( tmlWriteAddr( ADDR_TML_ACTION, ternaryFromInt64( 3 ) ) == 1 );
    CHECK( tml.verdict == TML_REFUSE );
    CHECK( tmlAllowActuation( &tml ) == 0 );
    CHECK( tml.state == TML_FSM_ACTIVE );

    tml.decision_valid = 0;
    tml.verdict = TML_PROCEED;
    CHECK( tmlAllowActuation( &tml ) == 0 );
    CHECK( tml.state == TML_FSM_FREEZE );
    CHECK( tmlHalted( &tml ) == 1 );

    CHECK( tmlRecover( &tml, 1234 ) != 0 );
    CHECK( tml.state == TML_FSM_FREEZE );
    CHECK( tmlRecover( &tml, TML_UNFREEZE_TOKEN ) == 0 );
    CHECK( tml.state == TML_FSM_ACTIVE );

    CHECK( tmlReadAddr( ADDR_TML_STATUS, &value ) == 1 );
    CHECK( ternary2int( value ) == (int64_t)TML_FSM_ACTIVE );
    CHECK( tml.log.count >= 8 );
    CHECK( tml.log.head_hash[0] != '\0' );
    CHECK( tml.log.head_hash[0] != '0' || tml.log.head_hash[63] != '0' );

    tmlShutdown( &tml );
    CHECK( fileLines( path ) >= 8 );
    CHECK( fileContains( path, "\"event\":\"freeze\"" ) );
    {
        unsigned long count = tml.log.count;
        char head[TML_HASH_HEX];
        snprintf( head, sizeof( head ), "%s", tml.log.head_hash );
        CHECK( tmlInit( &tml, &policy, 1 ) == 0 );
        CHECK( tml.log.count == count );
        CHECK( strcmp( tml.log.head_hash, head ) == 0 );
        CHECK( tmlBootCheck( &tml ) == 0 );
        tmlShutdown( &tml );
        CHECK( tml.log.count == count + 3 );
        CHECK( fileLines( path ) == (long)tml.log.count );
    }
    remove( path );
}

static void testLogResume( void )
{
    TmlLog log;
    TriWord args[3] = { 0, TRIWORD_MASK, UINT64_MAX };
    char path[256];
    char head[TML_HASH_HEX];
    char longText[1024];
    Tryte headTryte;

    snprintf( path, sizeof( path ), "/tmp/tml_resume_%ld.jsonl", (long)getpid() );
    remove( path );
    CHECK( tmlLogOpen( &log, path ) == 0 );
    CHECK( log.count == 0 );
    args[0] = ternaryFromInt64( -42 );
    CHECK( tmlLogAppend( &log, "event\"\\\n", "quoted \"detail\"\\\n\tжурнал",
                         -1, 7, 2, UINT64_MAX, args ) == 0 );
    memset( longText, '\\', sizeof( longText ) - 1 );
    longText[sizeof( longText ) - 1] = '\0';
    CHECK( tmlLogAppend( &log, longText, longText, 1, 0, 1, 0, args ) == 0 );
    snprintf( head, sizeof( head ), "%s", log.head_hash );
    headTryte = log.head_tryte;
    tmlLogClose( &log );

    CHECK( tmlLogOpen( &log, path ) == 0 );
    CHECK( log.count == 2 );
    CHECK( strcmp( log.head_hash, head ) == 0 );
    CHECK( strcmp( log.prev_hash, head ) == 0 );
    CHECK( log.head_tryte == headTryte );
    CHECK( tmlLogAppend( &log, NULL, NULL, 0, 0, 0, 0, args ) == 0 );
    tmlLogClose( &log );
    CHECK( tmlLogOpen( &log, path ) == 0 );
    CHECK( log.count == 3 );
    tmlLogClose( &log );
    remove( path );
}

static void rejectLog( const char* path, const char* contents, size_t size )
{
    TmlLog log;
    char actual[8192];
    FILE* file = fopen( path, "wb" );
    CHECK( file != NULL );
    if( file == NULL )
    {
        return;
    }
    CHECK( fwrite( contents, 1, size, file ) == size );
    fclose( file );
    CHECK( tmlLogOpen( &log, path ) != 0 );
    CHECK( log.file == NULL );
    CHECK( log.enabled == 0 );
    tmlLogClose( &log );
    file = fopen( path, "rb" );
    CHECK( file != NULL );
    if( file != NULL )
    {
        CHECK( fread( actual, 1, sizeof( actual ), file ) == size );
        CHECK( memcmp( actual, contents, size ) == 0 );
        fclose( file );
    }
}

static void testLogCorruption( void )
{
    TmlLog log;
    TriWord args[3] = { 0, 0, 0 };
    char path[256];
    char original[4096];
    char changed[8192];
    const char* fields[] = { "\"risk\":", "\"seq\":", "\"prev_hash\":\"",
                             "\"hash\":\"", "\"event\":\"" };
    size_t length, firstLine;
    unsigned i;
    FILE* file;

    snprintf( path, sizeof( path ), "/tmp/tml_corrupt_%ld.jsonl", (long)getpid() );
    remove( path );
    CHECK( tmlLogOpen( &log, path ) == 0 );
    CHECK( tmlLogAppend( &log, "decision", "evidence", 1, 7, 1, 0, args ) == 0 );
    CHECK( tmlLogAppend( &log, "decision", "evidence", 1, 7, 1, 0, args ) == 0 );
    tmlLogClose( &log );
    file = fopen( path, "rb" );
    CHECK( file != NULL );
    if( file == NULL )
    {
        return;
    }
    length = fread( original, 1, sizeof( original ) - 1, file );
    fclose( file );
    original[length] = '\0';
    firstLine = (size_t)( strchr( original, '\n' ) - original ) + 1;

    for( i = 0; i < sizeof( fields ) / sizeof( fields[0] ); ++i )
    {
        char* p;
        memcpy( changed, original, length + 1 );
        p = strstr( changed, fields[i] );
        CHECK( p != NULL );
        if( p != NULL )
        {
            p += strlen( fields[i] );
            *p = *p == '0' ? '1' : '0';
            rejectLog( path, changed, length );
        }
    }
    rejectLog( path, original, length - 1 ); /* missing final newline */
    memcpy( changed, original, length );
    memcpy( changed + length, "{\"seq\":", 7 );
    rejectLog( path, changed, length + 7 ); /* torn next record */
    memcpy( changed + length, "{}\n", 3 );
    rejectLog( path, changed, length + 3 ); /* missing fields */
    memcpy( changed + length, original, firstLine );
    rejectLog( path, changed, length + firstLine ); /* valid hash, restarted sequence */
    memcpy( changed, original + firstLine, length - firstLine );
    rejectLog( path, changed, length - firstLine ); /* deleted first record */
    memcpy( changed, original, length );
    changed[20] = '\0';
    rejectLog( path, changed, length ); /* embedded NUL */
    snprintf( changed, sizeof( changed ), "{\"seq\":184467440737095516160%s",
              original + strlen( "{\"seq\":0" ) );
    rejectLog( path, changed, strlen( changed ) ); /* numeric overflow */
    memset( changed, 'x', 2200 );
    changed[2200] = '\n';
    rejectLog( path, changed, 2201 ); /* oversized record */
    remove( path );
}

static void testLogWriteFailure( void )
{
    TmlLog log;
    TriWord args[3] = { 0, 0, 0 };
    char path[256];
    snprintf( path, sizeof( path ), "/tmp/tml_io_%ld.jsonl", (long)getpid() );
    remove( path );
    CHECK( tmlLogOpen( &log, path ) == 0 );
    fclose( log.file );
    log.file = fopen( "/dev/full", "w" );
    CHECK( log.file != NULL );
    if( log.file != NULL )
    {
        CHECK( tmlLogAppend( &log, "decision", "write fails", 1, 0, 1, 0, args ) != 0 );
        CHECK( log.count == 0 );
        CHECK( log.enabled == 0 );
        CHECK( strspn( log.head_hash, "0" ) == 64 );
        CHECK( tmlLogAppend( &log, "decision", "still fails", 1, 0, 1, 0, args ) != 0 );
        tmlLogClose( &log );
    }
    remove( path );
}

static void testTaintCoupling( void )
{
    TmlContext tml;
    TmlPolicy policy;
    char path[256];

    snprintf( path, sizeof( path ), "/tmp/tml_taint_%ld.jsonl", (long)getpid() );
    remove( path );
    tmlPolicyDefaults( &policy );
    policy.taint_penalty = 20;
    policy.taint_freeze = 1;
    snprintf( policy.log_path, sizeof( policy.log_path ), "%s", path );

    CHECK( tmlInit( &tml, &policy, 1 ) == 0 );
    CHECK( tmlBootCheck( &tml ) == 0 );
    CHECK( taintInit() == 0 );
    taintMarkReg( REG_P02 );

    CHECK( tmlWriteAddr( ADDR_TML_RISK, ternaryFromInt64( 70 ) ) == 1 );
    CHECK( tmlWriteAddr( ADDR_TML_ACTION, ternaryFromInt64( 9 ) ) == 1 );
    CHECK( tml.action_tainted == 1 );
    CHECK( tml.risk == 90 );
    CHECK( tml.verdict == TML_REFUSE );
    CHECK( tml.state == TML_FSM_FREEZE );
    CHECK( tmlAllowActuation( &tml ) == 0 );

    tmlShutdown( &tml );
    taintShutdown();
    remove( path );
}

static void testDisabled( void )
{
    TmlContext tml;
    TmlPolicy policy;
    tmlPolicyDefaults( &policy );
    snprintf( policy.log_path, sizeof( policy.log_path ), "%s", "" );
    CHECK( tmlInit( &tml, &policy, 0 ) == 0 );
    CHECK( tmlActive() == NULL );
    CHECK( tmlAllowActuation( &tml ) == 1 );
    CHECK( tmlHalted( &tml ) == 0 );
    tmlShutdown( &tml );
}

int main( void )
{
    testSha256();
    testPolicy();
    testTmlLifecycle();
    testLogResume();
    testLogCorruption();
    testLogWriteFailure();
    testTaintCoupling();
    testDisabled();

    if( failures != 0 )
    {
        printf( "%d test(s) failed\n", failures );
        return 1;
    }
    printf( "all TML tests passed\n" );
    return 0;
}

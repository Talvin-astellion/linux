#include "tml_policy.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char* trim( char* text )
{
    char* end;
    while( *text && isspace( (unsigned char)*text ) )
    {
        ++text;
    }
    end = text + strlen( text );
    while( end > text && isspace( (unsigned char)end[-1] ) )
    {
        *--end = '\0';
    }
    return text;
}

void tmlPolicyDefaults( TmlPolicy* policy )
{
    if( policy == NULL )
    {
        return;
    }
    memset( policy, 0, sizeof( *policy ) );
    policy->tau_pause = 60;
    policy->tau_refuse = 85;
    policy->require_decision = 1;
    policy->taint_penalty = 25;
    policy->taint_freeze = 0;
    snprintf( policy->system_id, sizeof( policy->system_id ), "%s", "ternary-emu" );
    snprintf( policy->log_path, sizeof( policy->log_path ), "%s", "tml_memory.jsonl" );
}

int tmlPolicyLoad( TmlPolicy* policy, const char* path )
{
    FILE* file;
    char line[512];

    if( policy == NULL || path == NULL )
    {
        return -1;
    }
    file = fopen( path, "r" );
    if( file == NULL )
    {
        return -1;
    }

    while( fgets( line, sizeof( line ), file ) != NULL )
    {
        char* key;
        char* value;
        char* eq;
        key = trim( line );
        if( key[0] == '\0' || key[0] == '#' )
        {
            continue;
        }
        eq = strchr( key, '=' );
        if( eq == NULL )
        {
            continue;
        }
        *eq = '\0';
        value = trim( eq + 1 );
        key = trim( key );

        if( strcmp( key, "system_id" ) == 0 )
        {
            snprintf( policy->system_id, sizeof( policy->system_id ), "%s", value );
        }
        else if( strcmp( key, "log" ) == 0 || strcmp( key, "log_path" ) == 0 )
        {
            snprintf( policy->log_path, sizeof( policy->log_path ), "%s", value );
        }
        else if( strcmp( key, "tau_pause" ) == 0 )
        {
            policy->tau_pause = atoi( value );
        }
        else if( strcmp( key, "tau_refuse" ) == 0 )
        {
            policy->tau_refuse = atoi( value );
        }
        else if( strcmp( key, "require_decision" ) == 0 )
        {
            policy->require_decision = atoi( value ) != 0;
        }
        else if( strcmp( key, "taint_penalty" ) == 0 )
        {
            policy->taint_penalty = atoi( value );
        }
        else if( strcmp( key, "taint_freeze" ) == 0 )
        {
            policy->taint_freeze = atoi( value ) != 0;
        }
    }
    fclose( file );

    if( policy->tau_pause < 0 )
    {
        policy->tau_pause = 0;
    }
    if( policy->tau_pause > 100 )
    {
        policy->tau_pause = 100;
    }
    if( policy->tau_refuse < 0 )
    {
        policy->tau_refuse = 0;
    }
    if( policy->tau_refuse > 100 )
    {
        policy->tau_refuse = 100;
    }
    if( policy->taint_penalty < 0 )
    {
        policy->taint_penalty = 0;
    }
    if( policy->taint_penalty > 100 )
    {
        policy->taint_penalty = 100;
    }
    return 0;
}

TmlVerdict tmlPolicyVerdict( const TmlPolicy* policy, int risk )
{
    if( risk >= policy->tau_refuse )
    {
        return TML_REFUSE;
    }
    if( risk >= policy->tau_pause )
    {
        return TML_SACRED_ZERO;
    }
    return TML_PROCEED;
}

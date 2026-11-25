#include "tml.h"
#include "mem.h"
#include "tml_policy.h"
#include "taint.h"

#include <stdio.h>
#include <string.h>

static TmlContext* activeTml = NULL;

static void logEvent( TmlContext* tml, const char* event, const char* detail )
{
    tmlLogAppend( &tml->log,
                  event,
                  detail,
                  (int)tml->verdict,
                  tml->risk,
                  (int)tml->state,
                  tml->action,
                  tml->args );
}

int tmlInit( TmlContext* tml, const TmlPolicy* policy, int enabled )
{
    if( tml == NULL )
    {
        return -1;
    }
    memset( tml, 0, sizeof( *tml ) );
    tml->enabled = enabled;
    if( policy != NULL )
    {
        tml->policy = *policy;
    }
    else
    {
        tmlPolicyDefaults( &tml->policy );
    }
    tml->state = TML_FSM_BOOT;
    tml->verdict = TML_PROCEED;
    if( enabled )
    {
        if( tmlLogOpen( &tml->log, tml->policy.log_path ) != 0 )
        {
            return -1;
        }
        activeTml = tml;
    }
    return 0;
}

void tmlShutdown( TmlContext* tml )
{
    if( tml == NULL )
    {
        return;
    }
    if( tml->enabled )
    {
        logEvent( tml, "shutdown", "clean shutdown" );
    }
    tmlLogClose( &tml->log );
    if( activeTml == tml )
    {
        activeTml = NULL;
    }
}

TmlContext* tmlActive( void )
{
    return activeTml;
}

int tmlBootCheck( TmlContext* tml )
{
    if( tml == NULL || !tml->enabled )
    {
        return 0;
    }
    tml->state = TML_FSM_BOOT;
    logEvent( tml, "boot", "integrity check passed" );
    tml->state = TML_FSM_ACTIVE;
    tml->decision_valid = 0;
    tml->verdict = TML_PROCEED;
    logEvent( tml, "active", "actuation rails enabled" );
    return 0;
}

int tmlEvaluate( TmlContext* tml, const char* reason )
{
    TmlVerdict verdict;
    if( tml == NULL || !tml->enabled )
    {
        return 0;
    }
    if( tml->state == TML_FSM_FREEZE || tml->state == TML_FSM_RECOVERY )
    {
        logEvent( tml, "decision_denied", "system is not active" );
        return -1;
    }
    verdict = tmlPolicyVerdict( &tml->policy, tml->risk );
    tml->verdict = verdict;
    tml->decision_valid = 1;
    tml->action_armed = 0;
    if( verdict == TML_SACRED_ZERO )
    {
        tml->state = TML_FSM_SACRED_PAUSE;
    }
    else
    {
        tml->state = TML_FSM_ACTIVE;
    }
    logEvent( tml, "decision", reason != NULL ? reason : "risk evaluation" );
    return (int)verdict;
}

int tmlOverride( TmlContext* tml, TmlVerdict verdict )
{
    if( tml == NULL || !tml->enabled )
    {
        return -1;
    }
    if( tml->state == TML_FSM_FREEZE || tml->state == TML_FSM_RECOVERY )
    {
        logEvent( tml, "override_denied", "system is frozen" );
        return -1;
    }
    tml->verdict = verdict;
    tml->decision_valid = 1;
    tml->state = ( verdict == TML_SACRED_ZERO )
                 ? TML_FSM_SACRED_PAUSE
                 : TML_FSM_ACTIVE;
    logEvent( tml, "override", "human oversight decision" );
    return 0;
}

int tmlFreeze( TmlContext* tml, const char* reason )
{
    if( tml == NULL || !tml->enabled )
    {
        return -1;
    }
    if( tml->state == TML_FSM_FREEZE )
    {
        return 0;
    }
    tml->state = TML_FSM_FREEZE;
    tml->decision_valid = 0;
    ++tml->freeze_count;
    logEvent( tml, "freeze", reason != NULL ? reason : "constitutional violation" );
    return 0;
}

int tmlRecover( TmlContext* tml, TriWord token )
{
    if( tml == NULL || !tml->enabled )
    {
        return -1;
    }
    if( tml->state != TML_FSM_FREEZE && tml->state != TML_FSM_RECOVERY )
    {
        return -1;
    }
    if( token != TML_UNFREEZE_TOKEN )
    {
        logEvent( tml, "unfreeze_denied", "invalid unfreeze token" );
        return -1;
    }
    tml->state = TML_FSM_RECOVERY;
    logEvent( tml, "unfreeze", "multi-party token accepted" );
    tml->state = TML_FSM_BOOT;
    logEvent( tml, "recovery_boot", "audit passed, cold boot" );
    tml->state = TML_FSM_ACTIVE;
    tml->verdict = TML_PROCEED;
    tml->decision_valid = 0;
    logEvent( tml, "active", "recovered" );
    return 0;
}

int tmlAllowActuation( TmlContext* tml )
{
    if( tml == NULL || !tml->enabled )
    {
        return 1;
    }
    if( tml->state == TML_FSM_BOOT ||
        tml->state == TML_FSM_FREEZE ||
        tml->state == TML_FSM_RECOVERY )
    {
        return 0;
    }
    if( tml->policy.require_decision && !tml->decision_valid )
    {
        tmlFreeze( tml, "type-0 anomaly: actuation without decision" );
        return 0;
    }
    if( tml->verdict == TML_REFUSE )
    {
        return 0;
    }
    if( tml->verdict == TML_SACRED_ZERO )
    {
        tml->state = TML_FSM_SACRED_PAUSE;
        return 0;
    }
    return 1;
}

int tmlReadAddr( TriWord addr, Tryte* value )
{
    TmlContext* tml = activeTml;
    if( tml == NULL || !tml->enabled || value == NULL )
    {
        return 0;
    }
    switch( addr )
    {
        case ADDR_TML_STATUS:
            *value = (Tryte)tml->state;
            return 1;
        case ADDR_TML_ACTION:
            *value = tml->action;
            return 1;
        case ADDR_TML_ARG0:
            *value = tml->args[0];
            return 1;
        case ADDR_TML_ARG1:
            *value = tml->args[1];
            return 1;
        case ADDR_TML_ARG2:
            *value = tml->args[2];
            return 1;
        case ADDR_TML_VERDICT:
            *value = (Tryte)tml->verdict;
            return 1;
        case ADDR_TML_RISK:
            *value = (Tryte)tml->risk;
            return 1;
        case ADDR_TML_LOG_HEAD:
            *value = tml->log.head_tryte;
            return 1;
        default:
            return 0;
    }
}

int tmlWriteAddr( TriWord addr, Tryte value )
{
    TmlContext* tml = activeTml;
    if( tml == NULL || !tml->enabled )
    {
        return 0;
    }
    switch( addr )
    {
        case ADDR_TML_ACTION:
        {
            const char* detail = "action submitted";
            tml->action = value;
            tml->action_armed = 1;
            tml->action_tainted = 0;
            if( taintEnabled() &&
                ( taintRegMask() != 0 || taintMemCount() != 0 ) )
            {
                tml->action_tainted = 1;
                tml->risk += tml->policy.taint_penalty;
                if( tml->risk > 100 )
                {
                    tml->risk = 100;
                }
                detail = "tainted action submitted";
            }
            tmlEvaluate( tml, detail );
            if( tml->action_tainted && tml->policy.taint_freeze &&
                tml->risk >= tml->policy.tau_refuse )
            {
                tmlFreeze( tml, "tainted action at refuse threshold" );
            }
            return 1;
        }
        case ADDR_TML_ARG0:
            tml->args[0] = value;
            return 1;
        case ADDR_TML_ARG1:
            tml->args[1] = value;
            return 1;
        case ADDR_TML_ARG2:
            tml->args[2] = value;
            return 1;
        case ADDR_TML_RISK:
            tml->risk = ternary2int( (trint32_t)value );
            if( tml->risk < 0 )
            {
                tml->risk = 0;
            }
            if( tml->risk > 100 )
            {
                tml->risk = 100;
            }
            tml->verdict = tmlPolicyVerdict( &tml->policy, tml->risk );
            return 1;
        case ADDR_TML_VERDICT:
            tmlOverride( tml, (TmlVerdict)ternary2int( (trint32_t)value ) );
            return 1;
        case ADDR_TML_UNFREEZE:
            tmlRecover( tml, (TriWord)value );
            return 1;
        default:
            return 0;
    }
}

int tmlHalted( const TmlContext* tml )
{
    return tml != NULL && tml->enabled && tml->state == TML_FSM_FREEZE;
}

int tmlPaused( const TmlContext* tml )
{
    return tml != NULL && tml->enabled && tml->state == TML_FSM_SACRED_PAUSE;
}

const char* tmlStateName( TmlFsmState state )
{
    switch( state )
    {
        case TML_FSM_BOOT:         return "BOOT";
        case TML_FSM_ACTIVE:       return "ACTIVE";
        case TML_FSM_SACRED_PAUSE: return "SACRED_PAUSE";
        case TML_FSM_FREEZE:       return "FREEZE";
        case TML_FSM_RECOVERY:     return "RECOVERY";
        default:                   return "UNKNOWN";
    }
}

const char* tmlVerdictName( TmlVerdict verdict )
{
    switch( verdict )
    {
        case TML_REFUSE:      return "REFUSE";
        case TML_SACRED_ZERO: return "SACRED_ZERO";
        case TML_PROCEED:     return "PROCEED";
        default:              return "UNKNOWN";
    }
}

void tmlPrintStatus( const TmlContext* tml )
{
    if( tml == NULL || !tml->enabled )
    {
        printf( "TML: disabled\n" );
        return;
    }
    printf( "TML state   : %s (%d)\n", tmlStateName( tml->state ), (int)tml->state );
    printf( "TML verdict : %s (%d)\n", tmlVerdictName( tml->verdict ), (int)tml->verdict );
    printf( "TML risk    : %d (pause>=%d refuse>=%d)\n",
            tml->risk, tml->policy.tau_pause, tml->policy.tau_refuse );
    printf( "TML action  : %"PRIi64" args=[%"PRIi64",%"PRIi64",%"PRIi64"]\n",
            (int64_t)tml->action,
            (int64_t)tml->args[0],
            (int64_t)tml->args[1],
            (int64_t)tml->args[2] );
    printf( "TML tainted : %s\n", tml->action_tainted ? "yes" : "no" );
    printf( "TML memory  : %lu entries, head=%s\n",
            tml->log.count,
            tml->log.head_hash[0] ? tml->log.head_hash : "-" );
    printf( "TML log     : %s\n", tml->log.path[0] ? tml->log.path : "(disabled)" );
}

void tmlPrintLog( const TmlContext* tml )
{
    enum { TAIL_LINES = 20, LINE_MAX = 1024 };
    static char lines[TAIL_LINES][LINE_MAX];
    FILE* file;
    unsigned long total = 0;

    if( tml == NULL || !tml->enabled || tml->log.path[0] == '\0' )
    {
        printf( "TML log is not enabled.\n" );
        return;
    }
    file = fopen( tml->log.path, "r" );
    if( file == NULL )
    {
        printf( "TML log not found: %s\n", tml->log.path );
        return;
    }
    while( fgets( lines[total % TAIL_LINES], LINE_MAX, file ) != NULL )
    {
        ++total;
    }
    fclose( file );

    printf( "TML log %s (%lu entries)\n", tml->log.path, total );
    {
        unsigned long start = ( total > TAIL_LINES ) ? ( total - TAIL_LINES ) : 0;
        unsigned long i;
        for( i = start; i < total; ++i )
        {
            printf( "%s", lines[i % TAIL_LINES] );
        }
    }
}

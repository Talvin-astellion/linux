#ifndef TERNARY_TML_H
#define TERNARY_TML_H

#include "cpu.h"
#include "tml_log.h"

#define TML_UNFREEZE_TOKEN 4242
#define TML_DETAIL_MAX 256

typedef enum
{
    TML_REFUSE = -1,
    TML_SACRED_ZERO = 0,
    TML_PROCEED = 1
} TmlVerdict;

typedef enum
{
    TML_FSM_BOOT = 0,
    TML_FSM_ACTIVE = 1,
    TML_FSM_SACRED_PAUSE = 2,
    TML_FSM_FREEZE = 3,
    TML_FSM_RECOVERY = 4
} TmlFsmState;

typedef struct
{
    int tau_pause;
    int tau_refuse;
    int require_decision;
    int taint_penalty;
    int taint_freeze;
    char system_id[64];
    char log_path[TML_PATH_MAX];
} TmlPolicy;

typedef struct
{
    int enabled;
    TmlPolicy policy;
    TmlFsmState state;
    TmlVerdict verdict;
    int risk;
    TriWord action;
    TriWord args[3];
    int decision_valid;
    int action_armed;
    int action_tainted;
    unsigned long freeze_count;
    TmlLog log;
} TmlContext;

int tmlInit( TmlContext* tml, const TmlPolicy* policy, int enabled );
void tmlShutdown( TmlContext* tml );
TmlContext* tmlActive( void );

int tmlBootCheck( TmlContext* tml );
int tmlEvaluate( TmlContext* tml, const char* reason );
int tmlOverride( TmlContext* tml, TmlVerdict verdict );
int tmlFreeze( TmlContext* tml, const char* reason );
int tmlRecover( TmlContext* tml, TriWord token );
int tmlAllowActuation( TmlContext* tml );

int tmlReadAddr( TriWord addr, Tryte* value );
int tmlWriteAddr( TriWord addr, Tryte value );

int tmlHalted( const TmlContext* tml );
int tmlPaused( const TmlContext* tml );
const char* tmlStateName( TmlFsmState state );
const char* tmlVerdictName( TmlVerdict verdict );
void tmlPrintStatus( const TmlContext* tml );
void tmlPrintLog( const TmlContext* tml );

#endif /* TERNARY_TML_H */

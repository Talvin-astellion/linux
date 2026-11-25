#ifndef TERNARY_TML_POLICY_H
#define TERNARY_TML_POLICY_H

#include "tml.h"

void tmlPolicyDefaults( TmlPolicy* policy );
int tmlPolicyLoad( TmlPolicy* policy, const char* path );
TmlVerdict tmlPolicyVerdict( const TmlPolicy* policy, int risk );

#endif /* TERNARY_TML_POLICY_H */

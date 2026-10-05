#pragma once
#include <stdbool.h>
#include <math.h>

/* Folytonos térbeli fázis: az irány csak a sebesség előjelét változtatja. */
static inline float app_flow_advance(float phase,double kw,bool forward,float seconds)
{
    if(fabs(kw)<0.0005 || seconds<=0)return phase;
    float cycles_per_second=.75f*(float)fmin(fabs(kw)/7.,1.);
    phase+=seconds*cycles_per_second*(forward?1.f:-1.f);
    return phase-floorf(phase);
}

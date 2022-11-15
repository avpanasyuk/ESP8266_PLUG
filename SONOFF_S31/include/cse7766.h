#pragma once

extern double power;
extern double voltage;
extern double current;
extern double energy;

extern struct ratio_t {
    float V, C, P;
} ratio;

extern void ReadCse7766();

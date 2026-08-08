#pragma once

extern double power;   // W
extern double voltage; // V
extern double current; // A
extern double energy;  // joules (Ws) since boot or the last ResetEnergy()

extern struct ratio_t {
    float V, C, P;
} ratio;

extern void ReadCse7766(); //!< drains the chip's UART stream; call every loop pass
extern void ResetEnergy();

#pragma once
#include <stdint.h>

// Pin interrupts through the GPIO DETECT signal (GPIOTE PORT event) instead of GPIOTE IN channels: an IN channel
// keeps drawing current in sleep, the DETECT signal hardly any. Takes the place of attachInterrupt().
// A pin calls its handler once when it reaches its active level, and is armed again by pinWakeArm() once it is
// back at its idle level.
void pinWakeAttach(uint32_t pin, void (*handler)(), bool activeHigh);
void pinWakeDetach(uint32_t pin);
void pinWakeArm();

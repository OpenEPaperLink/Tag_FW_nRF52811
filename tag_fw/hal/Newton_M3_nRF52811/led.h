#pragma once
#include <stdint.h>

// LED patterns sent by the AP (command 4, struct ledFlash), played while the tag sleeps
void setled(uint64_t parta, uint32_t partb);
void ledSleep(uint32_t ms);

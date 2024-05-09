#ifndef _EPD_IFACE_H_
#define _EPD_IFACE_H_

#include <stdbool.h>
#include <stdint.h>

void epdSetup();
void epdEnterSleep();

void draw();
void drawNoWait();
void drawWithSleep();
void epdWaitRdy();

#define EPD_LUT_DEFAULT 0
#define EPD_LUT_NO_REPEATS 1
#define EPD_LUT_FAST_NO_REDS 2
#define EPD_LUT_FAST 3

void selectLUT(uint8_t lut);

#include "uc_variant_043.h"
#include "unissd.h"
#include "dualssd.h"
#include "uc_variant_029.h"
#include "uc8159.h"
#include "uc8179.h"

#endif
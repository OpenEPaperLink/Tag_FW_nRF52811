#include <Arduino.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hal.h"
#include "lut.h"
#include "settings.h"
#include "wdt.h"
#include "drawing.h"

#include "epd_interface.h"
#include "uc_variant2_bwry.h"

#define EPD_CMD_POWER_OFF 0x02
#define EPD_CMD_POWER_ON 0x04
#define EPD_CMD_BOOSTER_SOFT_START 0x06
#define EPD_CMD_DEEP_SLEEP 0x07
#define EPD_CMD_DISPLAY_START_TRANSMISSION_DTM1 0x10
#define EPD_CMD_DISPLAY_REFRESH 0x12
#define EPD_CMD_DISPLAY_START_TRANSMISSION_DTM2 0x13
#define EPD_CMD_VCOM_INTERVAL 0x50
#define EPD_CMD_RESOLUTION_SETTING 0x61
#define EPD_CMD_UNKNOWN 0xF8

void epdvar2bwry::epdEnterSleep() {
    epd_cmd(EPD_CMD_POWER_OFF);
    epd_data(0x00);
    delay(100);

    epd_cmd(EPD_CMD_DEEP_SLEEP);
    epd_data(0xA5);
    delay(100);
}

void epdvar2bwry::epdSetup() {
    pinMode(EPD_BS, OUTPUT);
    digitalWrite(EPD_BS, 1);
    epdReset(EPD_BUSY_UC);

    epdWrite(0x66, 6, 0x49, 0x55, 0x13, 0x5D, 0x05, 0x10);
    
    epdWrite(0xB0, 1, 0x00);

    epdWrite(0x00, 2, 0x57, 0x69);
    epdWrite(0x03, 1, 0x00);

    epdWrite(EPD_CMD_BOOSTER_SOFT_START, 3, 0xD7, 0xDE, 0x12);

    epdWrite(0x41, 1, 0x00); // ok


    epdWrite(EPD_CMD_VCOM_INTERVAL, 1, 0x17);

    epdWrite(0x60, 2, 0x0C, 0x05);
    epdWrite(EPD_CMD_RESOLUTION_SETTING, 4, 0x00, 0xA8, 0x01, 0x90);
    
    epdWrite(0x84, 1, 0x00);
    
    epdWrite(0xE3, 1, 0xFF);

    epdWrite(0xE3, 1, 0xFF);

    epdWrite(EPD_CMD_POWER_ON, 0);
    delay(1000);
    printf("EPD INIT COMPLETE\n");
    // epdBusyWaitRising(5000);
}

void epdvar2bwry::epdWriteDisplayData()
{
    uint8_t *drawline_b = nullptr;
    uint8_t *drawline_r = nullptr;
    uint8_t *drawline_y = nullptr;

    drawline_b = (uint8_t *)calloc(this->effectiveXRes / 8, 1);
    drawline_r = (uint8_t *)calloc(this->effectiveXRes / 8, 1);
    drawline_y = (uint8_t *)calloc(this->effectiveXRes / 8, 1);

    epd_cmd(EPD_CMD_DISPLAY_START_TRANSMISSION_DTM1);
     //markData();
     //epdSelect();

    uint8_t *buf = (uint8_t *)calloc(this->effectiveXRes / 2, 1);
    uint32_t drawStart = millis();
    printf("Rendering draw... X: %u Y: %u\n", this->effectiveXRes, this->effectiveYRes);
    for (uint16_t curY = 0; curY < this->effectiveYRes; curY += 1)
    {
        wdt60s();

        memset(drawline_b, 0, this->effectiveXRes / 8);
        memset(drawline_r, 0, this->effectiveXRes / 8);
        memset(drawline_y, 0, this->effectiveXRes / 8);

        if (this->epdMirrorV)
        {
            drawItem::renderDrawLine(drawline_b, this->effectiveYRes - curY - 1, 0);
            drawItem::renderDrawLine(drawline_r, this->effectiveYRes - curY - 1, 1);
            drawItem::renderDrawLine(drawline_y, this->effectiveYRes - curY - 1, 2);
        }
        else
        {
            drawItem::renderDrawLine(drawline_b, curY, 0);
            drawItem::renderDrawLine(drawline_r, curY, 1);
            drawItem::renderDrawLine(drawline_y, curY, 2);
        }

        for (uint16_t x = 0; x < this->effectiveXRes;)
        {
            // merge color buffers into one
            uint8_t *temp = &(buf[x / 2]);
            for (uint8_t shift = 0; shift < 2; shift++)
            {
                *temp <<= 4;
                uint8_t curByte = x / 8;
                uint8_t curMask = (1 << (7 - (x % 8)));
                if ((drawline_r[curByte] & curMask))
                {
                    *temp |= 0x03;
                }
                else if (drawline_y[curByte] & curMask)
                {
                    *temp |= 0x02;
                }
                else if (drawline_b[curByte] & curMask)
                {
                }
                else
                {
                    *temp |= 0x01;
                }
                x++;
            }
        }
        // start transfer of the 'odd' data line
        for (uint16_t curYyy = 0; curYyy < (this->effectiveXRes / 2); curYyy += 1)
        {
            epd_data(buf[curYyy]);
        }
         //epdSPIAsyncWrite(buf, (this->effectiveXRes / 2));
         //epdSPIWait();
    }
    printf("\nRendering complete in %lu ms\n", millis() - drawStart);

    // flush the draw list, make sure items don't appear on subsequent screens
    drawItem::flushDrawItems();

    // wait until the last line of display has finished writing and clean our stuff up
     //epdSPIWait();
     //epdDeselect();
    if (buf)
        free(buf);
    if (drawline_b)
        free(drawline_b);
    if (drawline_r)
        free(drawline_r);
    if (drawline_y)
        free(drawline_y);
}

void epdvar2bwry::selectLUT(uint8_t lut) {
    // implement alternative LUTs here. Currently just reset the watchdog to two minutes,
    // to ensure it doesn't reset during the much longer bootup procedure
    lut += 1;  // make the compiler a happy camper
    wdt120s();
    return;
}

void epdvar2bwry::draw() {
    this->drawNoWait();
    this->epdWaitRdy();
    // epdBusyWaitRising(50000);
    // delay(100);
}
void epdvar2bwry::drawNoWait() {
    this->epdWriteDisplayData();
    printf("Starting draw\n");
    //epdWrite(0x68, 1, 0x00);
    // epdBusyWaitRising(200);
    // epd_cmd(EPD_CMD_DISPLAY_REFRESH);
    //epdWrite(0x12, 1, 0x01);
    
    epdWrite(0x12, 1, 0x01);
    //epd_cmd(EPD_CMD_DISPLAY_REFRESH);
    printf("draw complete\n");
}

void epdvar2bwry::epdWaitRdy() {
    epdBusyWaitRising(50000);
    printf("done waiting too\n");
    delay(100);
}
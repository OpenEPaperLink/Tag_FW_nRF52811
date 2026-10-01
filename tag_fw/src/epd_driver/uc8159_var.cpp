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

#include "uc8159_var.h"

#define CMD_PANEL_SETTING 0x00
#define CMD_POWER_SETTING 0x01
#define CMD_POWER_OFF 0x02
#define CMD_POWER_OFF_SEQUENCE 0x03
#define CMD_POWER_ON 0x04
#define CMD_BOOSTER_SOFT_START 0x06
#define CMD_DEEP_SLEEP 0x07
#define CMD_DISPLAY_START_TRANSMISSION_DTM1 0x10
#define CMD_DATA_STOP 0x11
#define CMD_DISPLAY_REFRESH 0x12
#define CMD_DISPLAY_IMAGE_PROCESS 0x13
#define CMD_VCOM_LUT_C 0x20
#define CMD_LUT_B 0x21
#define CMD_LUT_W 0x22
#define CMD_LUT_G1 0x23
#define CMD_LUT_G2 0x24
#define CMD_LUT_R0 0x25
#define CMD_LUT_R1 0x26
#define CMD_LUT_R2 0x27
#define CMD_LUT_R3 0x28
#define CMD_LUT_XON 0x29
#define CMD_PLL_CONTROL 0x30
#define CMD_TEMPERATURE_DOREADING 0x40
#define CMD_TEMPERATURE_SELECT 0x41
#define CMD_TEMPERATURE_WRITE 0x42
#define CMD_TEMPERATURE_READ 0x43
#define CMD_VCOM_INTERVAL 0x50
#define CMD_LOWER_POWER_DETECT 0x51
#define CMD_TCON_SETTING 0x60
#define CMD_RESOLUTION_SETING 0x61
#define CMD_SPI_FLASH_CONTROL 0x65
#define CMD_REVISION 0x70
#define CMD_STATUS 0x71
#define CMD_AUTO_MEASUREMENT_VCOM 0x80
#define CMD_READ_VCOM 0x81
#define CMD_VCOM_DC_SETTING 0x82
#define CMD_PARTIAL_WINDOW 0x90
#define CMD_PARTIAL_IN 0x91
#define CMD_PARTIAL_OUT 0x92
#define CMD_PROGRAM_MODE 0xA0
#define CMD_ACTIVE_PROGRAM 0xA1
#define CMD_READ_OTP 0xA2
#define CMD_EPD_EEPROM_SLEEP 0xB9
#define CMD_EPD_EEPROM_WAKE 0xAB
#define CMD_CASCADE_SET 0xE0
#define CMD_POWER_SAVING 0xE3
#define CMD_FORCE_TEMPERATURE 0xE5
#define CMD_LOAD_FLASH_LUT 0xE5

#define epdEepromSelect()           \
    do {                            \
        digitalWrite(EPD_HLT, LOW); \
    } while (0)

#define epdEepromDeselect()          \
    do {                             \
        digitalWrite(EPD_HLT, HIGH); \
    } while (0)

void dump(const uint8_t *a, const uint16_t l);

// EEPROM passthrough read (kept available for diagnostics but unused by the
// normal refresh flow — the official BWRY 60 firmware never accesses the
// on-flex flash via host passthrough during refresh; the panel reads its
// own waveform LUT via the 0xE5 0x03 (LOAD_FLASH_LUT) command).
void uc8159_var::epdEepromRead(uint16_t addr, uint8_t *data, uint16_t len) {
    epdWrite(CMD_SPI_FLASH_CONTROL, 1, 0x01);
    delay(1);
    epdEepromSelect();
    spi_write(0x03);
    spi_write(0x00);
    spi_write(addr >> 8);
    spi_write(addr & 0xFF);
    epdSPIReadBlock(data, len);
    epdEepromDeselect();
    delay(1);
    epdWrite(CMD_SPI_FLASH_CONTROL, 1, 0x00);
}

// Trigger an internal temperature read on the panel and clock out the
// 2-byte readback. The host doesn't use the value — the panel may latch it
// internally for compensation when PANEL_SETTING is re-issued with the
// run-mode bits set in byte 2 (0x06 in the official capture).
void uc8159_var::readPanelTemperature() {
    epdWrite(CMD_TEMPERATURE_SELECT, 1, 0x00);
    epdWrite(CMD_TEMPERATURE_DOREADING, 0);
    epdBusyWaitRising(2000);
    epdHardSPI(false);
    uint8_t t0 = spi3_read();
    uint8_t t1 = spi3_read();
    epdHardSPI(true);
    // printf("EPD temp raw: 0x%02X 0x%02X\n", t0, t1);
}

void uc8159_var::epdEnterSleep() {
    // Captured: 0x02 0x00 then ~69ms gap then 0x07 0xA5
    epdWrite(CMD_POWER_OFF, 1, 0x00);
    epdBusyWaitRising(2000);
    epdWrite(CMD_DEEP_SLEEP, 1, 0xA5);
    delay(100);
}

void uc8159_var::epdSetup() {
    epdReset(EPD_BUSY_UC);
    digitalWrite(EPD_BS, LOW);

    // PANEL_SETTING byte1 0xEF (vs capture's 0xE7) preserves the correct
    // mirror/scan direction for our hardware. Byte2 0x08 is the init-mode
    // value; it's changed to 0x06 after the temperature read (see drawNoWait).
    epdWrite(CMD_PANEL_SETTING,      2, 0xEF, 0x08);
    epdWrite(CMD_POWER_SETTING,      2, 0x07, 0x00);
    epdWrite(CMD_BOOSTER_SOFT_START, 3, 0xC7, 0xCC, 0x1B);

    // Power on BEFORE the remaining configuration.
    epdBusyWaitRising(250);
    epdWrite(CMD_POWER_ON, 0);
    epdBusyWaitRising(250);

    epdWrite(CMD_TEMPERATURE_SELECT, 1, 0x00);
    epdWrite(CMD_VCOM_INTERVAL,      1, 0x77);
    epdWrite(CMD_TCON_SETTING,       1, 0x22);
    epdWrite(CMD_RESOLUTION_SETING,  4, 0x02, 0x58, 0x01, 0xC0);
    epdWrite(CMD_POWER_SAVING,       1, 0xAA);
    epdWrite(CMD_FORCE_TEMPERATURE,  1, 0x03);
    delay(10);

    printf("EPD INIT COMPLETE\n");
}

void uc8159_var::selectLUT(uint8_t lut) {
    lut += 1;
    wdt120s();
    return;
}

void uc8159_var::epdWriteDisplayData() {
    uint8_t blocksize = 16;
    uint16_t byteWidth = this->effectiveXRes / 8;
    uint8_t screenrow_bw[byteWidth * blocksize];
    uint8_t screenrow_r[byteWidth * blocksize];
    uint8_t screenrow_y[byteWidth * blocksize];
    uint8_t screenrowInterleaved[byteWidth * 4];

    epd_cmd(CMD_DISPLAY_START_TRANSMISSION_DTM1);
    markData();
    epdSelect();

    for (uint16_t curY = 0; curY < this->effectiveYRes; curY += blocksize) {
        wdt30s();
        memset(screenrow_bw, 0, byteWidth * blocksize);
        memset(screenrow_r, 0, byteWidth * blocksize);
        memset(screenrow_y, 0, byteWidth * blocksize);

        // Render all three color planes
        for (uint8_t bcount = 0; bcount < blocksize; bcount++) {
            drawItem::renderDrawLine(screenrow_bw + (byteWidth * bcount), curY + bcount, 0);
            drawItem::renderDrawLine(screenrow_r  + (byteWidth * bcount), curY + bcount, 1);
            drawItem::renderDrawLine(screenrow_y  + (byteWidth * bcount), curY + bcount, 2);
        }

        for (uint8_t bcount = 0; bcount < blocksize; bcount++) {
            for (uint16_t curX = 0; curX < byteWidth; curX++) {
                interleaveColorToBuffer(
                    screenrowInterleaved + (curX * 4),
                    screenrow_bw[curX + (byteWidth * bcount)],
                    screenrow_r [curX + (byteWidth * bcount)],
                    screenrow_y [curX + (byteWidth * bcount)]
                );
            }

            epdSPIAsyncWrite(screenrowInterleaved, byteWidth * 4);
            epdSPIWait();
            epdDeselect();
            epdSelect();
        }
    }
    epdSPIWait();

    epdDeselect();
    epd_cmd(CMD_DATA_STOP);

    drawItem::flushDrawItems();
}

// 2 pixels per byte (4 bits per pixel). Color codes derived empirically:
//   black=0x0, red=0x4, yellow=0x5, white=0x3
// Each source byte holds 8 monochrome bits per plane; one output byte holds
// 2 pixels in nibbles (upper nibble = even pixel, lower = odd).
void uc8159_var::interleaveColorToBuffer(uint8_t *dst, uint8_t b, uint8_t r, uint8_t y) {
    for (int8_t shift = 3; shift >= 0; --shift) {
        uint8_t mask1 = 1 << (2 * shift);
        uint8_t mask2 = 1 << (2 * shift + 1);

        uint8_t n1;
        if (r & mask1)        n1 = 0x4;  // red
        else if (y & mask1)   n1 = 0x5;  // yellow
        else if (b & mask1)   n1 = 0x0;  // black
        else                  n1 = 0x3;  // white

        uint8_t n2;
        if (r & mask2)        n2 = 0x4;
        else if (y & mask2)   n2 = 0x5;
        else if (b & mask2)   n2 = 0x0;
        else                  n2 = 0x3;

        *dst++ = (n2 << 4) | n1;
    }
}

void uc8159_var::draw() {
    drawNoWait();
    epdBusyWaitRising(30000);
}

void uc8159_var::drawNoWait() {
    // Per the captured official refresh flow:
    //   1. Read panel temperature (latches compensation inside the panel)
    //   2. Re-issue PANEL_SETTING with byte2 = 0x06 — switches the panel
    //      from configuration mode to temperature-compensated run mode
    //   3. Stream the image via DTM1 (handled in epdWriteDisplayData)
    //   4. Trigger refresh with 0x12 0x00 (with a 0x00 data byte)
    // Byte1 stays 0xEF to preserve mirror direction from epdSetup.
    readPanelTemperature();
    epdWrite(CMD_PANEL_SETTING, 2, 0xEF, 0x06);
    epdWriteDisplayData();
    epdWrite(CMD_DISPLAY_REFRESH, 1, 0x00);
}

void uc8159_var::epdWaitRdy() {
    epdBusyWaitRising(50000);
    delay(100);
}

#include <Arduino.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lut.h"
#include "settings.h"
#include "hal.h"
#include "wdt.h"

#include "drawing.h"
#include "eeprom.h"
#include "userinterface.h"
#include "epd_interface.h"

#define CMD_DRV_OUTPUT_CTRL 0x01
#define CMD_SOFT_START_CTRL 0x0C
#define CMD_ENTER_SLEEP 0x10
#define CMD_DATA_ENTRY_MODE 0x11
#define CMD_SOFT_RESET 0x12
#define CMD_SOFT_RESET2 0x13
#define CMD_SETUP_VOLT_DETECT 0x15
#define CMD_TEMP_SENSOR_CONTROL 0x18
#define CMD_ACTIVATION 0x20
#define CMD_DISP_UPDATE_CTRL 0x21
#define CMD_DISP_UPDATE_CTRL2 0x22
#define CMD_WRITE_FB_BW 0x24
#define CMD_WRITE_FB_RED 0x26
#define CMD_VCOM_GLITCH_CTRL 0x2B
#define CMD_LOAD_OTP_LUT 0x31
#define CMD_WRITE_LUT 0x32
#define CMD_BORDER_WAVEFORM_CTRL 0x3C
#define CMD_WINDOW_X_SIZE 0x44
#define CMD_WINDOW_Y_SIZE 0x45
#define CMD_WRITE_PATTERN_RED 0x46
#define CMD_WRITE_PATTERN_BW 0x47
#define CMD_XSTART_POS 0x4E
#define CMD_YSTART_POS 0x4F
#define CMD_ANALOG_BLK_CTRL 0x74
#define CMD_DIGITAL_BLK_CTRL 0x7E

#define SCREEN_CMD_CLOCK_ON 0x80
#define SCREEN_CMD_CLOCK_OFF 0x01
#define SCREEN_CMD_ANALOG_ON 0x40
#define SCREEN_CMD_ANALOG_OFF 0x02
#define SCREEN_CMD_LATCH_TEMPERATURE_VAL 0x20
#define SCREEN_CMD_LOAD_LUT 0x10
#define SCREEN_CMD_USE_MODE_2 0x08  // modified commands 0x10 and 0x04
#define SCREEN_CMD_REFRESH 0xC7

// Fast updates on the M3 2.9" BWR (controller 0x15).
// Its waveform register (write 0x32, read 0x33) holds 257 bytes, not the 70 of the SSD1619A datasheet: 4 blocks of 56,
// one per LUT (block 3 for R=0/BW=0, 2 for R=0/BW=1, 1 for R=1, 0 unused), each 8 groups of
// [repeat, phase A, B, C, D, 1, 1] with phase bytes of voltage << 6 | frames (VSS, VSH1, VSL, VSH2). The timing is
// the same in every block. Bytes 224-256 hold the voltages and other settings, which stay as loaded from OTP.
// A fast update drives only the pixels that turn black or white: short shakes and pulses in that direction.
// The black and white waveforms mirror each other and those turns alternate for every pixel, so no DC builds up. Pixels that stay the same,
// and red ones, get R=1: a block that does nothing. That needs the image on the screen: it is drawn into the
// display's memory first, then read back line by line while the new image is written. Red can't change this way;
// if it does, the update becomes a full refresh. Other full refreshes are up to the AP (LUT 0); after a reboot the
// image on the screen isn't known, so the first image is always drawn with a full refresh.
#define WS_LEN 257
#define WS_BLOCK 56
#define WS_GROUP 7
#define WS_BLACK 3
#define WS_WHITE 2
#define WS_VSH1 229
#define WS_VSL 231
#define WS_FRAMES_ROOM 876        // OTP waveform length around 21 C, the temperature the pulse lengths were tuned at
#define PHASE(v, t) (((v) << 6) | (t))
#define V_VSH1 1
#define V_VSL 2

static uint32_t shownAddr = 0xFFFFFFFF;  // image on the screen, if drawn from flash without anything over it
static struct EepromImageHeader shownHeader;
static uint8_t *ws = nullptr;  // waveform for the fast update being drawn

// reading on the data line, at up to 2.5 MHz
static inline void clkDelay() {
    for (uint8_t i = 0; i < 4; i++) __NOP();
}

static void readBegin(uint8_t cmd) {
    epdHardSPI(false);
    digitalWrite(EPD_DC, LOW);
    digitalWrite(EPD_CS, LOW);
    for (int8_t i = 7; i >= 0; i--) {
        digitalWrite(EPD_CLK, LOW);
        digitalWrite(EPD_MOSI, (cmd >> i) & 1);
        delayMicroseconds(1);
        digitalWrite(EPD_CLK, HIGH);
        delayMicroseconds(1);
    }
    digitalWrite(EPD_CLK, LOW);
    digitalWrite(EPD_DC, HIGH);
    pinMode(EPD_MOSI, INPUT);
    delayMicroseconds(1);
}

static void readBytes(uint8_t *buf, uint16_t len) {
    for (uint16_t n = 0; n < len; n++) {
        uint8_t b = 0;
        for (uint8_t i = 0; i < 8; i++) {
            NRF_P0->OUTCLR = 1UL << EPD_CLK;
            clkDelay();
            NRF_P0->OUTSET = 1UL << EPD_CLK;
            clkDelay();
            b = (b << 1) | ((NRF_P0->IN >> EPD_MOSI) & 1);
        }
        buf[n] = b;
    }
}

static void readEnd() {
    digitalWrite(EPD_CS, HIGH);
    pinMode(EPD_MOSI, OUTPUT);
    digitalWrite(EPD_CLK, LOW);
    epdHardSPI(true);
}

// loads the OTP waveform for the current temperature and turns it into the one for a fast update
static bool fastWaveform(uint8_t lut) {
    uint32_t frames = 0;
    epdWrite(0x18, 1, 0x80);
    epdWrite(0x22, 1, 0xB1);
    epdWrite(0x20, 0);
    epdBusyWaitFalling(1000);
    ws = (uint8_t *)malloc(WS_LEN);
    if (!ws) return false;
    readBegin(0x33);
    readBytes(ws, WS_LEN);
    readEnd();

    // only a register laid out as described above
    for (uint8_t g = 0; g < 8; g++) {
        uint8_t *grp = ws + g * WS_GROUP;
        uint16_t len = 0;
        for (uint8_t p = 1; p <= 4; p++) {
            if (grp[p] & 0xC0) goto bad;
            len += grp[p];
            for (uint8_t b = 1; b < 4; b++)
                if ((grp[b * WS_BLOCK + p] & 0x3F) != grp[p]) goto bad;
        }
        for (uint8_t b = 1; b < 4; b++)
            if (grp[b * WS_BLOCK] != grp[0]) goto bad;
        frames += grp[0] * len;
    }
    if (ws[WS_VSH1] < 0x23 || ws[WS_VSH1] > 0x4B || ws[WS_VSL] < 0x1A || ws[WS_VSL] > 0x3A) goto bad;
    if (frames < 200 || frames > 6000) goto bad;

    {
        // slower particles in the cold: pulses as much longer as the OTP waveform is
        uint32_t t = ((lut == EPD_LUT_FAST ? 25 : 45) * frames) / WS_FRAMES_ROOM;
        uint32_t t2 = (20 * frames) / WS_FRAMES_ROOM;
        if (t > 63) t = 63;
        if (t2 > 63) t2 = 63;
        // the fastest is only the pulse; else 4 x 3 frames shake, the pulse, 2 x 2 frames shake and a shorter pulse,
        // which leaves the least grey and pink where black turns white (tried against an OTP refresh)
        static const uint8_t rpNormal[4] = {4, 1, 2, 1}, rpFast[4] = {0, 1, 0, 0};
        const uint8_t *rp = (lut == EPD_LUT_FAST) ? rpFast : rpNormal;
        const uint8_t tp[4][2] = {{3, 3}, {(uint8_t)t, 0}, {2, 2}, {(uint8_t)t2, 0}};
        memset(ws, 0, WS_BLOCK * 4);
        for (uint8_t b = 0; b < 4; b++) {
            uint8_t *blk = ws + b * WS_BLOCK;
            for (uint8_t g = 0; g < 8; g++) blk[g * WS_GROUP + 5] = blk[g * WS_GROUP + 6] = 1;
            for (uint8_t g = 0; g < 4; g++) {
                if (!rp[g]) continue;
                blk[g * WS_GROUP] = rp[g];
                blk[g * WS_GROUP + 1] = tp[g][0];
                blk[g * WS_GROUP + 2] = tp[g][1];
            }
        }
        // black and white mirror each other: shakes start away from the target, pulses go to it
        uint8_t *k = ws + WS_BLACK * WS_BLOCK, *w = ws + WS_WHITE * WS_BLOCK;
        for (uint8_t g = 0; g < 4; g++) {
            if (!rp[g]) continue;
            uint8_t *kg = k + g * WS_GROUP, *wg = w + g * WS_GROUP;
            if (g & 1) {
                kg[1] = PHASE(V_VSH1, tp[g][0]);
                wg[1] = PHASE(V_VSL, tp[g][0]);
            } else {
                kg[1] = PHASE(V_VSL, tp[g][0]);
                kg[2] = PHASE(V_VSH1, tp[g][1]);
                wg[1] = PHASE(V_VSH1, tp[g][0]);
                wg[2] = PHASE(V_VSL, tp[g][1]);
            }
        }
    }
    return true;
bad:
    printf("EPD: unknown waveform layout, no fast update\n");
    free(ws);
    ws = nullptr;
    return false;
}

// called before a new image from flash is put in the draw list
void epdPrepareDiff(uint8_t lut) {
    if (lut == EPD_LUT_DEFAULT || shownAddr == 0xFFFFFFFF) return;
    if (epd->controllerType != 0x15 || !tag.thirdColor) return;
    struct EepromImageHeader eih;
    eepromRead(shownAddr, &eih, sizeof(eih));
    if (memcmp(&eih, &shownHeader, sizeof(eih))) return;  // that slot was overwritten
    ((unissd *)epd)->prepareFast(lut);
}

void epdImageShown(uint32_t addr) {
    if (noAPShown || lowBatteryShown) return;  // the image with that over it isn't in flash
    eepromRead(addr, &shownHeader, sizeof(shownHeader));
    shownAddr = addr;
}

void unissd::prepareFast(uint8_t lut) {
    if (!fastWaveform(lut)) return;
    if (!drawAddImage(shownAddr)) {
        free(ws);
        ws = nullptr;
        return;
    }
    epdWriteDisplayData();  // the image on the screen into the display's memory
}

void unissd::setRamLine(uint16_t ry) {
    epdWrite(CMD_XSTART_POS, 1, (this->XOffset / 8));
    epdWrite(CMD_YSTART_POS, 2, ry & 0xFF, ry >> 8);
}

// writes the new image over the one in the display's memory: black/white as is, red where red or unchanged.
// false when red changes; the display's memory is then partly written
bool unissd::epdWriteFastData() {
    uint8_t wb = this->effectiveXRes / 8;
    uint8_t *buf = (uint8_t *)malloc(wb * 4);
    if (!buf) return false;
    uint8_t *nbw = buf, *nred = buf + wb, *obw = buf + 2 * wb, *ored = buf + 3 * wb;
    bool ok = true;
    for (uint16_t i = 0; i < this->effectiveYRes && ok; i++) {
        wdt60s();
        uint16_t y = this->YOffset + i;
        uint16_t ry = this->epdMirrorV ? y : (this->YOffset + this->effectiveYRes - 1 - i);
        memset(nbw, 0, wb * 2);
        drawItem::renderDrawLine(nbw, y, 0);
        drawItem::renderDrawLine(nred, y, 1);
        if (this->epdMirrorH) {
            drawItem::reverseBytes(nbw, wb);
            drawItem::reverseBytes(nred, wb);
        }
        for (uint8_t plane = 0; plane < 2; plane++) {
            uint8_t *old = plane ? ored : obw;
            setRamLine(ry);
            epdWrite(0x41, 1, plane);
            readBegin(0x27);
            readBytes(old, 1);  // dummy
            readBytes(old, wb);
            readEnd();
        }
        for (uint8_t b = 0; b < wb; b++) {
            if (nred[b] != ored[b]) ok = false;
            ored[b] = nred[b] | ~(nbw[b] ^ obw[b]);
        }
        if (!ok) break;
        setRamLine(ry);
        epd_cmd(CMD_WRITE_FB_BW);
        markData();
        epdSelect();
        epdSPIAsyncWrite(nbw, wb);
        epdSPIWait();
        epdDeselect();
        setRamLine(ry);
        epd_cmd(CMD_WRITE_FB_RED);
        markData();
        epdSelect();
        epdSPIAsyncWrite(ored, wb);
        epdSPIWait();
        epdDeselect();
    }
    free(buf);
    return ok;
}

void unissd::selectLUT(uint8_t lut) {
    // implement alternative LUTs here. Currently just reset the watchdog to two minutes,
    // to ensure it doesn't reset during the much longer bootup procedure
    lut += 1;  // make the compiler a happy camper
    wdt120s();
    return;
}

void unissd::epdEnterSleep() {
    epdReset(EPD_BUSY_SSD);
    epdWrite(CMD_SOFT_RESET2, 0);
    epdBusyWaitFalling(15);
    epdWrite(CMD_ENTER_SLEEP, 1, 0x03);
}
void unissd::epdSetup() {
#ifdef DEBUG_EPD
    printf("Starting EPD Setup\n");
#endif
    epdReset(EPD_BUSY_SSD);
    epdWrite(CMD_SOFT_RESET, 0);
    epdWaitUntilNotBusy(EPD_BUSY_SSD, 50);
	delay(10);
    switch (this->controllerType) {
        case 0x0F:
        case 0x12:
        case 0x15:
        case 0x0C:
            // stock init 1.6"
            epdWrite(CMD_DRV_OUTPUT_CTRL, 3, this->effectiveYRes & 0xFF, this->effectiveYRes >> 8, 0x00);
            if (epd->epdMirrorV) {
                epdWrite(CMD_DATA_ENTRY_MODE, 1, 0x03);
                epdWrite(CMD_WINDOW_Y_SIZE, 4, this->YOffset & 0xFF, this->YOffset >> 8, (this->YOffset + this->effectiveYRes) & 0xFF, (this->YOffset + this->effectiveYRes) >> 8);
            } else {
                epdWrite(CMD_DATA_ENTRY_MODE, 1, 0x01);
                epdWrite(CMD_WINDOW_Y_SIZE, 4, (this->YOffset + this->effectiveYRes) & 0xFF, (this->YOffset + this->effectiveYRes) >> 8, this->YOffset & 0xFF, this->YOffset >> 8);
            }
            epdWrite(CMD_WINDOW_X_SIZE, 2, this->XOffset / 8, ((this->XOffset + this->effectiveXRes) / 8) - 1);
            epdWrite(CMD_BORDER_WAVEFORM_CTRL, 1, 0x05);
            epdWrite(CMD_TEMP_SENSOR_CONTROL, 1, 0x80);
            // end stock init
            // added
            if (tag.thirdColor) {
                epdWrite(CMD_DISP_UPDATE_CTRL, 2, 0x08, 0x00);  // fix reversed image with stock setup
            } else {
                epdWrite(CMD_DISP_UPDATE_CTRL, 2, 0x48, 0x00);  // fix reversed image with stock setup
            }
            break;
        case 0x19:
        case 0x0A:
            // stock init 9.7"
            epdWrite(0x46, 1, 0xF7);
            delay(15);
            epdWrite(0x47, 1, 0xF7);
            delay(15);
            epdWrite(CMD_SOFT_START_CTRL, 5, 0xAE, 0xC7, 0xC3, 0xC0, 0x80);
            epdWrite(CMD_DRV_OUTPUT_CTRL, 3, 0x9F, 0x02, 0x00);
            // epdWrite(CMD_DRV_OUTPUT_CTRL, 3, 0x2E, 0x02, 0x00);
            epdWrite(CMD_DATA_ENTRY_MODE, 1, 0x02);
            epdWrite(CMD_WINDOW_X_SIZE, 4, 0xBF, 0x03, 0x00, 0x00);
            epdWrite(CMD_WINDOW_Y_SIZE, 4, 0x00, 0x00, 0x7F, 0x02);
            epdWrite(CMD_BORDER_WAVEFORM_CTRL, 1, 0x01);
            epdWrite(CMD_TEMP_SENSOR_CONTROL, 1, 0x80);
            epdWrite(CMD_DISP_UPDATE_CTRL2, 1, 0xF7);
            // end stock init
            // added
            epdWrite(CMD_DISP_UPDATE_CTRL, 2, 0x08, 0x00);  // fix reversed image with stock setup
            break;
    }
}

void unissd::epdWriteDisplayData() {
    // this display expects two entire framebuffers worth of data to be written, one for b/w and one for red
    uint8_t *buf[2] = {0, 0};  // this will hold pointers to odd/even data lines
    uint8_t c_end = 2;         // The loop must be executed 2 times if BWR, 1 time if BW
    if (!tag.thirdColor) {
        c_end = 1;
    }
#ifdef DEBUG_EPD
    printf("EPD: Render Start\n");
    uint32_t t_start = millis();
#endif
    for (uint8_t c = 0; c < c_end; c++) {
        switch (this->controllerType) {
            case 0x0F:
            case 0x12:
            case 0x15:
            case 0x0C:
                epdWrite(CMD_XSTART_POS, 1, (this->XOffset / 8));
                if (epd->epdMirrorV) {
                    epdWrite(CMD_YSTART_POS, 2, this->YOffset & 0xFF, (this->YOffset) >> 8);
                } else {
                    epdWrite(CMD_YSTART_POS, 2, (this->YOffset + this->effectiveYRes - 1) & 0xFF, (this->YOffset + this->effectiveYRes - 1) >> 8);
                }
                break;
            case 0x19:
            case 0x0A:
                epdWrite(CMD_XSTART_POS, 2, 0xBF, 0x03);
                epdWrite(CMD_YSTART_POS, 2, 0x00, 0x00);
                break;
        }
        if (c == 0) epd_cmd(CMD_WRITE_FB_BW);
        if (c == 1) epd_cmd(CMD_WRITE_FB_RED);
        delay(10);
        markData();
        epdSelect();

        for (uint16_t curY = 0 + epd->YOffset; curY < (epd->effectiveYRes + epd->YOffset); curY += 2) {
            wdt60s();
            // Get 'even' screen line
            buf[0] = (uint8_t *)calloc(epd->effectiveXRes / 8, 1);

            drawItem::renderDrawLine(buf[0], curY, c);

            if (epd->epdMirrorH) drawItem::reverseBytes(buf[0], epd->effectiveXRes / 8);
            // on the first pass, the second (buf[1]) buffer is unused, so we don't have to wait for it to flush to the display / free it
            if (buf[1]) {
                // wait for 'odd' display line to finish writing to the screen
                epdSPIWait();
                free(buf[1]);
            }

            // start transfer of even data line to the screen
            epdSPIAsyncWrite(buf[0], (epd->effectiveXRes / 8));

            // Get 'odd' screen display line
            buf[1] = (uint8_t *)calloc(epd->effectiveXRes / 8, 1);

            drawItem::renderDrawLine(buf[1], curY + 1, c);

            if (epd->epdMirrorH) drawItem::reverseBytes(buf[1], epd->effectiveXRes / 8);

            // wait until the 'even' data has finished writing
            epdSPIWait();
            free(buf[0]);

            // start transfer of the 'odd' data line
            epdSPIAsyncWrite(buf[1], (epd->effectiveXRes / 8));
        }
        // check if this was the first pass. If it was, we'll need to wait until the last display line finished writing
        if (c == 0) {
            epdSPIWait();
            epdDeselect();
            free(buf[1]);
            buf[1] = nullptr;
        }
    }
#ifdef DEBUG_EPD
    printf("Render done in %lu ms\n", millis() - t_start);
#endif
    // flush the draw list, make sure items don't appear on subsequent screens
    drawItem::flushDrawItems();

    // wait until the last line of display has finished writing and clean our stuff up
    epdSPIWait();
    epdDeselect();
    if (buf[1]) free(buf[1]);
}

void unissd::draw() {
    drawNoWait();
    getVoltage();
#ifdef DEBUG_EPD
    printf("EPD: Waiting for draw to finish...\n");
#endif
    epdWaitRdy();
#ifdef DEBUG_EPD
    printf("EPD: Draw finished\n");
#endif
}
void unissd::drawNoWait() {
#ifdef DEBUG_EPD
    printf("EPD: Starting to send image data\n");
#endif
    bool fast = false;
    if (ws) {
        fast = epdWriteFastData();
        if (fast) {
            drawItem::flushDrawItems();
        } else {
            printf("EPD: red changed, full refresh\n");
        }
    }
    if (fast) {
        epd_cmd(CMD_WRITE_LUT);
        for (uint16_t i = 0; i < WS_LEN; i++) epd_data(ws[i]);
        epdWrite(CMD_DISP_UPDATE_CTRL2, 1, SCREEN_CMD_REFRESH);
    } else {
        epdWriteDisplayData();
        epdWrite(CMD_DISP_UPDATE_CTRL2, 1, 0xF7);
    }
    if (ws) {
        free(ws);
        ws = nullptr;
    }
    shownAddr = 0xFFFFFFFF;  // set again by epdImageShown() when this is an image from flash
    epdWrite(CMD_ACTIVATION, 0);
#ifdef DEBUG_EPD
    printf("EPD: Send Data End\n");
#endif
}
void unissd::epdWaitRdy() {
    epdBusyWaitFalling(120000);
}
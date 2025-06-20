#include <Arduino.h>

#include "wdt.h"
#include "HAL_Newton_M3.h"
#include "../src/epd_driver/epd_interface.h"

#include "../../../shared/oepl-definitions.h"
#include "../include/eeprom.h"
#include "settings.h"

bool isValidNRF52811Pin(uint8_t pin) {
    switch (pin) {
        case 0: case 1:
        case 22: case 23: case 24:
        case 27: case 28:
        case 30: case 31:
            return true;
        default:
            return false;
    }
}

uint8_t getUICRByte(uint8_t offset) {
    // the nRF accesses registers and data in 32-bit words. We'll need to do some quick maffs to get individual bytes
    uint32_t reg = NRF_UICR->CUSTOMER[offset / 4];
    offset %= 4;
    offset *= 8;
    reg >>= offset;
    return (reg & 0xFF);
}

void identifyTagInfo() {
    // get some info about the tag from the UICR. Found the information when comparing the 'customer' area for various tags. The following information is known;
    // this has been deducted from comparing many of the UICR data from various tags
    /*
    00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F 10 11 12 13 14 15 16 17 18 19 1A 1B 1C 1D 1E 1F

    93 47 2E 06 16 01 15 04 00 11 01 58 02 C0 01 04 00 03 81 9D 00 00 48 FF FF FF FF FF FF FF FF FF     6.0 UC8159
    62 65 F6 06 16 07 04 04 00 0E 01 0A 02 98 00 38 00 07 01 9C 00 00 47 03 68 00 00 00 00 00 00 00		4.3 UCvar43 (no buttons)
    E0 5F F6 06 16 07 04 04 00 0E 01 0A 02 98 00 38 00 07 01 9C 00 00 47 03 68 00 00 00 00 00 00 00		4.3 UCvar43 (no buttons)
    B1 42 68 06 16 03 0E 04 00 0E 01 0A 02 98 00 38 00 07 01 9C 00 00 47 FF FF FF FF FF FF FF FF FF	 	4.3 UCvar43-dif batch (no buttons)
    78 56 34 12 17 01 1E 04 00 1A 01 0A 02 98 00 38 00 07 81 9D 00 00 47 03 0A 00 00 00 00 00 00 00     4.3 with buttons
    49 B6 77 7E 16 06 0F 04 00 0F 01 28 01 A0 00 39 00 07 81 9D 00 00 41 03 14 00 00 00 00 00 00 00	 	2.2 SSD1619
    DF 00 C6 05 15 0A 14 04 00 15 01 68 01 B8 00 38 00 07 01 9C 00 00 43 FF FF FF FF FF FF FF FF FF		2.6 Lite (no buttons)
    E5 16 52 06 16 02 18 04 00 12 01 C8 00 C8 00 04 00 07 01 9C 00 00 40 FF FF FF FF FF FF FF FF FF		1.6 Lite SSD1619 (no buttons)
    67 CC 22 7E 15 0B 15 04 00 15 01 80 01 A8 00 39 00 07 81 9D 00 00 42 FF FF FF FF FF FF FF FF FF		2.9 SSD1619
    0B 81 08 04 14 09 0F 04 00 0D 01 80 01 A8 00 38 00 07 81 9D 00 00 42 FF FF FF FF FF FF FF FF FF		2.9 UC8151
    26 36 42 7E 16 03 14 04 00 15 01 80 01 A8 00 38 00 07 01 9C 00 00 42 FF FF FF FF FF FF FF FF FF		2.9 Lite (SSD) (no buttons)
    F1 D5 B2 05 15 0A 04 04 00 12 01 90 01 2C 01 04 00 07 01 9C 00 00 46 FF FF FF FF FF FF FF FF FF		4.2 SSD
    79 19 1C 06 16 01 05 04 00 12 02 90 01 2C 01 04 00 07 81 9D 00 00 46 FF FF FF FF FF FF FF FF FF     4.2 SSD Yellow
    CA FE BA DE 15 0B 12 04 00 10 01 E0 01 20 03 39 00 03 81 9D 00 00 4C FF FF FF FF FF FF FF FF FF		7.4 UC8179
    F3 22 BC 05 15 0A 0D 04 00 19 01 A0 02 C0 03 38 07 07 01 80 00 00 64 FF FF FF FF FF FF FF FF FF		9.7 SSD
    AD BA FE CA 15 0A 1B 04 00 19 01 A0 02 C0 03 38 07 07 01 80 00 00 64 FF FF FF FF FF FF FF FF FF		9.7 type 2
    92 64 1D 05 15 06 12 04 00 0D 01 2C 01 C8 00 38 00 07 81 9D 00 00 44 FF FF FF FF FF FF FF FF FF		2.7 ? (buttons, leds, BWR)
    92 C3 80 05 15 08 19 04 00 12 01 18 03 10 01 04 07 07 01 80 00 00 63 FF FF FF FF FF FF FF FF FF     5.85 BWR
    22 F0 BF 05 15 0A 14 04 00 12 00 18 03 10 01 04 07 07 01 80 00 00 24 FF FF FF FF FF FF FF FF FF     5.85 BW
    99 78 B1 05 15 0A 06 04 00 0D 01 68 01 B8 00 38 07 07 01 80 00 00 43 FF FF FF FF FF FF FF FF FF     2.6"
    72 92 1E 7E 15 0B 09 04 00 15 00 80 01 A8 00 38 00 01 01 9C 00 00 22 FF FF FF FF FF FF FF FF FF     2.9" FREEZER
    2F A5 03 06 15 0C 07 04 00 15 00 80 01 A8 00 38 00 07 81 1D 00 00 4E FF FF FF FF FF FF FF FF FF     2.9" BW
    31 50 53 06 16 02 19 04 00 12 01 C8 00 C8 00 04 00 07 01 9C 00 00 40 FF FF FF FF FF FF FF FF FF
    4B F3 DE 04 15 05 07 04 00 0F 01 C8 00 90 00 38 00 07 01 19 00 00 4D FF FF FF FF FF FF FF FF FF     1.3-peghook
    C1 D3 42 06 16 02 0A 04 00 0A 01 80 02 C0 03 38 00 03 81 9D 00 00 4A FF FF FF FF FF FF FF FF FF     11.6" BWR
    04 5A 1F 05 15 06 12 04 00 0D 01 2C 01 C8 00 38 00 07 81 9D 00 00 44 FF FF FF FF FF FF FF FF FF     2.7" BWR
    
    F5 71 3E 07 16 09 04 04 00 17 03 A8 00 A8 00 05 00 07 81 9D 00 00 66 FF FF FF FF FF FF FF FF FF     1.6 BWRY
    F8 FA BF 7E 16 09 0D 04 00 17 03 28 01 A8 00 39 00 07 81 9D 00 00 67 FF FF FF FF FF FF FF FF FF     2.4 BWRY
    6B 75 A6 7E 16 08 15 04 00 17 03 90 01 A8 00 39 00 07 81 9D 00 00 68 FF FF FF FF FF FF FF FF FF     3.0 BWRY

---|----MAC----|--------------|--|SC|X----|Y----|--------|B1|--------|TY|


        MAC    | calib  |	  |?????|Xres |Yres |  ???   |capab|    |type|

    0x09 - controller?
            0x0D - UC8151?
            0x0E - UVvar43
            0x1A - UVvar43 (probably)
            0x0F - SSD (var2.2)
            0x10 - UC8179
            0x11 - UC8159
            0x12 - SSD (var1.6)
            0x15 - SSD (2.9 lite)
            0x19 - SSD (9.7)
            0x0A - SSD (4.2)
    0x0A -  Have third color?
    0x12 -  0x01 | (0x80 if it has a button)
    0x13 -  0x80 | (0x10 if it has a LED) | (0x0C ?? ) | (0x01 if it has a button)
    */
    uint16_t tmp = getUICRByte(0x0B);
    tmp |= getUICRByte(0x0C) << 8;
    uint16_t epdXRes = tmp;
    tmp = getUICRByte(0x0D);
    tmp |= getUICRByte(0x0E) << 8;
    uint16_t epdYRes = tmp;
    uint8_t controllerType = getUICRByte(0x09);
    uint8_t capabilities[2];
    capabilities[0] = getUICRByte(0x12);
    capabilities[1] = getUICRByte(0x13);
    tag.solumType = getUICRByte(0x16);
    tag.thirdColor = getUICRByte(0x0A);


    //Start of CustomSetup
    uint32_t magicNumber = getUICRByte(CUSTOM_SETUP_ADDR)<<(8*3);
    magicNumber |= getUICRByte(CUSTOM_SETUP_ADDR+1)<<(8*2);
    magicNumber |= getUICRByte(CUSTOM_SETUP_ADDR+2)<<(8*1);
    magicNumber |= getUICRByte(CUSTOM_SETUP_ADDR+3);
    uint8_t version_CS = getUICRByte(CUSTOM_SETUP_ADDR+4);
    uint8_t crc_CS = getUICRByte(CUSTOM_SETUP_ADDR+5);
    uint8_t len_CS = getUICRByte(CUSTOM_SETUP_ADDR+6);

    //At the moment there is no check on the CRC
    if(magicNumber == MAGIC_NUMBER && version_CS == 0x01 && len_CS == 0x11){

        uint8_t buttons = getUICRByte(CUSTOM_SETUP_ADDR+7);
        uint8_t buttonPin[8] = {0};
        uint8_t buttonMode[8] = {0};
        for(int i=0; i<8; i++){
            buttonPin[i] = getUICRByte(CUSTOM_SETUP_ADDR+8+i);
            buttonMode[i] = getUICRByte(CUSTOM_SETUP_ADDR+16+i);
        }
        
        printf("magic number found for custom setup\n");
        /*for(int i=0; i<8; i++){
            printf("Button %d: pin=%d, config=%d\n", i, buttonPin[i], buttonMode[i]);
        }*/

        for(int i=0; i<8; i++){
            //If bit I of button is set to 1
            if((buttons & (1 << i)) != 0){
                if(isValidNRF52811Pin(buttonPin[i])){
                    tag.customSetup.buttons |= (1 << i);
                    tag.customSetup.buttonPin[i] = buttonPin[i];
                    tag.customSetup.buttonMode[i] = buttonMode[i];
                    printf("- Button %d: pin=%d, config=0b", i, buttonPin[i]);
                    for (int j = 7; j >= 0; j--) {
                        putchar((buttonMode[i] & (1 << j)) ? '1' : '0');
                    }
                    printf(".\n");
                }else{
                    printf("error: button %d: pin=%d, config=0b%b: Pin not available.\n", i, buttonPin[i], buttonMode[i]);
                }
            }
        }


    }else{
        //printf("Magic number not found, version or lengh mismatch. Found: %x instead.\n", magicNumber);
    }

    //end of CustomSetupm

    switch (controllerType) {
        case 0x0A:
        case 0x0F:
        case 0x12:
        case 0x15:
        case 0x19:
            if (epdXRes == 792 && epdYRes == 272) {
                epd = new dualssd;
            } else {
                epd = new unissd;
            }
            break;
        case 0x0D:
            epd = new epdvar29;
            break;
        case 0x0E:
        case 0x1A:  // 4.3 variant with buttons? probably var43
            epd = new epdvar43;
            break;
        case 0x11:
            epd = new uc8159;
            break;
        case 0x10:
            epd = new uc8179;
            break;
        case 0x17:
            epd = new epdvarbwry;
            break;
    }

    epd->controllerType = controllerType;

    // set the resolution based on UICR data
    epd->Xres = epdXRes;
    epd->Yres = epdYRes;

    // set the effective (working) resolution
    epd->effectiveXRes = epdXRes;
    epd->effectiveYRes = epdYRes;

    // set default offset;
    epd->XOffset = 0;
    epd->YOffset = 0;

    if (capabilities[0] & 0x80)
        tag.buttonCount++;
    if (capabilities[1] & 0x01)
        tag.buttonCount++;
    //if (capabilities[1] & 0x02)
    //    tag.buttonCount++;
    if (capabilities[1] & 0x10)
        tag.hasLED = true;
    if (capabilities[0] & 0x01)
        tag.hasNFC = true;

#ifdef DEBUG_SHOW_TAGINFO

    printf("TagType report:\n");
    printf("Resolution: %d*%d Px\n", epd->Xres, epd->Yres);
    printf("Nb of buttons: %d\n", tag.buttonCount);
    if (tag.hasLED) {
        printf("This tag have a led: Yes\n");
    } else {
        printf("This tag have a led: No\n");
    }
    if (tag.hasNFC) {
        printf("This tag have NFC: Yes\n");
    } else {
        printf("This tag have NFC: No\n");
    }
    if (tag.thirdColor) {
        printf("This tag is Black and white only: No\n");
    } else {
        printf("This tag is Black and white only: Yes\n");
    }
#endif

    // we'll calculate image slot size here
    uint32_t imageSize = epd->Xres * epd->Yres / 4;
    tag.imageSize = ((imageSize + EEPROM_ERZ_SECTOR_SZ - 1) / EEPROM_ERZ_SECTOR_SZ) * EEPROM_ERZ_SECTOR_SZ;

    switch (tag.solumType) {
        case STYPE_SIZE_016:
            tag.macSuffix = 0xB0D0;
            epd->epdMirrorV = true;
            tag.OEPLtype = SOLUM_M3_BWR_16;
            epd->effectiveXRes = epdXRes;
            break;
        case STYPE_SIZE_022:
            tag.macSuffix = 0xB190;
            epd->drawDirectionRight = true;
            tag.OEPLtype = SOLUM_M3_BWR_22;
            epd->XOffset = 8;
            break;
        case STYPE_SIZE_022_LITE:
            tag.macSuffix = 0xE1D4;
            epd->drawDirectionRight = true;
            tag.OEPLtype = SOLUM_M3_BWR_22_LITE;
            epd->XOffset = 8;
            break;
        case STYPE_SIZE_026:
            tag.macSuffix = 0xB3D0;
            epd->drawDirectionRight = true;
            tag.OEPLtype = SOLUM_M3_BWR_26;
            epd->XOffset = 8;
            break;
        case STYPE_SIZE_029:
            tag.OEPLtype = SOLUM_M3_BWR_29;
            if (tag.buttonCount == 2) {
                // probably the 'normal' M3 version
                tag.macSuffix = 0xB290;
            } else {
                // probably the 'lite' version
                tag.macSuffix = 0xB2DA;
            }
            epd->drawDirectionRight = true;
            epd->XOffset = 8;
            break;
        case STYPE_SIZE_029_BW:
            tag.OEPLtype = SOLUM_M3_BW_29;
            tag.macSuffix = 0xAEB0;
            epd->drawDirectionRight = true;
            epd->XOffset = 8;
            break;
        case STYPE_SIZE_029_FREEZER:
            tag.OEPLtype = SOLUM_M3_BW_29;
            tag.macSuffix = 0x82D0;
            epd->drawDirectionRight = true;
            epd->XOffset = 8;
            break;
        case STYPE_SIZE_042:
            switch (tag.thirdColor) {
                case 0x01:
                    tag.macSuffix = 0xB6D0;
                    tag.OEPLtype = SOLUM_M3_BWR_42;
                    break;
                case 0x02:
                    tag.macSuffix = 0xC690;
                    tag.OEPLtype = SOLUM_M3_BWY_42;
                    break;
            }
            epd->epdMirrorV = true;
            break;
        case STYPE_SIZE_043:

            if (tag.customSetup.buttonPin[0] == 31 && tag.customSetup.buttonMode[0] == 0b01010101) {
                tag.macSuffix = 0xE790;
            } else {
                tag.macSuffix = 0xB7D0;
            }
            epd->drawDirectionRight = true;
            //            epd->mirrorH = true;
            tag.OEPLtype = SOLUM_M3_BWR_43;
            break;
        case STYPE_SIZE_058:
            tag.macSuffix = 0xE3D0;
            tag.OEPLtype = SOLUM_M3_BWR_58;
            break;
        case STYPE_SIZE_058_FREEZER:
            tag.macSuffix = 0x84D0;
            tag.OEPLtype = SOLUM_M3_BW_58;
            break;
        case STYPE_SIZE_060:
            tag.macSuffix = 0xB890;
            tag.OEPLtype = SOLUM_M3_BWR_60;
            break;
        case STYPE_SIZE_075:
            tag.macSuffix = 0xBC90;
            tag.OEPLtype = SOLUM_M3_BWR_75;
            epd->Xres = epdYRes;
            epd->Yres = epdXRes;
            epd->effectiveXRes = epdYRes;
            epd->effectiveYRes = epdXRes;
            break;
        case STYPE_SIZE_097:
            tag.macSuffix = 0xE4D0;
            epd->drawDirectionRight = true;
            tag.OEPLtype = SOLUM_M3_BWR_97;
            break;
        case STYPE_SIZE_027:
            tag.macSuffix = 0xB490;
            epd->drawDirectionRight = true;
            tag.OEPLtype = SOLUM_M3_BWR_27;
            break;
        case STYPE_SIZE_24_BWRY:
            tag.macSuffix = 0x9390;
            epd->drawDirectionRight = true;
            tag.OEPLtype = SOLUM_M3_BWRY_24;
            break;
        case STYPE_SIZE_16_BWRY:
            tag.macSuffix = 0x9290;
            epd->drawDirectionRight = false;
            tag.OEPLtype = SOLUM_M3_BWRY_16;
            epd->epdMirrorV = true;
            break;
        case STYPE_SIZE_30_BWRY:
            tag.macSuffix = 0x9490;
            epd->drawDirectionRight = false;
            tag.OEPLtype = SOLUM_M3_BWRY_30;
            epd->epdMirrorV = true;
            break;
        case STYPE_SIZE_013:
            tag.ledInverted = true;
            tag.macSuffix = 0xBDB0;
            epd->drawDirectionRight = true;
            epd->effectiveXRes = epdYRes;
            epd->effectiveYRes = epdXRes;
            tag.OEPLtype = SOLUM_M3_PEGHOOK_BWR_13;
            tag.boardType = NRF_BOARDTYPE_PEGHOOK;
            epd->XOffset = 8;
            break;
        case STYPE_SIZE_116:
            tag.macSuffix = 0xBA90;
            epd->drawDirectionRight = true;
            tag.OEPLtype = SOLUM_M3_BWR_116;
            break;
        case STYPE_SIZE_116B:
            tag.macSuffix = 0xBA9B;
            epd->drawDirectionRight = true;
            tag.OEPLtype = SOLUM_M3_BWR_116;
            break;
    }

    if (epd->drawDirectionRight) {
        epd->effectiveXRes = epdYRes;
        epd->effectiveYRes = epdXRes;
    }
}

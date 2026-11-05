#define __packed
#include "settings.h"

//#include <flash.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "powermgt.h"
#include "syncedproto.h"
#include "eeprom.h"
#include "../../shared/oepl-definitions.h"
#include "../../shared/oepl-proto.h"
#include "hal.h"

#define __xdata

#define SETTINGS_MAGIC 0xABBA5AA5

// The settings are in the last sector of the external flash. The image slots start at 0, and so does the firmware
// update, where the bootloader reads it: an update erases images, which the AP sends again, but not the settings.
static uint32_t settingsAddress() {
    return eepromGetSize() - EEPROM_ERZ_SECTOR_SZ;
}

struct tagsettings __xdata tagSettings = {0};

void loadDefaultSettings() {
    tagSettings.settingsVer = SETTINGS_STRUCT_VERSION;
    tagSettings.enableFastBoot = DEFAULT_SETTING_FASTBOOT;
    tagSettings.enableRFWake = DEFAULT_SETTING_RFWAKE;
    tagSettings.enableTagRoaming = DEFAULT_SETTING_TAGROAMING;
    tagSettings.enableScanForAPAfterTimeout = DEFAULT_SETTING_SCANFORAP;
    tagSettings.enableLowBatSymbol = DEFAULT_SETTING_LOWBATSYMBOL;
    tagSettings.enableNoRFSymbol = DEFAULT_SETTING_NORFSYMBOL;
    tagSettings.customMode = 0;
    tagSettings.fastBootCapabilities = 0;
    tagSettings.minimumCheckInTime = INTERVAL_BASE;
    tagSettings.fixedChannel = 0;
    tagSettings.batLowVoltage = BATTERY_VOLTAGE_MINIMUM;
}

void loadSettingsFromBuffer(uint8_t* p) {
    printf("SETTINGS: received settings from AP\n");
    switch (*p) {
        case SETTINGS_STRUCT_VERSION:  // the current tag struct
            memcpy((void*)&tagSettings, (void*)p, sizeof(struct tagsettings));
            break;
        default:
            printf("SETTINGS: received something we couldn't really process, version %d\n", *p);
        break;
    }
    tagSettings.fastBootCapabilities = capabilities;
    writeSettings();
}

static void upgradeSettings() {
    // add an upgrade strategy whenever you update the struct version
}

void loadSettings() {
    uint8_t settingsTempBuffer[sizeof(struct tagsettings)];
    eepromRead(settingsAddress()+4, (void*)settingsTempBuffer, sizeof(struct tagsettings));
    memcpy((void*)&tagSettings, (void*)settingsTempBuffer, sizeof(struct tagsettings));
    uint32_t valid = 0;
    eepromRead(settingsAddress(), (void*)&valid, 4);
    if (tagSettings.settingsVer == 0xFF || valid != SETTINGS_MAGIC) {
        // settings not set. load the defaults
        loadDefaultSettings();
        printf("SETTINGS: Loaded default settings\n");
    } else {
        if (tagSettings.settingsVer < SETTINGS_STRUCT_VERSION) {
            // upgrade
            upgradeSettings();
            printf("SETTINGS: Upgraded from previous version\n");
        } else {
            // settings are valid
            printf("SETTINGS: Loaded from EEPROM\n");
        }
    }
}

void writeSettings() {
    eepromErase(settingsAddress(), 1);
    uint32_t valid = SETTINGS_MAGIC;
    eepromWrite(settingsAddress(), (void*)&valid, 4);
    eepromWrite(settingsAddress()+4, (void*)&tagSettings, sizeof(tagSettings));
    printf("SETTINGS: Updated settings in EEPROM\n");
}

void invalidateSettingsEEPROM() {
    int32_t __xdata valid = 0x0000;
    eepromWrite(settingsAddress(), (void*)&valid, 4);
}
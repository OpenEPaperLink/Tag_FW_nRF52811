#include "syncedproto.h"

#include <Arduino.h>
#include <md5.h>

#include "comms.h"
#include "drawing.h"
#include "eeprom.h"
#include "hal.h"
#include "powermgt.h"
#include "../../shared/oepl-definitions.h"
#include "../../shared/oepl-proto.h"
#include "userinterface.h"
#include "wdt.h"

#define FW_LOC 0
#define FW_METADATA_LOC 196608
#define MAGIC1 0xABBAABBA
#define MAGIC2 0xDEADBEEF
struct fwmetadata {
    uint32_t fwsize;
    uint32_t magic1;
    uint32_t magic2;
};

#define EEPROM_SETTINGS_SIZE 4096

#define BLOCKSIZE_MS 280  // was 270

#define FWNRF
#define LEDSENABLED
#define PERSISTENTVAR
#define EEPROM_IMG_START 0

;
#define HAL_PacketRX commsRxUnencrypted
#define HAL_msDelay delay

void dump(const uint8_t *a, const uint16_t l);

extern void executeCommand(uint8_t cmd);  // this is defined in main.c

// What the bootloader accepts: an application of 8 KB up to the flash above the bootloader, linked at 0x4000
#define FW_APP_START 0x4000UL
#define FW_MIN_SIZE 8192UL
#define FW_MAX_SIZE (196608UL - FW_APP_START)
#define FW_RAM_START 0x20000000UL
#define FW_RAM_END 0x20006000UL

static bool fwSizeOk(uint32_t size) {
    return size >= FW_MIN_SIZE && size <= FW_MAX_SIZE;
}

// The downloaded file must be an application for this tag. Its vector table holds the initial stack pointer (in
// RAM) and the reset handler (Thumb, inside the application). The full image with the bootloader in front, or a
// firmware for another tag, fails that: flashing it would leave a tag that only a flasher can bring back.
static bool fwImageOk(uint32_t size) {
    uint32_t vectors[2];
    eepromRead(FW_LOC, vectors, sizeof(vectors));
    uint32_t sp = vectors[0], reset = vectors[1];
    if (sp <= FW_RAM_START || sp > FW_RAM_END || (sp & 3)) return false;
    if (!(reset & 1)) return false;
    reset &= ~1UL;
    return reset >= FW_APP_START && reset < FW_APP_START + size;
}

static void saveUpdateMetadata(uint32_t size) {
    struct fwmetadata metadata;
    metadata.magic1 = MAGIC1;
    metadata.magic2 = MAGIC2;
    metadata.fwsize = size;
    eepromWrite(FW_METADATA_LOC, &metadata, sizeof(struct fwmetadata));
}

static bool validateEepromMD5(uint64_t ver, uint32_t eepromstart, uint32_t flen) {
    unsigned char hash[16];
    char chunk[512];
    MD5 md5;

    // Open the executable itself for reading
    md5.reset();
    for (uint32_t offset = 0; offset < flen; offset += 512) {
        uint32_t len = flen - offset;
        if (len > 512) len = 512;
        eepromRead(eepromstart + offset, chunk, 512);
        md5.update(chunk, len);
    }

    // Retrieve the final hash
    md5.finalize(hash);

    bool isValid = ver == *((uint64_t *)hash);
    if (!isValid) {
        printf("MD5 failed check! This is what we should get:\n");
        dump((const uint8_t *)&(ver), 8);
        printf("This is what we got:\n");
        dump(hash, 16);
    }

#ifdef DEBUG_DONTVALIDATEPROTO
    return true;
#else
    return isValid;
#endif
}

#include "../../common/oepl-protocol.cpp"
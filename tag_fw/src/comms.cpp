#include <Arduino.h>
#include "comms.h"

#include <string.h>

#include "../../shared/oepl-definitions.h"
#include "../../shared/oepl-proto.h"

#include "hal.h"


extern uint8_t mSelfMac[8];
uint8_t mLastLqi = 0;
int8_t mLastRSSI = 0;

uint8_t commsGetLastPacketLQI(void) {
    return mLastLqi;
}

int8_t commsGetLastPacketRSSI(void) {
    return mLastRSSI;
}

#define RX_WAIT_TICKS 33  // ~1 ms of the 32768 Hz clock

int8_t commsRxUnencrypted(uint8_t *data) {
    // The callers poll until a deadline. Instead of spinning, sleep until the radio has a packet (its interrupt
    // wakes us up) or for at most ~1 ms.
    if (!radioRxPending()) {
        initRTC0Ticks(RX_WAIT_TICKS);
        __WFE();
        __SEV();
        __WFE();
        if (!radioRxPending()) return COMMS_RX_ERR_NO_PACKETS;
    }
    memset(data, 0, 128);
    int8_t rxedLen = radioRxDequeuePkt(data, 128, &mLastRSSI, &mLastLqi);
    if (rxedLen < 0)
        return COMMS_RX_ERR_NO_PACKETS;
    if (memcmp((void *)&data[5], mSelfMac, 8))  // Here we filter for our MAC Address... in software...
        return COMMS_RX_ERR_INVALID_PACKET;
    return rxedLen;
}

bool commsTxNoCpy(const void *packetp) {
    return radioTxLL(packetp);
}

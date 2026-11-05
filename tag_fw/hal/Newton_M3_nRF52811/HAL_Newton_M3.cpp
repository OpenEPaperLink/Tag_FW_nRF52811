#include <Arduino.h>

#include "wdt.h"
#include "HAL_Newton_M3.h"
#include "epd_driver/epd_interface.h"

int8_t temperature = 0;
uint16_t batteryVoltage = 0;
uint32_t batteryRaw = 0;
bool lowBattery = false;
bool disablePinInterruptSleep = false;

epdInterface* epd = nullptr;
tagSpecs tag;

int8_t startHFCLK(void) {
    if (!isHFCLKstable()) {
        NRF_CLOCK->EVENTS_HFCLKSTARTED = 0;
        NRF_CLOCK->TASKS_HFCLKSTART = 1U;

        while (!isHFCLKstable())
            ;

        return 0;
    } else {
        return -1;
    }
}

uint8_t isHFCLKstable(void) {
    uint32_t stable = (uint32_t)(CLOCK_HFCLKSTAT_STATE_Running << CLOCK_HFCLKSTAT_STATE_Pos) | (CLOCK_HFCLKSTAT_SRC_Xtal << CLOCK_HFCLKSTAT_SRC_Pos);

    if (NRF_CLOCK->HFCLKSTAT == stable) {
        NRF_CLOCK->EVENTS_HFCLKSTARTED = 0;

        return 1;
    } else {
        return 0;
    }
}

void setupBatteryVoltage() {
    NRF_SAADC->CH[0]
        .CONFIG = (SAADC_CH_CONFIG_GAIN_Gain1_6 << SAADC_CH_CONFIG_GAIN_Pos) |
                  (SAADC_CH_CONFIG_MODE_SE << SAADC_CH_CONFIG_MODE_Pos) |
                  (SAADC_CH_CONFIG_REFSEL_Internal << SAADC_CH_CONFIG_REFSEL_Pos) |
                  (SAADC_CH_CONFIG_RESN_Bypass << SAADC_CH_CONFIG_RESN_Pos) |
                  (SAADC_CH_CONFIG_RESP_Bypass << SAADC_CH_CONFIG_RESP_Pos) |
                  (SAADC_CH_CONFIG_TACQ_3us << SAADC_CH_CONFIG_TACQ_Pos);
    NRF_SAADC->CH[0].PSELP = SAADC_CH_PSELP_PSELP_VDD << SAADC_CH_PSELP_PSELP_Pos;
    NRF_SAADC->CH[0].PSELN = SAADC_CH_PSELN_PSELN_NC << SAADC_CH_PSELN_PSELN_Pos;
    NRF_SAADC->RESOLUTION = SAADC_RESOLUTION_VAL_14bit << SAADC_RESOLUTION_VAL_Pos;
    NRF_SAADC->RESULT.MAXCNT = 1;
    NRF_SAADC->RESULT.PTR = (uint32_t)&batteryRaw;
    NRF_SAADC->SAMPLERATE = SAADC_SAMPLERATE_MODE_Task << SAADC_SAMPLERATE_MODE_Pos;
    NRF_SAADC->ENABLE = SAADC_ENABLE_ENABLE_Enabled << SAADC_ENABLE_ENABLE_Pos;
    NRF_SAADC->TASKS_CALIBRATEOFFSET = 1;
    while (NRF_SAADC->EVENTS_CALIBRATEDONE == 0)
        ;
    NRF_SAADC->EVENTS_CALIBRATEDONE = 0;
    while (NRF_SAADC->STATUS == (SAADC_STATUS_STATUS_Busy << SAADC_STATUS_STATUS_Pos))
        ;
    NRF_SAADC->ENABLE = SAADC_ENABLE_ENABLE_Disabled << SAADC_ENABLE_ENABLE_Pos;
}

void getVoltage() {
    NRF_SAADC->ENABLE = SAADC_ENABLE_ENABLE_Enabled << SAADC_ENABLE_ENABLE_Pos;

    NRF_SAADC->TASKS_START = 1;
    while (NRF_SAADC->EVENTS_STARTED == 0)
        ;
    NRF_SAADC->EVENTS_STARTED = 0;

    NRF_SAADC->TASKS_SAMPLE = 1;
    while (NRF_SAADC->EVENTS_END == 0)
        ;
    NRF_SAADC->EVENTS_END = 0;

    // Convert the result to voltage
    // Result = [V(p) - V(n)] * GAIN/REFERENCE * 2^(RESOLUTION)
    // Result = (VDD - 0) * ((1/6) / 0.6) * 2^14
    // VDD = Result / 4551.1
    batteryRaw *= 10000;
    batteryRaw /= 45511;
    batteryVoltage = (uint16_t)batteryRaw;

    printf("voltage = %d\n", batteryVoltage);

    // Stop the SAADC, since it's not used anymore.
    NRF_SAADC->TASKS_STOP = 1;
    while (NRF_SAADC->EVENTS_STOPPED == 0)
        ;
    NRF_SAADC->EVENTS_STOPPED = 0;
    NRF_SAADC->ENABLE = SAADC_ENABLE_ENABLE_Disabled << SAADC_ENABLE_ENABLE_Pos;
}

void setupTemperature() {
    // Trigger temperature measurement

    NRF_TEMP->A0 = NRF_FICR->TEMP.A0;
    NRF_TEMP->A1 = NRF_FICR->TEMP.A1;
    NRF_TEMP->A2 = NRF_FICR->TEMP.A2;
    NRF_TEMP->A3 = NRF_FICR->TEMP.A3;
    NRF_TEMP->A4 = NRF_FICR->TEMP.A4;
    NRF_TEMP->A5 = NRF_FICR->TEMP.A5;

    // top two 'B0' and 'B1' are also in binary.h by the Arduino framework...
    NRF_TEMP->B0 = NRF_FICR->TEMP.B0;
    NRF_TEMP->B1 = NRF_FICR->TEMP.B1;
    NRF_TEMP->B2 = NRF_FICR->TEMP.B2;
    NRF_TEMP->B3 = NRF_FICR->TEMP.B3;
    NRF_TEMP->B4 = NRF_FICR->TEMP.B4;
    NRF_TEMP->B5 = NRF_FICR->TEMP.B5;

    NRF_TEMP->T0 = NRF_FICR->TEMP.T0;
    NRF_TEMP->T1 = NRF_FICR->TEMP.T1;
    NRF_TEMP->T2 = NRF_FICR->TEMP.T2;
    NRF_TEMP->T3 = NRF_FICR->TEMP.T3;
    NRF_TEMP->T4 = NRF_FICR->TEMP.T4;
}

void getTemperature() {
    NRF_TEMP->TASKS_START = 1;

    // Wait until measurement is finished
    while (NRF_TEMP->EVENTS_DATARDY == 0)
        ;
    NRF_TEMP->EVENTS_DATARDY = 0;

    // Read temperature and convert to celcius (rounding down)
    int32_t temp = (int32_t)NRF_TEMP->TEMP;
    temp *= 25;

    temperature = (uint8_t)(temp / 100);

    //printf("temp = %lu.%lu\n", temp / 100, temp % 100);
}

void boardGetOwnMac(uint8_t *mac) {
    mac[0] = tag.macSuffix & 0xFF;
    mac[1] = tag.macSuffix >> 8;
    // the MAC comes from the UICR; an empty UICR has none, then the chip's device ID keeps tags apart
    uint32_t id = NRF_UICR->CUSTOMER[0];
    if (id == 0xFFFFFFFF) id = NRF_FICR->DEVICEID[0];
    mac[2] = id & 0xFF;
    mac[3] = (id >> 8) & 0xFF;
    mac[4] = (id >> 16) & 0xFF;
    mac[5] = id >> 24;
    mac[6] = 0;
    mac[7] = 0;
    uint8_t cksum = 0;
    for (uint8_t c = 0; c < 8; c++) {
        cksum ^= mac[c];
        cksum ^= mac[c] >> 4;
    }
    mac[0] += cksum & 0x0F;
}

// CRV, RREN and CONFIG can't be changed once the watchdog runs, so the first call (wdt30s() at boot) sets the
// timeout for good and every later call only reloads it. The watchdog pauses while the CPU sleeps.
void watchdog_enable(int timeout) {
    if (!NRF_WDT->RUNSTATUS) {
        NRF_WDT->CONFIG = (WDT_CONFIG_HALT_Pause << WDT_CONFIG_HALT_Pos) | (WDT_CONFIG_SLEEP_Pause << WDT_CONFIG_SLEEP_Pos);
        NRF_WDT->CRV = (uint32_t)(((uint64_t)32768 * (uint32_t)timeout) / 1000);
        NRF_WDT->RREN = WDT_RREN_RR0_Msk;
        NRF_WDT->TASKS_START = 1;
    }
    NRF_WDT->RR[0] = WDT_RR_RR_Reload;
}

uint32_t sleepMsEntry = 0;
uint32_t loops = 0;
bool interrupted = false;

void resettimer() {
    // tell the sleep function to net sleep again
    sleepMsEntry = sleepMsEntry - 999999999;
    loops = 0;
    interrupted = true;
}

void sleepwithinterrupts(uint32_t sleepinterval) {
    yield();
    sleepMsEntry = millis();

    // pins that woke us up before can wake us up again
    if (!disablePinInterruptSleep) pinWakeArm();

    // other interrupts wake us up early: sleep again for the time that is left
    uint32_t elapsed;
    while ((elapsed = millis() - sleepMsEntry) < sleepinterval) {
        initRTC0(sleepinterval - elapsed);
        __WFE();
        __SEV();
        __WFE();
    }
}

void sleepForMs(uint32_t ms) {
    // Turn everything off for minimal deep sleep current
    radioRxEnable(0);

    // disable brownout
    NRF_POWER->POFCON &= ~((uint32_t)1);

    NRF_CLOCK->TASKS_HFCLKSTOP = 1U;
    while ((NRF_CLOCK->HFCLKSTAT & 0x10001) == 0x10001)
        ;
    Serial.end();
    yield();
    // sleep, playing the LED pattern if there is one
    ledSleep(ms);
    // we have to restart the serial
    Serial.begin(115200);
}

#define LF_FREQUENCY 32768UL
#define RTC_MAX_SLEEP_MS 500000UL  // the RTC compare value has 24 bits: at most 512 s ahead
static uint32_t wakeUpTicks = 2;

// RTC ticks for ms milliseconds, at least 2: a compare value closer than that to the counter may not trigger
static uint32_t msToRtcTicks(uint32_t ms) {
    uint32_t ticks = (uint32_t)(((uint64_t)ms * LF_FREQUENCY + 500) / 1000);
    return ticks < 2 ? 2 : ticks;
}

// Wakes us up after 'ticks' of the 32768 Hz clock, for waits shorter than a ms too
void initRTC0Ticks(uint32_t ticks) {
    NVIC_DisableIRQ(RTC0_IRQn);
    NVIC_ClearPendingIRQ(RTC0_IRQn);
    NVIC_SetPriority(RTC0_IRQn, 15);
    NRF_RTC0->PRESCALER = 0;
    NRF_RTC0->INTENSET = RTC_EVTENSET_COMPARE0_Enabled << RTC_EVTENSET_COMPARE0_Pos;
    NRF_RTC0->EVTENSET = RTC_INTENSET_COMPARE0_Enabled << RTC_INTENSET_COMPARE0_Pos;
    // a compare value closer than 2 ticks to the counter may not trigger
    wakeUpTicks = ticks < 2 ? 2 : ticks;
    NRF_RTC0->CC[0] = (NRF_RTC0->COUNTER + wakeUpTicks) & RTC_COUNTER_COUNTER_Msk;
    NVIC_EnableIRQ(RTC0_IRQn);
    NRF_RTC0->TASKS_START = 1;
}

void initRTC0(uint32_t ms) {
    // a longer sleep is done in parts: sleepwithinterrupts() starts the timer again for the rest
    initRTC0Ticks(msToRtcTicks(ms > RTC_MAX_SLEEP_MS ? RTC_MAX_SLEEP_MS : ms));
}

#ifdef __cplusplus
extern "C" {
#endif
void RTC0_IRQHandler(void) {
    volatile uint32_t dummy;
    if (NRF_RTC0->EVENTS_COMPARE[0] == 1) {
        NRF_RTC0->EVENTS_COMPARE[0] = 0;
        NRF_RTC0->CC[0] = (NRF_RTC0->COUNTER + wakeUpTicks) & RTC_COUNTER_COUNTER_Msk;
        dummy = NRF_RTC0->EVENTS_COMPARE[0];
        dummy;
        NRF_RTC0->TASKS_STOP = 1;
    }
}
#ifdef __cplusplus
}
#endif

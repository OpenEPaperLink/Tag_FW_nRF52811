#include "led.h"

#include <Arduino.h>

#include "HAL_Newton_M3.h"
#include "wdt.h"
#include "../../../shared/oepl-proto.h"

// LED patterns from the AP: command 4 with a struct ledFlash, see
// https://github.com/OpenEPaperLink/OpenEPaperLink/wiki/Led-control
// Mode 1 plays up to three groups of flashes, each group 'count' flashes of one colour 'speed' x 100 ms apart, followed by
// 'delay' x 100 ms, and repeats that 'repeats' times (255: until a new pattern or a button press). The pattern plays while
// the tag sleeps and carries on across check-ins. The colour comes from the PWM peripheral, so the CPU sleeps during a flash.

#define LED_MIN_SLEEP_MS 2000UL  // shorter sleeps are pauses in a transfer: no flashes there
#define LED_REPEAT_FOREVER 255
#define LED_PWM_TOP 1000  // 16 kHz at 16 MHz

extern bool interrupted;

static struct ledFlash pattern;
static uint8_t group = 0;        // 0..2
static uint8_t flash = 0;        // flash within the group
static uint8_t repeat = 0;       // completed repeats of the three groups
static uint32_t waitMs = 0;      // time to the next flash
static uint16_t pwmSeq[4];       // must be in RAM for EasyDMA

void setled(uint64_t parta, uint32_t partb) {
    uint8_t raw[12];
    for (uint8_t c = 0; c < 8; c++) raw[c] = parta >> (8 * c);
    for (uint8_t c = 0; c < 4; c++) raw[8 + c] = partb >> (8 * c);
    memcpy(&pattern, raw, sizeof(pattern));
    group = 0;
    flash = 0;
    repeat = 0;
    waitMs = 0;
}

static uint8_t groupColor(uint8_t g) {
    return g == 0 ? pattern.color1 : g == 1 ? pattern.color2 : pattern.color3;
}
static uint8_t groupCount(uint8_t g) {
    return g == 0 ? pattern.flashCount1 : g == 1 ? pattern.flashCount2 : pattern.flashCount3;
}
static uint32_t groupSpeedMs(uint8_t g) {
    return 100UL * (g == 0 ? pattern.flashSpeed1 : g == 1 ? pattern.flashSpeed2 : pattern.flashSpeed3);
}
static uint32_t groupDelayMs(uint8_t g) {
    return 100UL * (g == 0 ? pattern.delay1 : g == 1 ? pattern.delay2 : pattern.delay3);
}

// on time of one flash in µs: 0 is 0.5 ms, 15 has no off time
static uint32_t flashOnUs(uint8_t g) {
    if (pattern.flashDuration == 0) return 500;
    if (pattern.flashDuration == 15) return groupSpeedMs(g) * 1000;
    return pattern.flashDuration * 1000UL;
}

// sleep with the CPU halted until the RTC wakes us up; other events wake us up early, then sleep the rest
static void sleepUs(uint32_t us) {
    uint32_t ticks = (uint32_t)(((uint64_t)us * 32768 + 500000) / 1000000);
    uint32_t start = NRF_RTC0->COUNTER;
    initRTC0Ticks(ticks);
    while (((NRF_RTC0->COUNTER - start) & RTC_COUNTER_COUNTER_Msk) < ticks) {
        __WFE();
        __SEV();
        __WFE();
    }
}

static void ledFlashOnce(uint8_t color, uint32_t onUs) {
    // RGB332: rrrgggbb. Brightness as duty cycle, the polarity bit makes the duty the time the LED is on
    uint16_t polarity = tag.ledInverted ? 0x8000 : 0;
    pwmSeq[0] = ((color >> 5) & 0x07) * LED_PWM_TOP / 7 | polarity;
    pwmSeq[1] = ((color >> 2) & 0x07) * LED_PWM_TOP / 7 | polarity;
    pwmSeq[2] = (color & 0x03) * LED_PWM_TOP / 3 | polarity;
    pwmSeq[3] = polarity;

    NRF_PWM0->PSEL.OUT[0] = digitalPinToPin(LED_RED);
    NRF_PWM0->PSEL.OUT[1] = digitalPinToPin(LED_GREEN);
    NRF_PWM0->PSEL.OUT[2] = digitalPinToPin(LED_BLUE);
    NRF_PWM0->PSEL.OUT[3] = PWM_PSEL_OUT_CONNECT_Disconnected << PWM_PSEL_OUT_CONNECT_Pos;
    NRF_PWM0->MODE = PWM_MODE_UPDOWN_Up;
    NRF_PWM0->PRESCALER = PWM_PRESCALER_PRESCALER_DIV_1;
    NRF_PWM0->COUNTERTOP = LED_PWM_TOP;
    NRF_PWM0->LOOP = 0;
    NRF_PWM0->DECODER = (PWM_DECODER_LOAD_Individual << PWM_DECODER_LOAD_Pos) | (PWM_DECODER_MODE_RefreshCount << PWM_DECODER_MODE_Pos);
    NRF_PWM0->SEQ[0].PTR = (uint32_t)pwmSeq;
    NRF_PWM0->SEQ[0].CNT = 4;
    NRF_PWM0->SEQ[0].REFRESH = 0;
    NRF_PWM0->SEQ[0].ENDDELAY = 0;
    NRF_PWM0->ENABLE = PWM_ENABLE_ENABLE_Enabled;
    // after the sequence the PWM keeps repeating its last value, until it is stopped
    NRF_PWM0->TASKS_SEQSTART[0] = 1;
    sleepUs(onUs);
    NRF_PWM0->EVENTS_STOPPED = 0;
    NRF_PWM0->TASKS_STOP = 1;
    while (!NRF_PWM0->EVENTS_STOPPED);
    NRF_PWM0->EVENTS_STOPPED = 0;
    // the pins go back to their GPIO setting: LED off
    NRF_PWM0->ENABLE = PWM_ENABLE_ENABLE_Disabled;
}

// Moves to the next flash. Returns false when the pattern is done.
static bool ledNext() {
    // groups without flashes are skipped; at most one round through all three
    for (uint8_t tries = 0; tries < 4; tries++) {
        if (flash < groupCount(group)) return true;
        flash = 0;
        if (++group == 3) {
            group = 0;
            if (pattern.repeats != LED_REPEAT_FOREVER && ++repeat > pattern.repeats) return false;
        }
    }
    return false;  // no flashes in any group
}

static void ledStop() {
    pattern.mode = 0;
}

void ledSleep(uint32_t ms) {
    if (pattern.mode != 1 || ms < LED_MIN_SLEEP_MS) {
        sleepwithinterrupts(ms);
        return;
    }
    interrupted = false;
    while (ms) {
        watchdog_enable(ms + 1000);
        if (waitMs >= ms) {
            sleepwithinterrupts(ms);
            if (interrupted) ledStop();  // a button press ends the pattern
            waitMs -= ms;
            return;
        }
        if (waitMs) sleepwithinterrupts(waitMs);
        if (interrupted) {
            ledStop();
            return;
        }
        ms -= waitMs;
        if (!ledNext()) {
            ledStop();
            sleepwithinterrupts(ms);
            return;
        }
        uint32_t onUs = flashOnUs(group);
        ledFlashOnce(groupColor(group), onUs);
        uint32_t onMs = onUs / 1000;
        ms = ms > onMs ? ms - onMs : 0;
        // the next flash follows 'speed' after this one, the last one of a group also waits 'delay'
        uint32_t speed = groupSpeedMs(group);
        waitMs = speed > onMs ? speed - onMs : 0;
        if (++flash == groupCount(group)) waitMs += groupDelayMs(group);
    }
}

#include "pinwake.h"

#include <Arduino.h>

#define PINWAKE_MAX 12

struct pinWake {
    uint8_t pin;  // nRF pin number
    bool activeHigh;
    bool waitRelease;  // armed for the inactive level, to be armed again when let go
    void (*handler)();
};

static pinWake pins[PINWAKE_MAX];
static uint8_t pinCount = 0;

static void setSense(uint8_t pin, uint32_t sense) {
    NRF_GPIO->PIN_CNF[pin] = (NRF_GPIO->PIN_CNF[pin] & ~GPIO_PIN_CNF_SENSE_Msk) | (sense << GPIO_PIN_CNF_SENSE_Pos);
}

static void pinWakeInit() {
    static bool initialized = false;
    if (initialized) return;
    initialized = true;
    // latched detect: the LATCH register tells which pin fired
    NRF_GPIO->DETECTMODE = GPIO_DETECTMODE_DETECTMODE_LDETECT;
    NRF_GPIO->LATCH = 0xFFFFFFFF;
    NRF_GPIOTE->EVENTS_PORT = 0;
    NRF_GPIOTE->INTENSET = GPIOTE_INTENSET_PORT_Msk;
    NVIC_SetPriority(GPIOTE_IRQn, 2);
    NVIC_ClearPendingIRQ(GPIOTE_IRQn);
    NVIC_EnableIRQ(GPIOTE_IRQn);
}

void pinWakeAttach(uint32_t pin, void (*handler)(), bool activeHigh) {
    pinWakeInit();
    uint8_t nrfPin = digitalPinToPin(pin);
    uint8_t c;
    for (c = 0; c < pinCount; c++) {
        if (pins[c].pin == nrfPin) break;
    }
    if (c == pinCount) {
        if (pinCount == PINWAKE_MAX) return;
        pinCount++;
    }
    pins[c].pin = nrfPin;
    pins[c].activeHigh = activeHigh;
    pins[c].waitRelease = false;
    pins[c].handler = handler;
    pinWakeArm();
}

void pinWakeDetach(uint32_t pin) {
    uint8_t nrfPin = digitalPinToPin(pin);
    for (uint8_t c = 0; c < pinCount; c++) {
        if (pins[c].pin != nrfPin) continue;
        NVIC_DisableIRQ(GPIOTE_IRQn);
        setSense(nrfPin, GPIO_PIN_CNF_SENSE_Disabled);
        NRF_GPIO->LATCH = 1UL << nrfPin;
        pins[c] = pins[--pinCount];
        NVIC_EnableIRQ(GPIOTE_IRQn);
        return;
    }
}

void pinWakeArm() {
    uint32_t in = NRF_GPIO->IN;
    for (uint8_t c = 0; c < pinCount; c++) {
        uint8_t pin = pins[c].pin;
        bool high = (in >> pin) & 1;
        // a pin still at its active level (a button held down) first waits for its release, which arms it again
        pins[c].waitRelease = (high == pins[c].activeHigh);
        bool senseHigh = pins[c].waitRelease ? !pins[c].activeHigh : pins[c].activeHigh;
        setSense(pin, senseHigh ? GPIO_PIN_CNF_SENSE_High : GPIO_PIN_CNF_SENSE_Low);
    }
}

extern "C" void GPIOTE_IRQHandler(void) {
    if (!NRF_GPIOTE->EVENTS_PORT) return;
    NRF_GPIOTE->EVENTS_PORT = 0;
    uint32_t latch = NRF_GPIO->LATCH;
    for (uint8_t c = 0; c < pinCount; c++) {
        uint8_t pin = pins[c].pin;
        if (!(latch & (1UL << pin))) continue;
        if (pins[c].waitRelease) {
            // let go: armed again, without waking anything up
            pins[c].waitRelease = false;
            setSense(pin, pins[c].activeHigh ? GPIO_PIN_CNF_SENSE_High : GPIO_PIN_CNF_SENSE_Low);
            continue;
        }
        // no more detects until pinWakeArm(), so a pin held at its active level doesn't keep firing
        setSense(pin, GPIO_PIN_CNF_SENSE_Disabled);
        if (pins[c].handler) pins[c].handler();
    }
    NRF_GPIO->LATCH = latch;
    (void)NRF_GPIOTE->EVENTS_PORT;
}

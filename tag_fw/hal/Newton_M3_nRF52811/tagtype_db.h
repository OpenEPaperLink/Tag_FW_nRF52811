#pragma once
#include <Arduino.h>


bool isValidNRF52811Pin(uint8_t pin);
uint8_t getUICRByte(uint8_t offset);
void identifyTagInfo();
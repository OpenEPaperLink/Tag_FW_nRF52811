#pragma once
#include <stdint.h>


#define EPD_BUSY_UC 0
#define EPD_BUSY_SSD 1
#define EPD_RESET_MAX 260

void spi_write(uint8_t data);
void epd_cmd(uint8_t data);
void epd_data(uint8_t data);
void waitBusy();
bool epdWaitUntilNotBusy(uint8_t type, uint32_t ms);
void epdBusyWaitFalling(uint32_t timeout);
void epdBusyWaitRising(uint32_t timeout);

uint8_t spi3_read();
uint8_t spi_trans(uint8_t data);
void epdWrite(uint8_t reg, uint8_t len, ...);

bool epdReset(uint8_t type);
void epdConfigGPIO(bool setup);
extern bool epdGPIOActive;

void epdSPIAsyncWrite(uint8_t* data, uint16_t len);
void epdSPIWait();
void epdSPIReadBlock(uint8_t* data, uint16_t len);
void epdHardSPI(bool enable);

#define epdSelect()                \
    do                             \
    {                              \
        digitalWrite(EPD_CS, LOW); \
    } while (0)

#define epdDeselect()               \
    do                              \
    {                               \
        digitalWrite(EPD_CS, HIGH); \
    } while (0)

#define markCommand()              \
    do {                           \
        digitalWrite(EPD_DC, LOW); \
    } while (0)

#define markData()                  \
    do {                            \
        digitalWrite(EPD_DC, HIGH); \
    } while (0)

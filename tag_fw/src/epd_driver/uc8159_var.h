#ifndef _EPD_UC8159_VAR_H_
#define _EPD_UC8159_VAR_H_

class uc8159_var : public epdInterface {
   public:
    void epdSetup();
    void epdEnterSleep();
    void draw();
    void drawNoWait();
    void epdWaitRdy();
    void epdWriteDisplayData();
    void selectLUT(uint8_t lut);

   protected:
    void epdEepromRead(uint16_t addr, uint8_t *data, uint16_t len);
    void readPanelTemperature();
    void interleaveColorToBuffer(uint8_t *dst, uint8_t b, uint8_t r, uint8_t y);
};
#endif
#ifndef _EPD_BWRY_VAR1_H_
#define _EPD_BWRY_VAR1_H_

class epdvarbwry : public epdInterface {
   public:
     void epdSetup() ;
     void epdEnterSleep() ;
     void draw();
     void drawNoWait();
     void epdWaitRdy();
     void epdWriteDisplayData();
     void selectLUT(uint8_t lut);
    protected:
     void calculatePixels(uint8_t* dst, uint8_t* src, uint8_t color);
};

#endif
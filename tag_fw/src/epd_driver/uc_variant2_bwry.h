#ifndef _EPD_BWRY_VAR2_H_
#define _EPD_BWRY_VAR2_H_

class epdvar2bwry : public epdInterface {
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
#ifndef _EPD_NONE_H_
#define _EPD_NONE_H_

// For a tag with an empty or unknown UICR: no display is driven. What was set up to draw is dropped.
class epdnone : public epdInterface {
   public:
    void epdSetup() {}
    void epdEnterSleep() {}
    void draw();
    void drawNoWait();
    void epdWaitRdy() {}
    void selectLUT(uint8_t lut) {}

   protected:
    void epdWriteDisplayData() {}
};
#endif

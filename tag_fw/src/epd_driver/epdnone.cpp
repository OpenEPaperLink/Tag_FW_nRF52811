#include <Arduino.h>

#include "hal.h"
#include "drawing.h"
#include "epdnone.h"

void epdnone::draw() {
    drawItem::flushDrawItems();
}

void epdnone::drawNoWait() {
    drawItem::flushDrawItems();
}

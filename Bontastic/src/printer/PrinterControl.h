#pragma once

#include <stdint.h>

struct PrinterSettings
{
    uint8_t heatDots;
    uint8_t heatTime;
    uint8_t heatInterval;
    uint8_t density;
    uint8_t breakTime;
    uint8_t lineHeight;
    uint8_t font;
    uint8_t size;
    uint8_t justify;
    uint8_t decorations;
    uint8_t feedRows;
    uint8_t charset;
    uint8_t codePage;
    char meshName[32];
    char meshPin[16];
};

void setupPrinterControl();
void printerControlLoop();
const PrinterSettings &getPrinterSettings();

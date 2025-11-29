#pragma once

#include <stdint.h>

struct PrinterSettings
{
    uint16_t temperature;
    uint16_t speed;
    uint8_t fan;
};

void setupPrinterControl();
void printerControlLoop();
const PrinterSettings &getPrinterSettings();

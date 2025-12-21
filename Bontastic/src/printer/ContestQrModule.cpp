#include "ContestQrModule.h"

#include <Arduino.h>
#include <string>

#include "Bontastic_Thermal.h"
#include "PrinterControl.h"

extern Bontastic_Thermal printer;

static uint32_t intervalMs = 5UL * 60UL * 1000UL;
static std::string content = "tALaJ2r35ygij1KCB3PvxSCl609Tajv4XYhMEscw7JY=";
static uint32_t nextAt;

void contestQrSetIntervalMinutes(uint32_t minutes)
{
    intervalMs = minutes * 60UL * 1000UL;
}

void contestQrSetContent(const char *value)
{
    content = value ? value : "";
}

void contestQrSetup()
{
    nextAt = 0;
}

void contestQrLoop()
{
    if (!intervalMs || content.empty())
    {
        return;
    }

    uint32_t now = millis();
    if (static_cast<int32_t>(now - nextAt) < 0)
    {
        return;
    }

    printer.justify('C');
    printer.qrSelectModel(2);
    printer.qrSetModuleSize(4);
    printer.qrSetErrorCorrection(48);
    printer.qrStoreData(reinterpret_cast<const uint8_t *>(content.data()), content.size());
    printer.qrPrint();
    printer.println(F("Hint for the badge"));
    printer.feed(2);
    applyPrinterSettings();

    nextAt = now + intervalMs;
}

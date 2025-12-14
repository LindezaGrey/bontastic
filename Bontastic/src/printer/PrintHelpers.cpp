#include "PrintHelpers.h"
#include "Bontastic_Thermal.h"
#include "PrinterControl.h"
#include <time.h>

Bontastic_Thermal printer(&Serial2);

void updatePrinterPins(int rx, int tx)
{
    Serial2.end();
    Serial2.begin(9600, SERIAL_8N1, rx, tx);
    printer.begin();
    applyPrinterSettings();
}

void printerSetup()
{
    const PrinterSettings &settings = getPrinterSettings();
    updatePrinterPins(settings.printerRxPin, settings.printerTxPin);
    printer.print(F("Bontastic Printer Ready"));
    printer.feed(2);
}

std::string utf8ToIso88591(const std::string &utf8)
{
    std::string iso;
    iso.reserve(utf8.size());
    for (size_t i = 0; i < utf8.size(); ++i)
    {
        uint8_t c = utf8[i];
        if (c < 0x80)
        {
            iso += (char)c;
        }
        else if ((c & 0xE0) == 0xC0) // 2-byte sequence
        {
            if (i + 1 < utf8.size())
            {
                uint8_t c2 = utf8[i + 1];
                if ((c2 & 0xC0) == 0x80)
                {
                    uint16_t cp = ((c & 0x1F) << 6) | (c2 & 0x3F);
                    if (cp <= 0xFF)
                    {
                        iso += (char)cp;
                    }
                    else
                    {
                        iso += '?'; // Character not in ISO-8859-1
                    }
                    i++; // Skip next byte
                }
            }
        }
        else
        {
            // 3+ byte sequences or invalid UTF-8, skip or replace
            // For now, just ignore or maybe add '?'
            // If we just skip, we might miss characters.
            // But ISO-8859-1 only supports up to U+00FF.
            // So 3-byte sequences (U+0800+) are definitely out.
        }
    }
    return iso;
}

void printTextMessage(const uint8_t *data, size_t size, const char *sender, uint32_t timestamp)
{
    Serial.write(data, size);
    Serial.println();

    // Format time
    time_t t = (time_t)timestamp;
    struct tm *tm = localtime(&t);
    char timeBuf[32];
    strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", tm);

    printer.println(F("----------------"));
    printer.print(F("From: "));
    printer.println(sender);
    printer.print(F("Time: "));
    printer.println(timeBuf);

    std::string utf8((const char *)data, size);
    std::string iso = utf8ToIso88591(utf8);
    printer.println(iso.c_str());
    printer.println(F("----------------"));
    printer.feed(2);
}

void printPosition(double lat, double lon, int32_t alt)
{
    Serial.print("POS lat=");
    Serial.print(lat, 7);
    Serial.print(" lon=");
    Serial.print(lon, 7);
    Serial.print(" alt=");
    Serial.println(alt);

    // printer.print("POS lat=");
    // printer.print(lat, 7);
    // printer.print(" lon=");
    // printer.print(lon, 7);
    // printer.print(" alt=");
    // printer.println(alt);
}

void printNodeInfo(uint32_t num, const char *name)
{
    Serial.print("NODE ");
    Serial.print(num);
    Serial.print(" ");
    Serial.println(name);

    printer.print("NODE ");
    printer.print(num);
    printer.print(" ");
    printer.println(name);
}

void printBinaryPayload(const uint8_t *data, size_t size)
{
    for (size_t i = 0; i < size; ++i)
    {
        uint8_t b = data[i];
        if (b < 16)
        {
            Serial.print("0");
        }
        Serial.print(b, HEX);
    }
    Serial.println();
}

void printInfo(const char *label, const char *value)
{
    Serial.print(label);
    Serial.print(": ");
    Serial.println(value);

    printer.print(label);
    printer.print(": ");
    printer.println(value);
}

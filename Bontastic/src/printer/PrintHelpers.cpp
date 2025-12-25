#include "PrintHelpers.h"
#include "Bontastic_Thermal.h"
#include "PrinterControl.h"
#include "assets/bontastic.h"
#include "src/printer/assets/congresslogo.h"
#include "MeshtasticBLELogger.h"
#include <time.h>
#include <vector>
#include <sstream>

Bontastic_Thermal printer(&Serial2);

static uint8_t reverseBits(uint8_t b)
{
    b = (uint8_t)((b & 0xF0) >> 4) | (uint8_t)((b & 0x0F) << 4);
    b = (uint8_t)((b & 0xCC) >> 2) | (uint8_t)((b & 0x33) << 2);
    b = (uint8_t)((b & 0xAA) >> 1) | (uint8_t)((b & 0x55) << 1);
    return b;
}

void gsV0WithUpsideDown(uint16_t widthBytes, uint16_t height, const uint8_t *data, size_t len, bool upsideDown)
{
    if (!data || !len)
    {
        return;
    }
    size_t expected = (size_t)widthBytes * (size_t)height;
    if (!upsideDown || len != expected)
    {
        printer.gsV0(0, widthBytes, height, data, len);
        return;
    }

    std::string flipped;
    flipped.resize(len);
    const size_t rowBytes = widthBytes;
    for (uint16_t y = 0; y < height; y++)
    {
        size_t srcRow = (size_t)(height - 1 - y) * rowBytes;
        size_t dstRow = (size_t)y * rowBytes;
        for (uint16_t xb = 0; xb < widthBytes; xb++)
        {
            uint8_t v = data[srcRow + (rowBytes - 1 - xb)];
            flipped[dstRow + xb] = (char)reverseBits(v);
        }
    }

    printer.gsV0(0, widthBytes, height, reinterpret_cast<const uint8_t *>(flipped.data()), flipped.size());
}

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
}

void printStartupLogo()
{
    bleLog("Startup bitmap print");
    // printer.gsV0(0, bontastic_width / 8, bontastic_height, bontastic_data, sizeof(bontastic_data));
    bool upsideDown = (getPrinterSettings().decorations & 0x10) != 0;
    gsV0WithUpsideDown(congresslogo_width / 8, congresslogo_height, congresslogo_data, sizeof(congresslogo_data), upsideDown);
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

static int getCharsPerLine()
{
    const PrinterSettings &settings = getPrinterSettings();
    // Standard 58mm Thermal Printer (384 dots width)
    // Font A: 12x24 dots -> 384 / 12 = 32 characters
    // Font B: 9x17 dots  -> 384 / 9  = 42.66 -> 42 characters
    int baseChars = (settings.font != 0) ? 42 : 32;

    // Check for double width (Size 'L' or doubleWidth decoration)
    // Size: 0=S, 1=M, 2=L (L includes double width)
    // Decorations: 0x08 is double width
    bool isDoubleWidth = (settings.size == 2) || (settings.decorations & 0x08);

    if (isDoubleWidth)
    {
        return baseChars / 2;
    }
    return baseChars;
}

static std::vector<std::string> wrapText(const std::string &text, size_t maxWidth)
{
    std::vector<std::string> lines;
    size_t start = 0;
    size_t len = text.length();

    while (start < len)
    {
        size_t end = text.find('\n', start);
        if (end == std::string::npos)
            end = len;

        std::string line = text.substr(start, end - start);
        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        if (line.empty())
        {
            lines.push_back("");
            start = end + 1;
            continue;
        }

        size_t lineStart = 0;
        while (lineStart < line.length())
        {
            if (line.length() - lineStart <= maxWidth)
            {
                lines.push_back(line.substr(lineStart));
                break;
            }

            size_t breakPoint = line.rfind(' ', lineStart + maxWidth);
            if (breakPoint == std::string::npos || breakPoint < lineStart)
            {
                breakPoint = lineStart + maxWidth;
            }

            lines.push_back(line.substr(lineStart, breakPoint - lineStart));
            lineStart = breakPoint;
            if (lineStart < line.length() && line[lineStart] == ' ')
            {
                lineStart++;
            }
        }

        start = end + 1;
    }
    return lines;
}

void printStyledText(const std::string &text)
{
    bool upsideDown = (getPrinterSettings().decorations & 0x10) != 0;
    if (upsideDown)
    {
        std::vector<std::string> lines = wrapText(text, getCharsPerLine());
        for (auto it = lines.rbegin(); it != lines.rend(); ++it)
        {
            printer.println(it->c_str());
        }
    }
    else
    {
        printer.println(text.c_str());
    }
}

void printTextMessage(const uint8_t *data, size_t size, const char *sender, uint32_t timestamp)
{
    bleLogf("TEXT %u bytes", (unsigned)size);

    // Format time
    time_t t = (time_t)timestamp;
    struct tm *tm = localtime(&t);
    char timeBuf[32];
    strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", tm);

    printer.println(F("----------------"));
    printer.print(F("From: "));
    bool inverse = getPrinterSettings().decorations & 0x02;
    if (inverse)
    {
        printer.inverseOff();
    }
    else
    {
        printer.inverseOn();
    }
    printer.println(sender);
    applyPrinterSettings();
    printer.print(F("Time: "));
    printer.println(timeBuf);

    std::string utf8((const char *)data, size);
    std::string iso = utf8ToIso88591(utf8);

    printStyledText(iso);
    printer.feed(2);
}

void printPosition(double lat, double lon, int32_t alt)
{
    bleLogf("POS lat=%.7f lon=%.7f alt=%ld", lat, lon, (long)alt);

    // printer.print("POS lat=");
    // printer.print(lat, 7);
    // printer.print(" lon=");
    // printer.print(lon, 7);
    // printer.print(" alt=");
    // printer.println(alt);
}

void printNodeInfo(uint32_t num, const char *name)
{
    bleLogf("NODE %lu %s", (unsigned long)num, name ? name : "");

    printer.print("NODE ");
    printer.print(num);
    printer.print(" ");
    printer.println(name);
}

void printBinaryPayload(const uint8_t *data, size_t size)
{
    bleLogf("BIN %u bytes", (unsigned)size);
}

void printInfo(const char *label, const char *value)
{
    bleLogf("%s: %s", label ? label : "", value ? value : "");

    printer.print(label);
    printer.print(": ");
    printer.println(value);
}

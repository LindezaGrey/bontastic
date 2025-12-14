#pragma once

#include <Arduino.h>

class Bontastic_Thermal : public Print
{
public:
    explicit Bontastic_Thermal(Stream *s = &Serial, uint8_t dtr = 255);

    size_t write(uint8_t c) override;

    void begin();
    void begin(uint16_t version);

    void reset();
    void setDefault();

    void setHeatConfig(uint8_t dots = 11, uint8_t time = 120, uint8_t interval = 40);
    void setPrintDensity(uint8_t density = 10, uint8_t breakTime = 2);

    void feed(uint8_t n = 1);
    void feedRows(uint8_t n);
    void tab();
    void setTabStops(const uint8_t *stops, size_t count);
    void setAbsolutePosition(uint16_t pos);
    void setLeftMargin(uint16_t margin);

    void defaultLineSpacing();
    void setLineHeight(int n);

    void justify(char value);

    void setStyle(uint8_t n);
    void setScale(uint8_t widthMul, uint8_t heightMul);

    void boldOn();
    void boldOff();

    void doubleStrikeOn();
    void doubleStrikeOff();

    void underlineOn(uint8_t weight = 1);
    void underlineOff();

    void setCharSpacing(uint8_t n);

    void upsideDownOn();
    void upsideDownOff();

    void rotate90(uint8_t n);

    void inverseOn();
    void inverseOff();

    void doubleWidthOn();
    void doubleWidthOff();

    void setCharset(uint8_t n);
    void setCodePage(uint8_t n);

    void enablePanelButtons(bool enabled);
    void testPage();
    void sleepSettings(uint16_t seconds);

    void queryBasicStatus(uint8_t n);
    void requestSensorState(uint8_t n);
    void setAutoStatusBack(uint8_t n);

    void setBarcodeHeight(uint8_t n);
    void setBarcodeModuleWidth(uint8_t n);
    void setBarcodeHRI(uint8_t n);
    void setBarcodeLeftMargin(uint8_t n);
    void printBarcode(uint8_t m, const uint8_t *data, size_t len, bool includeTerminator);

    void escStar(uint8_t m, uint16_t n, const uint8_t *data, size_t len);
    void gsStar(uint8_t x, uint8_t y, const uint8_t *data, size_t len);
    void gsSlash(uint8_t m);
    void gsV0(uint8_t m, uint16_t x, uint16_t y, const uint8_t *data, size_t len);

    void storeNvBitmaps(uint8_t n, const uint8_t *data, size_t len);
    void printNvBitmap(uint8_t n, uint8_t m);

    void userDefinedCharsEnabled(bool enabled);
    void defineUserDefinedChars(uint8_t y, uint8_t c1, uint8_t c2, const uint8_t *data, size_t len);
    void deleteUserDefinedChar(uint8_t n);

    void qrSelectModel(uint8_t model);
    void qrSetModuleSize(uint8_t n);
    void qrSetErrorCorrection(uint8_t n);
    void qrStoreData(const uint8_t *data, size_t len);
    void qrPrint();
    void qrSelectDataType(uint8_t n);

    void chineseModeOn();
    void chineseModeOff();
    void chineseFontMode(uint8_t n);

    void setFont(char font);
    void setSize(char value);

    void strikeOn();
    void strikeOff();

private:
    Stream *_stream;
    uint8_t _style;

    void writeBytes(uint8_t a);
    void writeBytes(uint8_t a, uint8_t b);
    void writeBytes(uint8_t a, uint8_t b, uint8_t c);
    void writeBytes(uint8_t a, uint8_t b, uint8_t c, uint8_t d);
    void writeN(const uint8_t *data, size_t len);
    void gsK(uint8_t cn, uint8_t fn, const uint8_t *data, size_t len);
    void updateStyle();
};

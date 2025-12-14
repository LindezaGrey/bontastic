#include "Bontastic_Thermal.h"

static constexpr uint8_t ASCII_HT = 0x09;
static constexpr uint8_t ASCII_LF = 0x0A;
static constexpr uint8_t ASCII_SO = 0x0E;
static constexpr uint8_t ASCII_DC4 = 0x14;
static constexpr uint8_t ASCII_DC2 = 0x12;
static constexpr uint8_t ASCII_ESC = 0x1B;
static constexpr uint8_t ASCII_FS = 0x1C;
static constexpr uint8_t ASCII_GS = 0x1D;

static constexpr uint8_t STYLE_FONT_B = (1 << 0);
static constexpr uint8_t STYLE_DOUBLE_HEIGHT = (1 << 4);
static constexpr uint8_t STYLE_DOUBLE_WIDTH = (1 << 5);

Bontastic_Thermal::Bontastic_Thermal(Stream *s, uint8_t) : _stream(s), _style(0) {}

size_t Bontastic_Thermal::write(uint8_t c)
{
    if (!_stream)
    {
        return 0;
    }
    if (c == '\r')
    {
        return 1;
    }
    _stream->write(c);
    return 1;
}

void Bontastic_Thermal::begin()
{
    reset();
    setHeatConfig();
}

void Bontastic_Thermal::begin(uint16_t)
{
    begin();
}

void Bontastic_Thermal::reset()
{
    writeBytes(ASCII_ESC, '@');
    _style = 0;
    updateStyle();
    inverseOff();
    upsideDownOff();
    underlineOff();
    doubleStrikeOff();
    setLineHeight(30);
}

void Bontastic_Thermal::setDefault()
{
    justify('L');
    inverseOff();
    upsideDownOff();
    boldOff();
    underlineOff();
    doubleStrikeOff();
    doubleWidthOff();
    setLineHeight(30);
    setFont('A');
    setCharset(0);
    setCodePage(0);
}

void Bontastic_Thermal::setHeatConfig(uint8_t dots, uint8_t time, uint8_t interval)
{
    writeBytes(ASCII_ESC, '7');
    writeBytes(dots, time, interval);
}

void Bontastic_Thermal::setPrintDensity(uint8_t density, uint8_t breakTime)
{
    writeBytes(ASCII_DC2, '#', (density << 5) | breakTime);
}

void Bontastic_Thermal::feed(uint8_t n)
{
    writeBytes(ASCII_ESC, 'd', n);
}

void Bontastic_Thermal::feedRows(uint8_t n)
{
    writeBytes(ASCII_ESC, 'J', n);
}

void Bontastic_Thermal::tab() { writeBytes(ASCII_HT); }

void Bontastic_Thermal::setTabStops(const uint8_t *stops, size_t count)
{
    writeBytes(ASCII_ESC, 'D');
    writeN(stops, count);
    writeBytes(0x00);
}

void Bontastic_Thermal::setAbsolutePosition(uint16_t pos)
{
    writeBytes(ASCII_ESC, '$', (uint8_t)(pos & 0xFF), (uint8_t)(pos >> 8));
}

void Bontastic_Thermal::setLeftMargin(uint16_t margin)
{
    writeBytes(ASCII_GS, 'L', (uint8_t)(margin & 0xFF), (uint8_t)(margin >> 8));
}

void Bontastic_Thermal::defaultLineSpacing() { writeBytes(ASCII_ESC, '2'); }

void Bontastic_Thermal::setLineHeight(int n)
{
    if (n < 0)
        n = 0;
    if (n > 255)
        n = 255;
    writeBytes(ASCII_ESC, '3', (uint8_t)n);
}

void Bontastic_Thermal::justify(char value)
{
    uint8_t pos = 0;
    switch (toupper(value))
    {
    case 'C':
        pos = 1;
        break;
    case 'R':
        pos = 2;
        break;
    default:
        pos = 0;
        break;
    }
    writeBytes(ASCII_ESC, 'a', pos);
}

void Bontastic_Thermal::setStyle(uint8_t n)
{
    _style = n;
    updateStyle();
}

void Bontastic_Thermal::setScale(uint8_t widthMul, uint8_t heightMul)
{
    if (widthMul < 1)
        widthMul = 1;
    if (widthMul > 8)
        widthMul = 8;
    if (heightMul < 1)
        heightMul = 1;
    if (heightMul > 8)
        heightMul = 8;
    uint8_t n = (uint8_t)(((widthMul - 1) << 4) | (heightMul - 1));
    writeBytes(ASCII_GS, '!', n);
}

void Bontastic_Thermal::boldOn() { writeBytes(ASCII_ESC, 'E', 1); }
void Bontastic_Thermal::boldOff() { writeBytes(ASCII_ESC, 'E', 0); }

void Bontastic_Thermal::doubleStrikeOn() { writeBytes(ASCII_ESC, 'G', 1); }
void Bontastic_Thermal::doubleStrikeOff() { writeBytes(ASCII_ESC, 'G', 0); }

void Bontastic_Thermal::underlineOn(uint8_t weight)
{
    if (weight > 2)
        weight = 2;
    writeBytes(ASCII_ESC, '-', weight);
}

void Bontastic_Thermal::underlineOff() { writeBytes(ASCII_ESC, '-', 0); }

void Bontastic_Thermal::setCharSpacing(uint8_t n) { writeBytes(ASCII_ESC, ' ', n); }

void Bontastic_Thermal::upsideDownOn() { writeBytes(ASCII_ESC, '{', 1); }
void Bontastic_Thermal::upsideDownOff() { writeBytes(ASCII_ESC, '{', 0); }

void Bontastic_Thermal::rotate90(uint8_t n) { writeBytes(ASCII_ESC, 'V', n); }

void Bontastic_Thermal::inverseOn() { writeBytes(ASCII_GS, 'B', 1); }
void Bontastic_Thermal::inverseOff() { writeBytes(ASCII_GS, 'B', 0); }

void Bontastic_Thermal::doubleWidthOn()
{
    _style |= STYLE_DOUBLE_WIDTH;
    updateStyle();
    writeBytes(ASCII_SO);
}

void Bontastic_Thermal::doubleWidthOff()
{
    _style &= (uint8_t)~STYLE_DOUBLE_WIDTH;
    updateStyle();
    writeBytes(ASCII_DC4);
}

void Bontastic_Thermal::setCharset(uint8_t n) { writeBytes(ASCII_ESC, 'R', n > 15 ? 15 : n); }
void Bontastic_Thermal::setCodePage(uint8_t n) { writeBytes(ASCII_ESC, 't', n > 47 ? 47 : n); }

void Bontastic_Thermal::enablePanelButtons(bool enabled) { writeBytes(ASCII_ESC, 'c', '5', enabled ? 1 : 0); }
void Bontastic_Thermal::testPage() { writeBytes(ASCII_DC2, 'T'); }

void Bontastic_Thermal::sleepSettings(uint16_t seconds)
{
    writeBytes(ASCII_ESC, '8', (uint8_t)(seconds & 0xFF), (uint8_t)(seconds >> 8));
}

void Bontastic_Thermal::queryBasicStatus(uint8_t n) { writeBytes(ASCII_ESC, 'v', n); }
void Bontastic_Thermal::requestSensorState(uint8_t n) { writeBytes(ASCII_GS, 'r', n); }
void Bontastic_Thermal::setAutoStatusBack(uint8_t n) { writeBytes(ASCII_GS, 'a', n); }

void Bontastic_Thermal::setBarcodeHeight(uint8_t n) { writeBytes(ASCII_GS, 'h', n); }
void Bontastic_Thermal::setBarcodeModuleWidth(uint8_t n) { writeBytes(ASCII_GS, 'w', n); }
void Bontastic_Thermal::setBarcodeHRI(uint8_t n) { writeBytes(ASCII_GS, 'H', n); }
void Bontastic_Thermal::setBarcodeLeftMargin(uint8_t n) { writeBytes(ASCII_GS, 'x', n); }

void Bontastic_Thermal::printBarcode(uint8_t m, const uint8_t *data, size_t len, bool includeTerminator)
{
    writeBytes(ASCII_GS, 'k', m);
    writeN(data, len);
    if (includeTerminator)
    {
        writeBytes(0x00);
    }
}

void Bontastic_Thermal::escStar(uint8_t m, uint16_t n, const uint8_t *data, size_t len)
{
    writeBytes(ASCII_ESC, '*', m, (uint8_t)(n & 0xFF));
    writeBytes((uint8_t)(n >> 8));
    writeN(data, len);
}

void Bontastic_Thermal::gsStar(uint8_t x, uint8_t y, const uint8_t *data, size_t len)
{
    writeBytes(ASCII_GS, '*', x, y);
    writeN(data, len);
}

void Bontastic_Thermal::gsSlash(uint8_t m) { writeBytes(ASCII_GS, '/', m); }

void Bontastic_Thermal::gsV0(uint8_t m, uint16_t x, uint16_t y, const uint8_t *data, size_t len)
{
    writeBytes(ASCII_GS, 'v', '0', m);
    writeBytes((uint8_t)(x & 0xFF), (uint8_t)(x >> 8));
    writeBytes((uint8_t)(y & 0xFF), (uint8_t)(y >> 8));
    writeN(data, len);
}

void Bontastic_Thermal::storeNvBitmaps(uint8_t n, const uint8_t *data, size_t len)
{
    writeBytes(ASCII_FS, 'q', n);
    writeN(data, len);
}

void Bontastic_Thermal::printNvBitmap(uint8_t n, uint8_t m) { writeBytes(ASCII_FS, 'p', n, m); }

void Bontastic_Thermal::userDefinedCharsEnabled(bool enabled) { writeBytes(ASCII_ESC, '%', enabled ? 1 : 0); }

void Bontastic_Thermal::defineUserDefinedChars(uint8_t y, uint8_t c1, uint8_t c2, const uint8_t *data, size_t len)
{
    writeBytes(ASCII_ESC, '&', y);
    writeBytes(c1, c2);
    writeN(data, len);
}

void Bontastic_Thermal::deleteUserDefinedChar(uint8_t n) { writeBytes(ASCII_ESC, '?', n); }

void Bontastic_Thermal::gsK(uint8_t cn, uint8_t fn, const uint8_t *data, size_t len)
{
    uint16_t p = (uint16_t)(len + 2);
    uint8_t pL = (uint8_t)(p & 0xFF);
    uint8_t pH = (uint8_t)(p >> 8);
    writeBytes(ASCII_GS, '(', 'k', pL);
    writeBytes(pH, cn, fn);
    writeN(data, len);
}

void Bontastic_Thermal::qrSelectModel(uint8_t model)
{
    uint8_t data[3] = {0x31, model, 0x00};
    gsK(0x31, 0x65, data, sizeof(data));
}

void Bontastic_Thermal::qrSetModuleSize(uint8_t n)
{
    uint8_t data[2] = {0x31, n};
    gsK(0x31, 0x67, data, sizeof(data));
}

void Bontastic_Thermal::qrSetErrorCorrection(uint8_t n)
{
    uint8_t data[2] = {0x31, n};
    gsK(0x31, 0x69, data, sizeof(data));
}

void Bontastic_Thermal::qrStoreData(const uint8_t *data, size_t len)
{
    uint8_t hdr[3] = {0x31, (uint8_t)(len & 0xFF), (uint8_t)(len >> 8)};
    uint16_t p = (uint16_t)(len + sizeof(hdr) + 2);
    uint8_t pL = (uint8_t)(p & 0xFF);
    uint8_t pH = (uint8_t)(p >> 8);
    writeBytes(ASCII_GS, '(', 'k', pL);
    writeBytes(pH, 0x31, 0x80);
    writeN(hdr, sizeof(hdr));
    writeN(data, len);
}

void Bontastic_Thermal::qrPrint()
{
    uint8_t data[1] = {0x30};
    gsK(0x31, 0x81, data, sizeof(data));
}

void Bontastic_Thermal::qrSelectDataType(uint8_t n)
{
    uint8_t data[2] = {0x31, n};
    gsK(0x31, 0x82, data, sizeof(data));
}

void Bontastic_Thermal::chineseModeOn() { writeBytes(ASCII_FS, '&'); }
void Bontastic_Thermal::chineseModeOff() { writeBytes(ASCII_FS, '.'); }
void Bontastic_Thermal::chineseFontMode(uint8_t n) { writeBytes(ASCII_FS, '!', n); }

void Bontastic_Thermal::setFont(char font)
{
    switch (toupper(font))
    {
    case 'B':
        _style |= STYLE_FONT_B;
        break;
    default:
        _style &= (uint8_t)~STYLE_FONT_B;
        break;
    }
    updateStyle();
}

void Bontastic_Thermal::setSize(char value)
{
    switch (toupper(value))
    {
    default:
    case 'S':
        _style &= (uint8_t)~STYLE_DOUBLE_HEIGHT;
        _style &= (uint8_t)~STYLE_DOUBLE_WIDTH;
        break;
    case 'M':
        _style |= STYLE_DOUBLE_HEIGHT;
        _style &= (uint8_t)~STYLE_DOUBLE_WIDTH;
        break;
    case 'L':
        _style |= STYLE_DOUBLE_HEIGHT;
        _style |= STYLE_DOUBLE_WIDTH;
        break;
    }
    updateStyle();
}

void Bontastic_Thermal::strikeOn() { doubleStrikeOn(); }
void Bontastic_Thermal::strikeOff() { doubleStrikeOff(); }

void Bontastic_Thermal::writeBytes(uint8_t a)
{
    if (_stream)
    {
        _stream->write(a);
    }
}

void Bontastic_Thermal::writeBytes(uint8_t a, uint8_t b)
{
    if (_stream)
    {
        _stream->write(a);
        _stream->write(b);
    }
}

void Bontastic_Thermal::writeBytes(uint8_t a, uint8_t b, uint8_t c)
{
    if (_stream)
    {
        _stream->write(a);
        _stream->write(b);
        _stream->write(c);
    }
}

void Bontastic_Thermal::writeBytes(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
    if (_stream)
    {
        _stream->write(a);
        _stream->write(b);
        _stream->write(c);
        _stream->write(d);
    }
}

void Bontastic_Thermal::writeN(const uint8_t *data, size_t len)
{
    if (!_stream)
    {
        return;
    }
    if (data && len)
    {
        _stream->write(data, len);
    }
}

void Bontastic_Thermal::updateStyle() { writeBytes(ASCII_ESC, '!', _style); }

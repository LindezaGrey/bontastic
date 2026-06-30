#include "Printer.h"
#include <HardwareSerial.h>
#include <WiFi.h>

static constexpr uint8_t ESC = 0x1B;
static constexpr uint8_t GS = 0x1D;

static HardwareSerial printerSerial(2);
EposPrinter printer(&printerSerial);

EposPrinter::EposPrinter(Stream *stream) : _stream(stream) {}

size_t EposPrinter::write(uint8_t c)
{
    if (c == '\r')
        return 1;
    return _stream ? _stream->write(c) : 0;
}

void EposPrinter::begin(uint8_t rx, uint8_t tx)
{
    printerSerial.end();
    printerSerial.begin(9600, SERIAL_8N1, rx, tx);
    reset();
}

void EposPrinter::reset()
{
    b(ESC, '@');
    lineHeight(30);
    align("left");
    bold(false);
    inverse(false);
    underline(false);
}

void EposPrinter::apply(const EposConfig &cfg)
{
    b(ESC, '7', cfg.heatDots);
    b(cfg.heatTime, cfg.heatInterval);
    b(0x12, '#', (uint8_t)((cfg.density << 5) | cfg.breakTime));
    lineHeight(cfg.lineHeight);
    charset(cfg.charset);
    codePage(cfg.codePage);
    font(cfg.font ? "font_b" : "font_a");
    scale(cfg.size == 2 ? 2 : 1, cfg.size ? 2 : 1);
    align(cfg.justify == 1 ? "center" : (cfg.justify == 2 ? "right" : "left"));
}

void EposPrinter::text(const String &value)
{
    print(value);
}

void EposPrinter::feed(uint8_t lines)
{
    b(ESC, 'd', lines);
}

void EposPrinter::cut()
{
    feed(3);
}

void EposPrinter::align(const String &value)
{
    String v = value;
    v.toLowerCase();
    b(ESC, 'a', v == "center" ? 1 : (v == "right" ? 2 : 0));
}

void EposPrinter::font(const String &value)
{
    String v = value;
    v.toLowerCase();
    b(ESC, 'M', v.endsWith("_b") || v == "b" ? 1 : 0);
}

void EposPrinter::scale(uint8_t width, uint8_t height)
{
    width = constrain(width, 1, 8);
    height = constrain(height, 1, 8);
    b(GS, '!', (uint8_t)(((width - 1) << 4) | (height - 1)));
}

void EposPrinter::bold(bool on) { b(ESC, 'E', on ? 1 : 0); }
void EposPrinter::inverse(bool on) { b(GS, 'B', on ? 1 : 0); }
void EposPrinter::underline(bool on) { b(ESC, '-', on ? 1 : 0); }
void EposPrinter::lineHeight(uint8_t n) { b(ESC, '3', n); }
void EposPrinter::charset(uint8_t n) { b(ESC, 'R', n > 15 ? 15 : n); }
void EposPrinter::codePage(uint8_t n) { b(ESC, 't', n > 47 ? 47 : n); }

void EposPrinter::raster(uint16_t width, uint16_t height, const uint8_t *data, size_t len)
{
    uint16_t widthBytes = (width + 7) / 8;
    rasterBytes(widthBytes, height, data, len);
}

void EposPrinter::rasterBytes(uint16_t widthBytes, uint16_t height, const uint8_t *data, size_t len)
{
    size_t expected = (size_t)widthBytes * height;
    Serial.printf("printer raster bytes=%u height=%u len=%u expected=%u\n", widthBytes, height, (unsigned)len, (unsigned)expected);
    b(GS, 'v', '0', 0);
    b((uint8_t)(widthBytes & 0xFF), (uint8_t)(widthBytes >> 8), (uint8_t)(height & 0xFF), (uint8_t)(height >> 8));
    n(data, len < expected ? len : expected);
}

void EposPrinter::barcode(const String &type, const String &data)
{
    uint8_t mode = 73;
    String t = type;
    t.toLowerCase();
    if (t.indexOf("ean13") >= 0)
        mode = 67;
    if (t.indexOf("code39") >= 0)
        mode = 69;
    b(GS, 'h', 80);
    b(GS, 'w', 2);
    b(GS, 'H', 2);
    b(GS, 'k', mode);
    b((uint8_t)data.length());
    n((const uint8_t *)data.c_str(), data.length());
}

void EposPrinter::qrcode(const String &data, uint8_t size)
{
    uint8_t model[2] = {50, 0};
    uint8_t module[1] = {(uint8_t)constrain(size, 1, 16)};
    uint8_t ec[1] = {48};
    gsK(0x31, 65, model, sizeof(model));
    gsK(0x31, 67, module, sizeof(module));
    gsK(0x31, 69, ec, sizeof(ec));
    uint16_t p = data.length() + 3;
    b(GS, '(', 'k', (uint8_t)(p & 0xFF));
    b((uint8_t)(p >> 8), 0x31, 80);
    b(0x30);
    n((const uint8_t *)data.c_str(), data.length());
    uint8_t print[1] = {0x30};
    gsK(0x31, 81, print, sizeof(print));
}

void EposPrinter::test()
{
    Serial.println("printer test write");
    reset();
    text("ePOS printer online\n");
    qrcode(WiFi.localIP().toString(), 4);
    feed(2);
    flush();
}

void EposPrinter::flush()
{
    if (_stream)
        _stream->flush();
}

void EposPrinter::b(uint8_t a)
{
    if (_stream)
        _stream->write(a);
}

void EposPrinter::b(uint8_t a, uint8_t bb)
{
    b(a);
    b(bb);
}

void EposPrinter::b(uint8_t a, uint8_t bb, uint8_t c)
{
    b(a);
    b(bb);
    b(c);
}

void EposPrinter::b(uint8_t a, uint8_t bb, uint8_t c, uint8_t d)
{
    b(a);
    b(bb);
    b(c);
    b(d);
}

void EposPrinter::n(const uint8_t *data, size_t len)
{
    if (_stream && data && len)
        _stream->write(data, len);
}

void EposPrinter::gsK(uint8_t cn, uint8_t fn, const uint8_t *data, size_t len)
{
    uint16_t p = len + 2;
    b(GS, '(', 'k', (uint8_t)(p & 0xFF));
    b((uint8_t)(p >> 8), cn, fn);
    n(data, len);
}

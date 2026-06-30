#pragma once

#include <Arduino.h>
#include "Config.h"

class EposPrinter : public Print
{
public:
    EposPrinter(Stream *stream = &Serial);
    size_t write(uint8_t c) override;
    void begin(uint8_t rx, uint8_t tx);
    void reset();
    void apply(const EposConfig &cfg);
    void text(const String &value);
    void feed(uint8_t lines = 1);
    void cut();
    void align(const String &value);
    void font(const String &value);
    void scale(uint8_t width, uint8_t height);
    void bold(bool on);
    void inverse(bool on);
    void underline(bool on);
    void lineHeight(uint8_t n);
    void charset(uint8_t n);
    void codePage(uint8_t n);
    void raster(uint16_t width, uint16_t height, const uint8_t *data, size_t len);
    void rasterBytes(uint16_t widthBytes, uint16_t height, const uint8_t *data, size_t len);
    void barcode(const String &type, const String &data);
    void qrcode(const String &data, uint8_t size);
    void test();
    void flush();

private:
    Stream *_stream;
    void b(uint8_t a);
    void b(uint8_t a, uint8_t b);
    void b(uint8_t a, uint8_t b, uint8_t c);
    void b(uint8_t a, uint8_t b, uint8_t c, uint8_t d);
    void n(const uint8_t *data, size_t len);
    void gsK(uint8_t cn, uint8_t fn, const uint8_t *data, size_t len);
};

extern EposPrinter printer;

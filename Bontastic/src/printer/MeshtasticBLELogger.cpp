#include "MeshtasticBLELogger.h"

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <stdarg.h>
#include <stdio.h>

static NimBLECharacteristic *logCharacteristic;

void bleLogAttachCharacteristic(NimBLECharacteristic *c)
{
    logCharacteristic = c;
}

void bleLog(const char *msg)
{
    if (!logCharacteristic || !msg)
    {
        return;
    }
    size_t n = strnlen(msg, 240);
    logCharacteristic->setValue(reinterpret_cast<const uint8_t *>(msg), n);
    logCharacteristic->notify();
}

void bleLogf(const char *fmt, ...)
{
    if (!logCharacteristic || !fmt)
    {
        return;
    }

    char buf[192] = {0};
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    buf[sizeof(buf) - 1] = 0;
    size_t n = strnlen(buf, sizeof(buf));
    logCharacteristic->setValue(reinterpret_cast<const uint8_t *>(buf), n);
    logCharacteristic->notify();
}

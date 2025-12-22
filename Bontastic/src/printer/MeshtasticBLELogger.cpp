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
    logCharacteristic->setValue(msg);
    logCharacteristic->notify();
}

void bleLogf(const char *fmt, ...)
{
    if (!logCharacteristic || !fmt)
    {
        return;
    }

    char buf[192];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    logCharacteristic->setValue(buf);
    logCharacteristic->notify();
}

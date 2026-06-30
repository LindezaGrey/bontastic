#pragma once

#include <Arduino.h>

struct EposConfig
{
    char ssid[33];
    char pass[65];
    uint8_t rxPin;
    uint8_t txPin;
    uint8_t errPin;
    uint8_t heatDots;
    uint8_t heatTime;
    uint8_t heatInterval;
    uint8_t density;
    uint8_t breakTime;
    uint8_t lineHeight;
    uint8_t font;
    uint8_t size;
    uint8_t justify;
    uint8_t charset;
    uint8_t codePage;
};

extern EposConfig config;
extern String deviceIp;

void loadConfig();
void saveConfig();
void applyPrinterConfig();
void ensurePrinterReady();
void invalidatePrinterConfig();
void reconnectWifi();
void requestWifiReconnect();
void requestTestPrint();

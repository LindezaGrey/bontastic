#pragma once

#include <Arduino.h>
#include <string>

void printTextMessage(const uint8_t *data, size_t size, const char *sender, uint32_t timestamp);
void printPosition(double lat, double lon, int32_t alt);
void printNodeInfo(uint32_t num, const char *name);
void printBinaryPayload(const uint8_t *data, size_t size);
void printInfo(const char *label, const char *value);
void printerSetup();
void printStartupLogo();
void updatePrinterPins(int rx, int tx);
std::string utf8ToIso88591(const std::string &utf8);

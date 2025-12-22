#pragma once

class NimBLECharacteristic;

void bleLogAttachCharacteristic(NimBLECharacteristic *c);
void bleLog(const char *msg);
void bleLogf(const char *fmt, ...);

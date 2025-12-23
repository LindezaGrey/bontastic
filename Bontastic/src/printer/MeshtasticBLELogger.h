#pragma once

class NimBLECharacteristic;

void bleLogAttachCharacteristic(NimBLECharacteristic *c);
void bleLogSetEnabled(bool enabled);
void bleLog(const char *msg);
void bleLogf(const char *fmt, ...);

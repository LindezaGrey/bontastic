#pragma once

#include <stdint.h>

void contestQrSetup();
void contestQrLoop();

void contestQrSetIntervalMinutes(uint32_t minutes);
void contestQrSetContent(const char *content);

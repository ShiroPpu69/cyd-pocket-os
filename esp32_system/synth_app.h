#pragma once

#include <Arduino.h>

class LGFX_CYD;

bool synthAppBegin(LGFX_CYD* display);
void synthAppEnd();
void synthAppTouch(uint16_t x, uint16_t y);
void synthAppRelease();
void synthAppTap(uint16_t x, uint16_t y);

#pragma once
#include <Arduino.h>

void systemHealthBegin();
void systemHealthStage(uint8_t stage, uint8_t screen);
void systemHealthTick(uint8_t screen, bool networkBusy);
void systemHealthReport(uint8_t screen, bool networkBusy);

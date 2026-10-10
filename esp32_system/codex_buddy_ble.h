#pragma once

#include <Arduino.h>

void codexBuddyBleBegin();
void codexBuddyBleEnd();
void codexBuddyBleLoop();
bool codexBuddyBleConnected();
bool codexBuddyBleSecure();
bool codexBuddyBleIdle();
uint32_t codexBuddyBlePasskey();
const char* codexBuddyBleDeviceName();
bool codexBuddyBleTakeLine(char* destination, size_t capacity);

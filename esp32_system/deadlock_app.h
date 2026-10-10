#pragma once
#include <Arduino.h>
class LGFX_CYD;
class BoundedSpotifyClient;
void deadlockAppBegin(LGFX_CYD* display, uint16_t* sharedIconBuffer, BoundedSpotifyClient* sharedTls);
void deadlockAppEnd();
void deadlockAppLoop(bool networkAllowed, bool wifiPaused = false);
void deadlockAppTap(uint16_t x, uint16_t y);
bool deadlockAppBusy();
void deadlockAppDiagnostics();

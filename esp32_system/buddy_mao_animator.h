#pragma once

#include <Arduino.h>

class LGFX_CYD;

// Official Codex Buddy state order:
// sleep, idle, busy, attention, completed, dizzy, heart.
enum class BuddyMaoState : uint8_t {
  SLEEP = 0,
  IDLE,
  BUSY,
  ATTENTION,
  COMPLETED,
  DIZZY,
  HEART
};

bool buddyMaoBegin(LGFX_CYD* display, int16_t x, int16_t y);
void buddyMaoEnd();
void buddyMaoSetState(BuddyMaoState state, bool celebrate = false);
bool buddyMaoTick();
BuddyMaoState buddyMaoStateFromName(const char* state);

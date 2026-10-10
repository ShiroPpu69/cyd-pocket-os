#pragma once

// Portrait launcher: one geometry shared by drawing and touch hit testing.
constexpr int HOME_GRID_X = 12;
constexpr int HOME_GRID_Y = 100;
constexpr int HOME_GRID_STEP_X = 76;
constexpr int HOME_GRID_STEP_Y = 84;
constexpr int HOME_ICON_SIZE = 64;
constexpr int HOME_CELL_HEIGHT = 80;
constexpr int HOME_NAV_Y = 278;
constexpr uint16_t HOME_BG = 0x0841;
constexpr uint16_t HOME_SURFACE = 0x18C3;
constexpr uint16_t HOME_EDGE = 0x39C7;
constexpr uint16_t HOME_TEXT = 0xEF7D;
constexpr uint16_t HOME_SECONDARY = 0xAD75;
constexpr uint16_t HOME_ACCENT = 0xAEB8;

uint16_t homeMix(uint16_t a, uint16_t b, unsigned amount) {
  const unsigned inverse = 256 - amount;
  const unsigned r = (((a >> 11) * inverse + (b >> 11) * amount) >> 8);
  const unsigned g = ((((a >> 5) & 63) * inverse + ((b >> 5) & 63) * amount) >> 8);
  const unsigned bl = (((a & 31) * inverse + (b & 31) * amount) >> 8);
  return (r << 11) | (g << 5) | bl;
}

int homeAppAt(int x, int y) {
  if (x < HOME_GRID_X || y < HOME_GRID_Y) return -1;
  const int col = (x - HOME_GRID_X) / HOME_GRID_STEP_X;
  const int row = (y - HOME_GRID_Y) / HOME_GRID_STEP_Y;
  if (col >= 3 || row >= 2 ||
      (x - HOME_GRID_X) % HOME_GRID_STEP_X >= HOME_ICON_SIZE ||
      (y - HOME_GRID_Y) % HOME_GRID_STEP_Y >= HOME_CELL_HEIGHT) return -1;
  const size_t index = homePage * HOME_APPS_PER_PAGE + row * 3 + col;
  return index < HOME_APP_COUNT ? static_cast<int>(index) : -1;
}

void drawHomeAppFeedback(int index, bool pressed) {
  if (index < 0) return;
  const int slot = index % HOME_APPS_PER_PAGE;
  const int x = HOME_GRID_X + (slot % 3) * HOME_GRID_STEP_X;
  const int y = HOME_GRID_Y + (slot / 3) * HOME_GRID_STEP_Y;
  lcd.drawRoundRect(x, y, HOME_ICON_SIZE, HOME_ICON_SIZE, 15,
                    pressed ? HOME_ACCENT : HOME_EDGE);
  lcd.drawRoundRect(x + 1, y + 1, HOME_ICON_SIZE - 2, HOME_ICON_SIZE - 2, 14,
                    pressed ? HOME_ACCENT : HOME_SURFACE);
}

int homePressedApp = -1;

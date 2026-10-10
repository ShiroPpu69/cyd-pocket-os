#pragma once

// Append apps here in display order. Every six entries start a new page.
// Screen is defined by the sketch before this header is included.
struct HomeApp {
  Screen screen;
  const char* nameEn;
  const char* nameZh;
  uint16_t color;
  uint8_t icon;
};

const HomeApp HOME_APPS[] = {
    {Screen::SETTINGS, "SETTINGS", "设置", C_LIME, 0},
    {Screen::DOTA, "DOTA", "DOTA", C_RED, 1},
    {Screen::SPOTIFY, "SPOTIFY", "SPOTIFY", C_GREEN, 2},
    {Screen::CODEX_BUDDY, "BUDDY", "BUDDY", C_AMBER, 3},
    {Screen::SYNTH, "SYNTH", "SYNTH", C_BLUE, 4},
    {Screen::PLACEHOLDER, "ABOUT", "关于", C_TEXT, 5},
    {Screen::DEADLOCK, "DEADLOCK", "死锁战绩", C_AMBER, 6},
};

constexpr size_t HOME_APPS_PER_PAGE = 6;
constexpr size_t HOME_APP_COUNT = sizeof(HOME_APPS) / sizeof(HOME_APPS[0]);
constexpr size_t HOME_PAGE_COUNT =
    ((HOME_APP_COUNT + HOME_APPS_PER_PAGE - 1) / HOME_APPS_PER_PAGE < 2)
        ? 2
        : (HOME_APP_COUNT + HOME_APPS_PER_PAGE - 1) / HOME_APPS_PER_PAGE;

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <JPEGDEC.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_bt.h>

#include "LGFX_CYD.h"
#include "buddy_mao_animator.h"
#include "codex_buddy_ble.h"
#include "hero_icons_rgb565.h"
#include "spotify_config.h"
#include "src/SpotifyArduino.h"
#include "synth_app.h"
#include "user_config.h"
#include "bounded_spotify_client.h"
#include "system_health.h"
#include "network_self_test.h"
#include "deadlock_app.h"

LGFX_CYD lcd;
Preferences preferences;

constexpr uint16_t C_BG = 0x0841;
constexpr uint16_t C_PANEL = 0x10C3;
constexpr uint16_t C_PANEL_2 = 0x1926;
constexpr uint16_t C_LINE = 0x326A;
constexpr uint16_t C_TEXT = 0xF7BE;
constexpr uint16_t C_MUTED = 0x7BEF;
constexpr uint16_t C_LIME = 0x05FF;
constexpr uint16_t C_AMBER = 0xFDC8;
constexpr uint16_t C_RED = 0xE289;
constexpr uint16_t C_GREEN = 0x3DAE;
constexpr uint16_t C_BLUE = 0x049F;

enum class Screen : uint8_t {
  HOME,
  SETTINGS,
  DOTA,
  SPOTIFY,
  CODEX_BUDDY,
  SYNTH,
  DEADLOCK,
  PLACEHOLDER
};
enum class Language : uint8_t { ENGLISH, CHINESE };

#include "home_apps.h"
size_t homePage = 0;

#include "home_style.h"

Screen currentScreen = Screen::HOME;
Language language = Language::ENGLISH;
uint8_t brightness = 78;
uint8_t placeholderIndex = 0;

bool touchDown = false;
bool gestureConsumed = false;
uint16_t touchStartX = 0;
uint16_t touchStartY = 0;
uint16_t touchLastX = 0;
uint16_t touchLastY = 0;
uint32_t touchStartedAt = 0;

struct MatchInfo {
  uint16_t heroId;
  int kills;
  int deaths;
  int assists;
  int durationMinutes;
  bool won;
};

MatchInfo recentMatches[10];
MatchInfo stagedMatches[10];
size_t recentMatchCount = 0;
size_t stagedMatchCount = 0;
uint16_t scaledHeroIcon[48 * 48];
portMUX_TYPE matchMux = portMUX_INITIALIZER_UNLOCKED;
std::atomic<bool> dotaRefreshBusy{false};
std::atomic<bool> dotaCancelled{false};
uint32_t dotaRefreshStartedAt = 0;
volatile bool stagedDataReady = false;
volatile bool forceDotaRedraw = false;

BoundedSpotifyClient spotifyTls;
SpotifyArduino spotifyApi(spotifyTls);
JPEGDEC spotifyJpeg;
enum class SpotifyView : uint8_t {
  NOW_PLAYING,
  PLAYLISTS,
  TRACKS,
  DEVICES
};
enum class SpotifyJob : uint8_t {
  NONE,
  AUTH,
  REFRESH,
  ARTWORK,
  PLAYLISTS,
  TRACKS,
  PREVIOUS,
  NEXT,
  TOGGLE,
  PLAY_TRACK,
  PLAYER_DETAILS,
  VOLUME,
  DEVICES,
  TRANSFER
};
struct SpotifyPlaylistRow {
  char id[32];
  char name[49];
  char uri[64];
};
struct SpotifyTrackRow {
  char name[49];
  char artist[37];
  char uri[64];
};
struct SpotifyDeviceRow {
  char id[48];
  char name[49];
  char type[25];
  bool active;
  bool restricted;
  int volume;
};
constexpr int SPOTIFY_PAGE_SIZE = 5;
bool spotifyInitialized = false;
bool spotifyAuthorized = false;
bool spotifyDeferredOpen = false;
bool spotifyImageChanged = false;
bool spotifyIsPlaying = false;
SpotifyView spotifyView = SpotifyView::NOW_PLAYING;
uint32_t spotifyRequestDueMs = 0;
uint32_t spotifyProgressDueMs = 0;
uint32_t spotifySongStartMs = 0;
uint32_t spotifySongDurationMs = 0;
uint32_t spotifyPausedProgressMs = 0;
bool spotifyMetadataChanged = true;
bool spotifyPlaybackChanged = true;
char spotifyTrackName[101] = "";
char spotifyArtistName[101] = "";
char spotifyAlbumName[101] = "";
char spotifyAlbumArtUrl[201] = "";
char spotifyDisplayedArtUrl[201] = "";
SpotifyPlaylistRow spotifyPlaylists[SPOTIFY_PAGE_SIZE];
SpotifyTrackRow spotifyTracks[SPOTIFY_PAGE_SIZE];
SpotifyDeviceRow spotifyDevices[SPOTIFY_PAGE_SIZE];
int spotifyPlaylistCount = 0;
int spotifyTrackCount = 0;
int spotifyPlaylistOffset = 0;
int spotifyTrackOffset = 0;
int spotifyPlaylistTotal = 0;
int spotifyTrackTotal = 0;
int spotifyDeviceCount = 0;
int spotifyVolume = 50;
bool spotifyVolumeKnown = false;
char spotifySelectedPlaylistId[32] = "";
char spotifySelectedPlaylistName[49] = "";
char spotifySelectedPlaylistUri[64] = "";
char spotifyListMessage[49] = "";
std::atomic<bool> spotifyJobBusy{false};
std::atomic<bool> spotifyJobResultReady{false};
volatile SpotifyJob spotifyCompletedJob = SpotifyJob::NONE;
volatile SpotifyJob spotifyQueuedJob = SpotifyJob::NONE;
volatile SpotifyJob spotifyActiveJob = SpotifyJob::NONE;
volatile int spotifyCompletedStatus = 0;
bool spotifyToggleTargetPlaying = false;
char spotifyPendingTrackUri[64] = "";
char spotifyPendingDeviceId[48] = "";
int spotifyPendingVolume = 50;
uint8_t* spotifyArtworkData = nullptr;
int spotifyArtworkLength = 0;
uint32_t spotifyJobStartedAt = 0;
uint32_t spotifyWifiStartedAt = 0;
uint32_t spotifyRetryAt = 0;
bool spotifyConnecting = false;
bool diagnosticWifiPaused = false;

// Parameters are copied before task creation; rapid taps cannot change an
// in-flight control request. The worker publishes data only after completion.
struct SpotifyRequestContext {
  int playlistOffset;
  int trackOffset;
  int volume;
  bool playing;
  char playlistId[32];
  char playlistUri[64];
  char trackUri[64];
  char deviceId[48];
  char artUrl[201];
};
SpotifyRequestContext spotifyRequest;

struct SpotifyWorkerResult {
  char track[101];
  char artist[101];
  char album[101];
  char artUrl[201];
  bool metadataValid;
  bool playing;
  uint32_t progress;
  uint32_t duration;
  uint32_t capturedAt;
  int volume;
  bool volumeValid;
  // Only one list request runs at a time. Share the staging storage instead
  // of spending scarce internal DRAM on three simultaneous list snapshots.
  union {
    SpotifyPlaylistRow playlists[SPOTIFY_PAGE_SIZE];
    SpotifyTrackRow tracks[SPOTIFY_PAGE_SIZE];
    SpotifyDeviceRow devices[SPOTIFY_PAGE_SIZE];
  };
  int playlistCount;
  int playlistTotal;
  int trackCount;
  int trackTotal;
  int deviceCount;
  uint8_t* artwork;
  int artworkLength;
};
SpotifyWorkerResult spotifyResult = {};

struct CodexBuddyState {
  char state[16] = "idle";
  char message[65] = "WAITING FOR CODEX BRIDGE";
  uint32_t tokens = 0;
  uint8_t primary = 0;
  uint8_t secondary = 0;
  uint32_t primaryResetsAt = 0;
  uint32_t secondaryResetsAt = 0;
  uint32_t senderNow = 0;
  uint32_t receivedAtMs = 0;
};

CodexBuddyState codexBuddy;
bool codexBuddyDirty = true;
bool codexBuddyDeferredOpen = false;
bool codexBuddyLastConnected = false;
bool codexBuddyLastSecure = false;
uint32_t codexBuddyLastFrameMs = 0;
uint32_t codexBuddyLastPasskey = 0;

enum class RefreshStatus : uint8_t {
  IDLE,
  CONNECTING,
  LOADING,
  SUCCESS,
  WIFI_FAILED,
  HTTP_FAILED,
  JSON_FAILED,
  NOT_CONFIGURED
};

volatile RefreshStatus refreshStatus = RefreshStatus::IDLE;
RefreshStatus shownRefreshStatus = RefreshStatus::IDLE;
uint32_t lastAttemptMs = 0;
uint32_t nextAttemptIntervalMs = 0;
constexpr uint32_t REFRESH_INTERVAL_MS = 3UL * 60UL * 1000UL;
constexpr uint32_t RETRY_INTERVAL_MS = 60UL * 1000UL;

void usePixelFont(uint8_t size = 1) {
  lcd.setFont(&fonts::Font0);
  lcd.setTextSize(size);
  lcd.setTextWrap(false);
}

void useChineseFont() {
  lcd.setFont(&fonts::efontCN_12);
  lcd.setTextSize(1);
  lcd.setTextWrap(false);
}

const char* tr(const char* english, const char* chinese) {
  return language == Language::CHINESE ? chinese : english;
}

void setUiFont(uint8_t pixelSize = 1) {
  if (language == Language::CHINESE) {
    useChineseFont();
  } else {
    usePixelFont(pixelSize);
  }
}

void printAt(const char* text, int x, int y, uint16_t color = C_TEXT) {
  lcd.setTextColor(color, C_BG);
  lcd.setCursor(x, y);
  lcd.print(text);
}

void drawCornerMarks(int x, int y, int w, int h, uint16_t color) {
  constexpr int d = 6;
  lcd.drawFastHLine(x, y, d, color);
  lcd.drawFastVLine(x, y, d, color);
  lcd.drawFastHLine(x + w - d, y, d, color);
  lcd.drawFastVLine(x + w - 1, y, d, color);
  lcd.drawFastHLine(x, y + h - 1, d, color);
  lcd.drawFastVLine(x, y + h - d, d, color);
  lcd.drawFastHLine(x + w - d, y + h - 1, d, color);
  lcd.drawFastVLine(x + w - 1, y + h - d, d, color);
}

void drawStatusBar(const char* section) {
  lcd.fillRect(0, 0, 240, 22, C_BG);
  lcd.drawFastHLine(0, 21, 240, C_LINE);
  usePixelFont(1);
  lcd.setTextColor(C_LIME, C_BG);
  lcd.setCursor(7, 7);
  lcd.print("CYD OS / ");
  lcd.setTextColor(C_MUTED, C_BG);
  lcd.print(section);

  const int bars = WiFi.status() == WL_CONNECTED ? 3 : 1;
  for (int i = 0; i < 3; ++i) {
    const uint16_t color = i < bars ? C_LIME : C_LINE;
    lcd.fillRect(191 + i * 5, 14 - i * 3, 3, 3 + i * 3, color);
  }
  lcd.drawRect(213, 7, 20, 9, C_MUTED);
  lcd.fillRect(233, 10, 2, 3, C_MUTED);
  lcd.fillRect(215, 9, map(brightness, 10, 100, 2, 16), 5, C_AMBER);
}

void drawHomePill() {
  lcd.fillRoundRect(88, 307, 64, 5, 3, C_MUTED);
}

void drawAppIcon(int x, int y, uint16_t color, uint8_t kind) {
  const uint16_t surface = homeMix(HOME_SURFACE, color, 35);
  const uint16_t ink = homeMix(color, HOME_TEXT, 88);
  lcd.fillRoundRect(x + 1, y + 3, 62, 63, 15, 0x0000);
  lcd.fillRoundRect(x, y, 64, 64, 15, HOME_SURFACE);
  lcd.fillRoundRect(x + 2, y + 2, 60, 60, 13, surface);
  lcd.drawRoundRect(x, y, 64, 64, 15, HOME_EDGE);
  lcd.drawFastHLine(x + 17, y + 2, 30, homeMix(surface, HOME_TEXT, 32));

  const int cx = x + 32;
  const int cy = y + 32;
  color = ink;
  switch (kind) {
    case 0:  // Settings: solid cog, consistent two-pixel details.
      for (int i = 0; i < 8; ++i) {
        const float angle = i * 0.785398f;
        const int tx = cx + static_cast<int>(cosf(angle) * 14);
        const int ty = cy + static_cast<int>(sinf(angle) * 14);
        lcd.fillRoundRect(tx - 3, ty - 3, 7, 7, 1, color);
      }
      lcd.fillCircle(cx, cy, 14, color);
      lcd.fillCircle(cx, cy, 7, surface);
      lcd.drawCircle(cx, cy, 4, color);
      break;
    case 1:  // Dota's diagonal cut and two opposite corner cuts.
      lcd.fillRect(cx - 17, cy - 17, 34, 34, color);
      lcd.fillTriangle(cx - 15, cy - 17, cx + 17, cy + 12, cx + 17, cy + 17, surface);
      lcd.fillTriangle(cx - 15, cy - 17, cx + 17, cy + 17, cx - 17, cy - 10, surface);
      lcd.fillTriangle(cx + 4, cy - 14, cx + 14, cy - 14, cx + 14, cy - 4, surface);
      lcd.fillTriangle(cx - 14, cy + 4, cx - 14, cy + 14, cx - 4, cy + 14, surface);
      break;
    case 2:  // Spotify: three descending sound curves.
      lcd.fillCircle(cx, cy, 20, color);
      for (int curve = 0; curve < 3; ++curve) {
        const int span = 13 - curve * 2;
        for (int px = -span; px <= span; ++px) {
          const int py = cy - 7 + curve * 7 + (px * px) / 80 + px / 8;
          lcd.fillCircle(cx + px, py, 1, surface);
        }
      }
      break;
    case 3:  // Actual Axe portrait, native RGB565 scalar drawing avoids byte swaps.
      for (int iy = 0; iy < HERO_ICON_HEIGHT; ++iy) {
        for (int ix = 0; ix < HERO_ICON_WIDTH; ++ix) {
          const uint8_t mask = pgm_read_byte(hero_icon_mask_2 + iy * 3 + ix / 8);
          if (!(mask & (0x80 >> (ix % 8)))) continue;
          lcd.fillRect(cx - 24 + ix * 2, cy - 24 + iy * 2, 2, 2,
                       pgm_read_word(hero_icon_2 + iy * HERO_ICON_WIDTH + ix));
        }
      }
      break;
    case 4:  // Synth: miniature keyboard, instantly recognizable.
      lcd.fillRoundRect(cx - 20, cy - 15, 40, 30, 3, color);
      for (int key = 1; key < 5; ++key) {
        lcd.drawFastVLine(cx - 20 + key * 8, cy - 12, 25, surface);
      }
      for (int key = 0; key < 4; ++key) {
        if (key == 2) continue;
        lcd.fillRect(cx - 15 + key * 8, cy - 15, 5, 17, surface);
      }
      break;
    case 6:  // Deadlock: keyhole silhouette.
      lcd.fillCircle(cx, cy - 7, 12, color);
      lcd.fillTriangle(cx - 5, cy + 1, cx - 11, cy + 20, cx + 11, cy + 20, color);
      lcd.fillTriangle(cx - 5, cy + 1, cx + 5, cy + 1, cx + 11, cy + 20, color);
      lcd.drawCircle(cx, cy - 7, 16, homeMix(surface, color, 75));
      break;
    default:  // info
      lcd.drawCircle(cx, cy, 18, color);
      lcd.drawCircle(cx, cy, 17, color);
      lcd.fillCircle(cx, cy - 9, 2, color);
      lcd.fillRoundRect(cx - 2, cy - 3, 5, 16, 1, color);
      break;
  }
}

void drawHome() {
  spotifyDeferredOpen = false;
  codexBuddyDeferredOpen = false;
  spotifyConnecting = false;
  spotifyQueuedJob = SpotifyJob::NONE;
  if (spotifyJobBusy) spotifyTls.cancel();
  if (!spotifyJobBusy && spotifyArtworkData) {
    free(spotifyArtworkData);
    spotifyArtworkData = nullptr;
    spotifyArtworkLength = 0;
  }
  if (currentScreen == Screen::CODEX_BUDDY) {
    buddyMaoEnd();
    codexBuddyBleEnd();
  } else if (currentScreen == Screen::SPOTIFY) {
    spotifyQueuedJob = SpotifyJob::NONE;
  } else if (currentScreen == Screen::SYNTH) {
    synthAppEnd();
  } else if (currentScreen == Screen::DOTA) {
    dotaCancelled = true;
  } else if (currentScreen == Screen::DEADLOCK) {
    deadlockAppEnd();
  }
  lcd.setRotation(0);
  currentScreen = Screen::HOME;
  homePressedApp = -1;
  lcd.startWrite();
  lcd.fillScreen(HOME_BG);
  // Static graphite wallpaper: zero frame buffer and no periodic repaint.
  lcd.fillTriangle(120, 23, 239, 23, 239, 230, 0x10A2);
  lcd.fillTriangle(175, 23, 239, 23, 239, 155, 0x18E3);
  lcd.drawLine(120, 23, 239, 230, 0x2104);
  usePixelFont(1);
  lcd.setTextColor(HOME_SECONDARY, HOME_BG);
  lcd.setCursor(13, 10);
  lcd.print("HOME");
  const bool connected = WiFi.status() == WL_CONNECTED;
  for (int i = 0; i < 3; ++i) {
    lcd.fillRect(179 + i * 5, 17 - i * 3, 3, 3 + i * 3,
                 connected ? HOME_ACCENT : HOME_EDGE);
  }
  lcd.drawCircle(208, 13, 3, HOME_SECONDARY);
  lcd.drawFastHLine(203, 13, 2, HOME_SECONDARY);
  lcd.drawFastHLine(212, 13, 2, HOME_SECONDARY);
  lcd.drawFastVLine(208, 8, 2, HOME_SECONDARY);
  lcd.drawFastVLine(208, 17, 2, HOME_SECONDARY);
  lcd.setCursor(219, 10);
  lcd.printf("%02u", brightness);

  usePixelFont(3);
  lcd.setTextColor(HOME_TEXT, HOME_BG);
  lcd.setCursor(12, 36);
  lcd.print("CYD OS");
  setUiFont(1);
  lcd.setTextColor(HOME_SECONDARY, HOME_BG);
  lcd.setCursor(13, 67);
  lcd.print(tr("Your apps, one touch away.", "你的应用，触手可及。"));
  usePixelFont(1);

  for (int i = 0; i < static_cast<int>(HOME_APPS_PER_PAGE); ++i) {
    const size_t appIndex = homePage * HOME_APPS_PER_PAGE + i;
    if (appIndex >= HOME_APP_COUNT) break;
    const HomeApp& app = HOME_APPS[appIndex];
    const int col = i % 3;
    const int row = i / 3;
    const int x = HOME_GRID_X + col * HOME_GRID_STEP_X;
    const int y = HOME_GRID_Y + row * HOME_GRID_STEP_Y;
    drawAppIcon(x, y, app.color, app.icon);
    setUiFont(1);
    const char* name = language == Language::CHINESE ? app.nameZh : app.nameEn;
    lcd.setTextColor(HOME_TEXT, HOME_BG);
    lcd.setTextDatum(top_center);
    lcd.drawString(name, x + 32, y + 68);
    lcd.setTextDatum(top_left);
  }

  if (homePage * HOME_APPS_PER_PAGE >= HOME_APP_COUNT) {
    lcd.drawRoundRect(74, 127, 92, 78, 18, HOME_EDGE);
    lcd.drawRoundRect(84, 117, 72, 78, 15, HOME_SECONDARY);
    lcd.drawFastHLine(108, 155, 24, HOME_ACCENT);
    lcd.drawFastVLine(120, 143, 24, HOME_ACCENT);
    setUiFont(1);
    lcd.setTextColor(HOME_TEXT, HOME_BG);
    lcd.setTextDatum(middle_center);
    lcd.drawString(tr("Room for more", "更多应用，等你加入"), 120, 227);
    lcd.setTextDatum(top_left);
  }

  lcd.fillRoundRect(12, HOME_NAV_Y, 216, 27, 13, HOME_SURFACE);
  const uint16_t leftInk = homePage > 0 ? HOME_TEXT : HOME_EDGE;
  const uint16_t rightInk = homePage + 1 < HOME_PAGE_COUNT ? HOME_TEXT : HOME_EDGE;
  lcd.drawLine(30, 286, 25, 291, leftInk);
  lcd.drawLine(25, 291, 30, 296, leftInk);
  lcd.drawLine(210, 286, 215, 291, rightInk);
  lcd.drawLine(215, 291, 210, 296, rightInk);
  usePixelFont(1);
  lcd.setTextColor(HOME_SECONDARY, HOME_SURFACE);
  lcd.setTextDatum(middle_center);
  char pageLabel[24];
  snprintf(pageLabel, sizeof(pageLabel), "%02u / %02u", static_cast<unsigned>(homePage + 1),
             static_cast<unsigned>(HOME_PAGE_COUNT));
  lcd.drawString(pageLabel, 120, 291);
  lcd.setTextDatum(top_left);
  // Compact page progress reflects actual pages, including many future apps.
  const int progressWidth = 32;
  const int segmentWidth = max(2, progressWidth / static_cast<int>(HOME_PAGE_COUNT));
  lcd.fillRoundRect(104, 313, progressWidth, 3, 1, HOME_EDGE);
  const int progressX = HOME_PAGE_COUNT <= 1 ? 104 :
      104 + homePage * (progressWidth - segmentWidth) / (HOME_PAGE_COUNT - 1);
  lcd.fillRoundRect(progressX, 313, segmentWidth, 3, 1, HOME_ACCENT);
  lcd.endWrite();
}

void drawHeader(const char* code, const char* titleEn, const char* titleZh) {
  lcd.fillScreen(C_BG);
  drawStatusBar(code);
  setUiFont(language == Language::ENGLISH ? 2 : 1);
  lcd.setTextColor(C_TEXT, C_BG);
  lcd.setCursor(10, 31);
  lcd.print(tr(titleEn, titleZh));
  lcd.drawFastHLine(10, 57, 220, C_LINE);
  lcd.fillRoundRect(10, 57, 56, 2, 1, C_LIME);
}

void drawBrightnessControl() {
  lcd.fillRoundRect(12, 91, 216, 54, 6, C_PANEL);
  lcd.drawRoundRect(12, 91, 216, 54, 6, C_LINE);
  usePixelFont(2);
  lcd.setTextColor(C_AMBER, C_PANEL);
  lcd.setCursor(21, 102);
  lcd.printf("%u%%", brightness);
  lcd.fillRoundRect(74, 111, 140, 6, 3, C_LINE);
  const int knobX = 74 + map(brightness, 10, 100, 0, 140);
  lcd.fillRoundRect(74, 111, knobX - 74, 6, 3, C_AMBER);
  lcd.fillCircle(knobX, 114, 7, C_TEXT);
}

void drawSettings() {
  currentScreen = Screen::SETTINGS;
  drawHeader("CFG", "SETTINGS", "设置");

  setUiFont();
  printAt(tr("DISPLAY OUTPUT", "显示亮度"), 12, 73, C_MUTED);
  drawBrightnessControl();

  setUiFont();
  printAt(tr("SYSTEM LANGUAGE", "系统语言"), 12, 160, C_MUTED);
  lcd.fillRoundRect(12, 179, 102, 62, 6,
                    language == Language::ENGLISH ? C_PANEL_2 : C_PANEL);
  lcd.fillRoundRect(126, 179, 102, 62, 6,
                    language == Language::CHINESE ? C_PANEL_2 : C_PANEL);
  lcd.drawRoundRect(12, 179, 102, 62, 6,
                    language == Language::ENGLISH ? C_LIME : C_LINE);
  lcd.drawRoundRect(126, 179, 102, 62, 6,
                    language == Language::CHINESE ? C_LIME : C_LINE);
  usePixelFont(2);
  lcd.setTextColor(C_TEXT, language == Language::ENGLISH ? C_PANEL_2 : C_PANEL);
  lcd.setCursor(25, 193);
  lcd.print("EN");
  usePixelFont(1);
  lcd.setCursor(25, 218);
  lcd.print("ENGLISH");
  useChineseFont();
  lcd.setTextColor(C_TEXT, language == Language::CHINESE ? C_PANEL_2 : C_PANEL);
  lcd.setCursor(141, 193);
  lcd.print("中");
  lcd.setCursor(141, 218);
  lcd.print("简体中文");

  setUiFont();
  printAt(tr("Preferences save automatically", "设置会自动保存"), 12, 259, C_MUTED);
  drawHomePill();
}

void drawPlaceholder(uint8_t index) {
  currentScreen = Screen::PLACEHOLDER;
  placeholderIndex = index;
  const char* namesEn[] = {"FILES", "SYSTEM", "ABOUT"};
  const char* namesZh[] = {"文件", "系统", "关于"};
  drawHeader("APP", namesEn[index], namesZh[index]);
  lcd.fillRoundRect(12, 81, 216, 145, 8, C_PANEL);
  lcd.drawRoundRect(18, 87, 204, 133, 6, C_LINE);
  usePixelFont(3);
  lcd.setTextColor(C_LIME, C_PANEL);
  lcd.setCursor(94, 111);
  lcd.print("+");
  setUiFont();
  lcd.setTextColor(C_TEXT, C_PANEL);
  lcd.setTextDatum(middle_center);
  lcd.drawString(tr("APP SLOT READY", "应用位置已就绪"), 120, 165);
  lcd.setTextDatum(top_left);
  usePixelFont(1);
  lcd.setTextColor(C_MUTED, C_PANEL);
  lcd.setCursor(48, 195);
  lcd.print("READY FOR NEXT APP");
  drawHomePill();
}

uint16_t makeColor565(uint8_t red, uint8_t green, uint8_t blue) {
  return ((red & 0xF8) << 8) | ((green & 0xFC) << 3) | (blue >> 3);
}

uint16_t codexBuddyStateColor() {
  if (strcmp(codexBuddy.state, "busy") == 0) return C_BLUE;
  if (strcmp(codexBuddy.state, "attention") == 0) return C_AMBER;
  if (strcmp(codexBuddy.state, "completed") == 0 ||
      strcmp(codexBuddy.state, "celebrate") == 0) {
    return C_LIME;
  }
  if (strcmp(codexBuddy.state, "dizzy") == 0) return C_RED;
  if (strcmp(codexBuddy.state, "heart") == 0) return 0xF81F;
  return C_MUTED;
}

void formatBuddyTokens(char* output, size_t length) {
  if (codexBuddy.tokens >= 1000000) {
    snprintf(output, length, "%lu.%01luM TOKENS",
             static_cast<unsigned long>(codexBuddy.tokens / 1000000),
             static_cast<unsigned long>((codexBuddy.tokens / 100000) % 10));
  } else if (codexBuddy.tokens >= 1000) {
    snprintf(output, length, "%lu.%01luK TOKENS",
             static_cast<unsigned long>(codexBuddy.tokens / 1000),
             static_cast<unsigned long>((codexBuddy.tokens / 100) % 10));
  } else {
    snprintf(output, length, "%lu TOKENS",
             static_cast<unsigned long>(codexBuddy.tokens));
  }
}

void formatBuddyReset(uint32_t resetAt, char* output, size_t length) {
  if (resetAt == 0 || codexBuddy.senderNow == 0) {
    strlcpy(output, "RESET --", length);
    return;
  }
  const uint32_t now =
      codexBuddy.senderNow + (millis() - codexBuddy.receivedAtMs) / 1000;
  const uint32_t left = resetAt > now ? resetAt - now : 0;
  if (left >= 86400) {
    snprintf(output, length, "%lud %luh",
             static_cast<unsigned long>(left / 86400),
             static_cast<unsigned long>((left / 3600) % 24));
  } else {
    snprintf(output, length, "%luh %02lum",
             static_cast<unsigned long>(left / 3600),
             static_cast<unsigned long>((left / 60) % 60));
  }
}

void drawCodexBuddyMeter(int y, const char* label, uint8_t percent,
                         uint32_t resetAt) {
  char reset[20];
  formatBuddyReset(resetAt, reset, sizeof(reset));
  usePixelFont(1);
  lcd.setTextColor(C_TEXT, C_BG);
  lcd.setCursor(14, y);
  lcd.print(label);
  lcd.setTextColor(C_MUTED, C_BG);
  lcd.setCursor(66, y);
  lcd.print(reset);
  lcd.setTextColor(percent >= 85 ? C_RED : percent >= 60 ? C_AMBER : C_LIME,
                   C_BG);
  lcd.setTextDatum(top_right);
  lcd.drawString(String(percent) + "%", 226, y);
  lcd.setTextDatum(top_left);
  lcd.drawRect(14, y + 15, 212, 14, C_LINE);
  const int fillWidth = map(constrain(percent, 0, 100), 0, 100, 0, 208);
  lcd.fillRect(16, y + 17, fillWidth, 10,
               percent >= 85 ? C_RED : percent >= 60 ? C_AMBER : C_LIME);
  lcd.fillRect(16 + fillWidth, y + 17, 208 - fillWidth, 10, C_PANEL);
}

void drawCodexBuddyDetails() {
  const uint16_t stateColor = codexBuddyStateColor();

  // Connectivity and state borders are tiny, independent update regions.
  lcd.fillRect(0, 23, 46, 42, C_BG);
  usePixelFont(1);
  lcd.setTextColor(codexBuddyBleSecure()
                       ? C_LIME
                       : codexBuddyBleConnected() ? C_AMBER : C_MUTED,
                   C_BG);
  lcd.setTextDatum(top_center);
  lcd.drawString(codexBuddyBleSecure() ? "LIVE"
                 : codexBuddyBleConnected() ? "PAIR"
                                            : "WAIT",
                 23, 29);
  lcd.drawFastHLine(8, 45, 30, stateColor);
  lcd.drawFastHLine(13, 49, 20, stateColor);
  lcd.drawFastHLine(18, 53, 10, stateColor);
  lcd.drawRoundRect(46, 22, 148, 160, 10, stateColor);

  // Everything below the character is redrawn as one small stable panel.
  // The 144x156 animation above is never cleared by usage updates.
  lcd.fillRect(0, 183, 240, 122, C_BG);
  char tokenText[24];
  formatBuddyTokens(tokenText, sizeof(tokenText));
  usePixelFont(2);
  lcd.setTextColor(C_TEXT, C_BG);
  lcd.setTextDatum(top_center);
  lcd.drawString(tokenText, 120, 185);
  usePixelFont(1);
  lcd.setTextColor(stateColor, C_BG);
  String stateLine = String("[") + codexBuddy.state + "] " +
                     codexBuddy.message;
  if (stateLine.length() > 36) stateLine = stateLine.substring(0, 36);
  lcd.drawString(stateLine, 120, 204);
  lcd.setTextDatum(top_left);

  const uint32_t pairingCode = codexBuddyBlePasskey();
  if (pairingCode != 0) {
    lcd.fillRoundRect(24, 224, 192, 63, 7, C_PANEL_2);
    lcd.drawRoundRect(24, 224, 192, 63, 7, C_AMBER);
    usePixelFont(1);
    lcd.setTextDatum(top_center);
    lcd.setTextColor(C_AMBER, C_PANEL_2);
    lcd.drawString("ENTER PAIRING CODE", 120, 232);
    usePixelFont(3);
    char code[8];
    snprintf(code, sizeof(code), "%06lu",
             static_cast<unsigned long>(pairingCode));
    lcd.drawString(code, 120, 251);
    lcd.setTextDatum(top_left);
  } else {
    drawCodexBuddyMeter(225, "5 HOUR", codexBuddy.primary,
                        codexBuddy.primaryResetsAt);
    drawCodexBuddyMeter(263, "7 DAY", codexBuddy.secondary,
                        codexBuddy.secondaryResetsAt);
  }

  usePixelFont(1);
  lcd.setTextColor(C_MUTED, C_BG);
  lcd.setTextDatum(top_center);
  lcd.drawString(codexBuddyBleDeviceName(), 120, 295);
  lcd.setTextDatum(top_left);
  drawHomePill();
  codexBuddyDirty = false;
  codexBuddyLastFrameMs = millis();
}

void drawCodexBuddy() {
  lcd.setRotation(0);
  currentScreen = Screen::CODEX_BUDDY;
  lcd.fillScreen(C_BG);
  drawStatusBar("BUDDY");
  lcd.fillRoundRect(47, 23, 146, 158, 9, TFT_BLACK);
  buddyMaoBegin(&lcd, 48, 24);
  buddyMaoSetState(buddyMaoStateFromName(codexBuddy.state),
                   strcmp(codexBuddy.state, "celebrate") == 0);
  drawCodexBuddyDetails();
  // Decode the first official frame immediately instead of leaving a blank
  // character window until the next main-loop pass.
  buddyMaoTick();
}

void openCodexBuddy() {
  lcd.setRotation(0);
  currentScreen = Screen::CODEX_BUDDY;
  lcd.fillScreen(C_BG);
  usePixelFont(2);
  lcd.setTextColor(C_LIME, C_BG);
  lcd.setTextDatum(middle_center);
  lcd.drawString("STARTING BUDDY", 120, 145);
  lcd.setTextDatum(top_left);
  if (spotifyJobBusy || spotifyJobResultReady || dotaRefreshBusy || deadlockAppBusy()) {
    codexBuddyDeferredOpen = true;
    usePixelFont(1);
    lcd.setTextColor(C_MUTED, C_BG);
    lcd.drawString("WAITING FOR NETWORK", 120, 173);
  } else {
    codexBuddyDeferredOpen = false;
    // This board has no PSRAM. Buddy uses BLE only: release Wi-Fi before
    // allocating its animation and the vendor Bluetooth stack.
    WiFi.mode(WIFI_OFF);
    codexBuddyDirty = true;
    drawCodexBuddy();
    codexBuddyBleBegin();
  }
}

void parseCodexBuddyLine(const char* line) {
  JsonDocument document;
  if (deserializeJson(document, line)) {
    Serial.println("[buddy] invalid JSON packet");
    return;
  }
  const uint32_t previousTokens = codexBuddy.tokens;
  const uint8_t previousPrimary = codexBuddy.primary;
  const uint8_t previousSecondary = codexBuddy.secondary;
  const uint32_t previousPrimaryReset = codexBuddy.primaryResetsAt;
  const uint32_t previousSecondaryReset = codexBuddy.secondaryResetsAt;
  char previousState[sizeof(codexBuddy.state)];
  char previousMessage[sizeof(codexBuddy.message)];
  strlcpy(previousState, codexBuddy.state, sizeof(previousState));
  strlcpy(previousMessage, codexBuddy.message, sizeof(previousMessage));
  const char* state = document["state"];
  if (state && state[0]) {
    strlcpy(codexBuddy.state, state, sizeof(codexBuddy.state));
  } else {
    const int running = document["running"] | 0;
    const int waiting = document["waiting"] | 0;
    strlcpy(codexBuddy.state,
            waiting > 0 ? "attention" : running > 0 ? "busy" : "idle",
            sizeof(codexBuddy.state));
  }
  if (document["tokens"].is<uint32_t>()) {
    codexBuddy.tokens = document["tokens"].as<uint32_t>();
  }
  if (document["primary"].is<int>()) {
    codexBuddy.primary =
        constrain(document["primary"].as<int>(), 0, 100);
  }
  if (document["secondary"].is<int>()) {
    codexBuddy.secondary =
        constrain(document["secondary"].as<int>(), 0, 100);
  }
  codexBuddy.primaryResetsAt =
      document["primary_resets_at"] | codexBuddy.primaryResetsAt;
  codexBuddy.secondaryResetsAt =
      document["secondary_resets_at"] | codexBuddy.secondaryResetsAt;
  codexBuddy.senderNow = document["now"] | codexBuddy.senderNow;
  const char* message = document["message"];
  if (!message) message = document["msg"];
  if (message && message[0]) {
    strlcpy(codexBuddy.message, message, sizeof(codexBuddy.message));
  } else {
    strlcpy(codexBuddy.message, "CODEX USAGE LIVE",
            sizeof(codexBuddy.message));
  }
  codexBuddy.receivedAtMs = millis();
  codexBuddyDirty =
      codexBuddyDirty || previousTokens != codexBuddy.tokens ||
      previousPrimary != codexBuddy.primary ||
      previousSecondary != codexBuddy.secondary ||
      previousPrimaryReset != codexBuddy.primaryResetsAt ||
      previousSecondaryReset != codexBuddy.secondaryResetsAt ||
      strcmp(previousState, codexBuddy.state) != 0 ||
      strcmp(previousMessage, codexBuddy.message) != 0;
  Serial.printf("[buddy] %s, 5h=%u%%, 7d=%u%%\n", codexBuddy.state,
                codexBuddy.primary, codexBuddy.secondary);
}

void codexBuddyLoop() {
  codexBuddyBleLoop();
  if (currentScreen == Screen::CODEX_BUDDY && codexBuddyDeferredOpen &&
      !spotifyJobBusy && !spotifyJobResultReady && !dotaRefreshBusy && !deadlockAppBusy()) {
    codexBuddyDeferredOpen = false;
    WiFi.mode(WIFI_OFF);
    codexBuddyDirty = true;
    drawCodexBuddy();
    codexBuddyBleBegin();
  }
  char packet[768];
  for (int i = 0; i < 4 && codexBuddyBleTakeLine(packet, sizeof(packet));
       ++i) {
    parseCodexBuddyLine(packet);
  }

  const bool connected = codexBuddyBleConnected();
  const bool secure = codexBuddyBleSecure();
  const uint32_t passkey = codexBuddyBlePasskey();
  if (connected != codexBuddyLastConnected ||
      secure != codexBuddyLastSecure || passkey != codexBuddyLastPasskey) {
    codexBuddyLastConnected = connected;
    codexBuddyLastSecure = secure;
    codexBuddyLastPasskey = passkey;
    codexBuddyDirty = true;
  }
  if (codexBuddy.receivedAtMs != 0 &&
      millis() - codexBuddy.receivedAtMs > 30000 &&
      strcmp(codexBuddy.message, "BRIDGE SIGNAL LOST") != 0) {
    strlcpy(codexBuddy.message, "BRIDGE SIGNAL LOST",
            sizeof(codexBuddy.message));
    codexBuddyDirty = true;
  }
  if (currentScreen == Screen::CODEX_BUDDY &&
      (codexBuddyDirty || millis() - codexBuddyLastFrameMs >= 60000)) {
    buddyMaoSetState(buddyMaoStateFromName(codexBuddy.state),
                     strcmp(codexBuddy.state, "celebrate") == 0);
    drawCodexBuddyDetails();
  }
  if (currentScreen == Screen::CODEX_BUDDY) {
    buddyMaoTick();
  }
}

// Dota app rendering below intentionally mirrors the standalone project.
// System navigation is handled only by pollTouch(), outside this app.
void drawDotaMessage(const char* title, const char* detail = "") {
  lcd.fillScreen(TFT_BLACK);
  usePixelFont(2);
  lcd.setTextColor(TFT_WHITE, TFT_BLACK);
  lcd.setCursor(12, 80);
  lcd.println(title);
  if (detail[0] != '\0') {
    usePixelFont(1);
    lcd.setTextColor(TFT_YELLOW, TFT_BLACK);
    lcd.setCursor(12, 115);
    lcd.println(detail);
  }
}

void drawHeroFallback(int x, int y, uint16_t heroId, uint16_t bg) {
  const uint8_t red = 50 + ((heroId * 47) % 180);
  const uint8_t green = 50 + ((heroId * 73) % 180);
  const uint8_t blue = 50 + ((heroId * 97) % 180);
  lcd.fillRect(x, y, 48, 48, makeColor565(red, green, blue));
  lcd.drawRect(x, y, 48, 48, C_TEXT);
  usePixelFont(2);
  lcd.setTextColor(TFT_BLACK);
  lcd.setCursor(x + (heroId < 10 ? 18 : heroId < 100 ? 12 : 6), y + 16);
  lcd.print(heroId);
}

void drawHeroIcon(int x, int y, uint16_t heroId, uint16_t bg) {
  const uint16_t* icon = getHeroIconById(heroId);
  const uint8_t* mask = getHeroIconMaskById(heroId);
  if (icon == nullptr || mask == nullptr) {
    drawHeroFallback(x, y, heroId, bg);
    return;
  }

  constexpr int sourceW = HERO_ICON_WIDTH;
  constexpr int maskBytes = (sourceW + 7) / 8;
  for (int sy = 0; sy < HERO_ICON_HEIGHT; ++sy) {
    for (int sx = 0; sx < sourceW; ++sx) {
      const uint8_t maskByte = pgm_read_byte(&mask[sy * maskBytes + sx / 8]);
      const bool opaque = maskByte & (0x80 >> (sx & 7));
      const uint16_t color = opaque
          ? pgm_read_word(&icon[sy * sourceW + sx])
          : bg;
      const int ox = sx * 2;
      const int oy = sy * 2;
      scaledHeroIcon[oy * 48 + ox] = color;
      scaledHeroIcon[oy * 48 + ox + 1] = color;
      scaledHeroIcon[(oy + 1) * 48 + ox] = color;
      scaledHeroIcon[(oy + 1) * 48 + ox + 1] = color;
    }
  }
  // The generated Dota assets contain native uint16_t RGB565 values for
  // Arduino_GFX. LovyanGFX's fast uint16_t path needs the opposite byte flag.
  const bool previousSwap = lcd.getSwapBytes();
  lcd.setSwapBytes(true);
  lcd.pushImage(x, y, 48, 48, scaledHeroIcon);
  lcd.setSwapBytes(previousSwap);
}

void drawDotaRowStat(const char* label, int value, int x, int y,
                     uint16_t valueColor) {
  usePixelFont(1);
  lcd.setTextColor(makeColor565(115, 128, 148));
  lcd.setCursor(x, y + 3);
  lcd.print(label);
  usePixelFont(2);
  lcd.setTextColor(valueColor);
  lcd.setCursor(x, y + 12);
  lcd.print(value);
}

void drawDotaRows() {
  lcd.fillScreen(makeColor565(8, 10, 14));
  constexpr int startY = 5;
  constexpr int rowStride = 62;
  constexpr int rowHeight = 57;
  const size_t count = min(static_cast<size_t>(5), recentMatchCount);
  for (size_t i = 0; i < count; ++i) {
    const MatchInfo& match = recentMatches[i];
    const int y = startY + i * rowStride;
    const uint16_t resultColor =
        match.won ? makeColor565(38, 176, 92)
                  : makeColor565(224, 67, 67);
    const uint16_t rowColor =
        i % 2 == 0 ? makeColor565(22, 26, 34)
                   : makeColor565(17, 20, 27);

    lcd.fillRoundRect(3, y, 234, rowHeight, 5, rowColor);
    lcd.fillRect(3, y + 8, 3, rowHeight - 16, resultColor);
    drawHeroIcon(9, y + 4, match.heroId, rowColor);
    lcd.fillRoundRect(64, y + 9, 29, 38, 5, resultColor);
    usePixelFont(3);
    lcd.setTextColor(TFT_WHITE);
    lcd.setCursor(70, y + 17);
    lcd.print(match.won ? "W" : "L");

    drawDotaRowStat("K", match.kills, 101, y + 9, TFT_WHITE);
    drawDotaRowStat("D", match.deaths, 131, y + 9, TFT_WHITE);
    drawDotaRowStat("A", match.assists, 161, y + 9, TFT_WHITE);
    drawDotaRowStat("MIN", match.durationMinutes, 197, y + 9,
                    makeColor565(255, 201, 71));
  }
}

void drawDota() {
  lcd.setRotation(0);
  currentScreen = Screen::DOTA;
  if (recentMatchCount > 0) {
    drawDotaRows();
  } else {
    switch (refreshStatus) {
      case RefreshStatus::CONNECTING:
        drawDotaMessage("CONNECTING WIFI");
        break;
      case RefreshStatus::WIFI_FAILED:
        drawDotaMessage("WIFI FAILED", "Will retry in 1 minute");
        break;
      case RefreshStatus::HTTP_FAILED:
        drawDotaMessage("OPENDOTA ERROR", "Will retry in 1 minute");
        break;
      case RefreshStatus::JSON_FAILED:
        drawDotaMessage("JSON ERROR", "Will retry in 1 minute");
        break;
      case RefreshStatus::NOT_CONFIGURED:
        drawDotaMessage("DOTA DISPLAY", "Configure user_config.h");
        break;
      case RefreshStatus::LOADING:
        // Preserve the standalone app's startup screen while loading.
        break;
      default:
        drawDotaMessage("DOTA DISPLAY", "ESP32 starting...");
        break;
    }
  }
  shownRefreshStatus = refreshStatus;
  forceDotaRedraw = false;
}

bool spotifyConfigReady() {
  return strcmp(SPOTIFY_CLIENT_ID, "YOUR_SPOTIFY_CLIENT_ID") != 0 &&
         strcmp(SPOTIFY_CLIENT_SECRET, "YOUR_SPOTIFY_CLIENT_SECRET") != 0 &&
         strcmp(SPOTIFY_REFRESH_TOKEN, "YOUR_SPOTIFY_REFRESH_TOKEN") != 0;
}

void drawSpotifyText() {
  const uint16_t playerBg = makeColor565(8, 11, 10);
  lcd.fillRect(101, 10, 129, 119, playerBg);
  lcd.setTextDatum(top_left);
  usePixelFont(1);
  lcd.setTextColor(C_LIME, playerBg);
  lcd.drawString("SONG", 102, 13);
  lcd.setTextColor(C_TEXT, playerBg);
  String song = spotifyTrackName[0] ? spotifyTrackName : "NOTHING PLAYING";
  if (song.length() > 20) song = song.substring(0, 19) + ".";
  lcd.drawString(song, 102, 29);

  lcd.setTextColor(C_LIME, playerBg);
  lcd.drawString("ARTIST", 102, 52);
  lcd.setTextColor(C_MUTED, playerBg);
  String artist = spotifyArtistName;
  if (artist.length() > 20) artist = artist.substring(0, 19) + ".";
  lcd.drawString(artist, 102, 68);

  lcd.setTextColor(C_LIME, playerBg);
  lcd.drawString("ALBUM", 102, 91);
  lcd.setTextColor(C_MUTED, playerBg);
  String album = spotifyAlbumName;
  if (album.length() > 20) album = album.substring(0, 19) + ".";
  lcd.drawString(album, 102, 107);
}

void drawSpotifyProgress(uint32_t progress, uint32_t duration) {
  if (duration == 0 || currentScreen != Screen::SPOTIFY) return;
  const int width = constrain(
      static_cast<int>((static_cast<uint64_t>(progress) * 216) / duration),
      0, 216);
  lcd.drawRect(10, 143, 220, 12, C_LINE);
  lcd.fillRect(12, 145, width, 8, C_LIME);
  lcd.fillRect(12 + width, 145, 216 - width, 8,
               makeColor565(19, 23, 21));
}

void drawSpotifyControls() {
  constexpr int wheelX = 128;
  constexpr int wheelY = 245;
  const uint16_t playerBg = makeColor565(8, 11, 10);
  lcd.fillRect(10, 116, 82, 16, playerBg);
  usePixelFont(1);
  lcd.setTextDatum(middle_center);
  lcd.setTextColor(spotifyIsPlaying ? C_LIME : C_AMBER, playerBg);
  lcd.drawString(spotifyIsPlaying ? "PLAY" : "PAUSE", 51, 124);

  lcd.fillCircle(wheelX, wheelY, 57, makeColor565(34, 38, 36));
  lcd.drawCircle(wheelX, wheelY, 57, makeColor565(125, 135, 130));
  lcd.drawCircle(wheelX, wheelY, 54, C_LINE);

  usePixelFont(1);
  lcd.setTextDatum(middle_center);
  lcd.setTextColor(TFT_WHITE, makeColor565(34, 38, 36));
  lcd.drawString("VOL+", wheelX, wheelY - 39);
  lcd.drawString("|<<", wheelX - 40, wheelY);
  lcd.drawString(">>|", wheelX + 40, wheelY);
  lcd.drawString("VOL-", wheelX, wheelY + 40);

  lcd.fillCircle(wheelX, wheelY, 24, makeColor565(11, 14, 13));
  lcd.drawCircle(wheelX, wheelY, 24, C_LIME);
  lcd.drawCircle(wheelX, wheelY, 21, C_LINE);
  lcd.setTextColor(TFT_WHITE, makeColor565(11, 14, 13));
  lcd.drawString(spotifyIsPlaying ? "II" : ">", wheelX, wheelY);

  lcd.fillRoundRect(7, 278, 42, 28, 5, makeColor565(24, 28, 26));
  lcd.drawRoundRect(7, 278, 42, 28, 5, C_LIME);
  lcd.setTextColor(C_LIME, makeColor565(24, 28, 26));
  lcd.drawString("MENU", 28, 292);
  lcd.setTextDatum(top_left);
}

void drawSpotifyBase() {
  lcd.setRotation(0);
  currentScreen = Screen::SPOTIFY;
  spotifyView = SpotifyView::NOW_PLAYING;
  lcd.fillScreen(TFT_BLACK);
  lcd.fillRoundRect(4, 4, 232, 164, 6, makeColor565(8, 11, 10));
  lcd.drawRoundRect(4, 4, 232, 164, 6, C_LINE);
  lcd.drawFastVLine(97, 10, 119, C_LINE);
  lcd.drawRect(9, 10, 84, 84, makeColor565(75, 84, 80));
  lcd.fillRect(11, 12, 80, 80, TFT_BLACK);
  lcd.setTextDatum(middle_center);
  usePixelFont(1);
  lcd.setTextColor(C_LIME, TFT_BLACK);
  lcd.drawString("SPOTIFY", 51, 52);
  lcd.setTextColor(C_MUTED, makeColor565(8, 11, 10));
  lcd.drawString("NOW PLAYING", 51, 108);
  lcd.setTextColor(spotifyIsPlaying ? C_LIME : C_AMBER,
                   makeColor565(8, 11, 10));
  lcd.drawString(spotifyIsPlaying ? "PLAY" : "PAUSE", 51, 124);
  lcd.setTextDatum(top_left);
  drawSpotifyProgress(0, 1);
  drawSpotifyText();
  drawSpotifyControls();
  if (spotifyArtworkData != nullptr && spotifyArtworkLength > 0 &&
      strcmp(spotifyDisplayedArtUrl, spotifyAlbumArtUrl) == 0) {
    displaySpotifyAlbumArt();
  }
}

void drawSpotifyListHeader(const char* title) {
  lcd.fillScreen(TFT_BLACK);
  lcd.fillRect(0, 0, 240, 36, makeColor565(12, 20, 16));
  lcd.drawRect(4, 5, 48, 26, C_LIME);
  usePixelFont(1);
  lcd.setTextColor(C_LIME, makeColor565(12, 20, 16));
  lcd.setTextDatum(middle_center);
  lcd.drawString("BACK", 28, 18);
  lcd.drawString(title, 116, 18);
  lcd.drawRect(180, 5, 26, 26, C_LIME);
  lcd.drawRect(210, 5, 26, 26, C_LIME);
  lcd.drawString("<", 193, 18);
  lcd.drawString(">", 223, 18);
  lcd.setTextDatum(top_left);
}

void drawSpotifyListRows() {
  const bool playlists = spotifyView == SpotifyView::PLAYLISTS;
  const bool devices = spotifyView == SpotifyView::DEVICES;
  drawSpotifyListHeader(playlists   ? "MY PLAYLISTS"
                        : devices   ? "PLAYBACK DEVICES"
                                    : spotifySelectedPlaylistName);
  const int count = playlists   ? spotifyPlaylistCount
                    : devices   ? spotifyDeviceCount
                                : spotifyTrackCount;
  for (int i = 0; i < SPOTIFY_PAGE_SIZE; i++) {
    const int y = 40 + i * 46;
    lcd.fillRect(5, y, 230, 42,
                 i % 2 == 0 ? makeColor565(15, 15, 15)
                            : makeColor565(24, 24, 24));
    if (i >= count) continue;
    lcd.drawRect(5, y, 230, 42, makeColor565(45, 60, 50));
    usePixelFont(1);
    lcd.setTextColor(TFT_WHITE,
                     i % 2 == 0 ? makeColor565(15, 15, 15)
                                : makeColor565(24, 24, 24));
    lcd.setTextDatum(middle_left);
    if (playlists) {
      lcd.drawString(spotifyPlaylists[i].name, 12, y + 21);
    } else if (devices) {
      lcd.setTextColor(spotifyDevices[i].active ? C_LIME : TFT_WHITE,
                       i % 2 == 0 ? makeColor565(15, 15, 15)
                                  : makeColor565(24, 24, 24));
      lcd.drawString(spotifyDevices[i].name, 12, y + 13);
      lcd.setTextColor(makeColor565(145, 165, 150),
                       i % 2 == 0 ? makeColor565(15, 15, 15)
                                  : makeColor565(24, 24, 24));
      String deviceDetail = spotifyDevices[i].type;
      if (spotifyDevices[i].active) deviceDetail += "  ACTIVE";
      lcd.drawString(deviceDetail, 12, y + 31);
    } else {
      lcd.drawString(spotifyTracks[i].name, 12, y + 13);
      lcd.setTextColor(makeColor565(145, 165, 150),
                       i % 2 == 0 ? makeColor565(15, 15, 15)
                                  : makeColor565(24, 24, 24));
      lcd.drawString(spotifyTracks[i].artist, 12, y + 31);
    }
  }
  lcd.fillRect(0, 272, 240, 22, TFT_BLACK);
  usePixelFont(1);
  lcd.setTextDatum(middle_center);
  lcd.setTextColor(spotifyListMessage[0] ? makeColor565(255, 170, 50)
                                         : makeColor565(100, 120, 105),
                   TFT_BLACK);
  if (spotifyListMessage[0]) {
    lcd.drawString(spotifyListMessage, 120, 283);
  } else {
    if (devices) {
      lcd.drawString(String(count) + " DEVICES", 120, 283);
    } else {
      const int offset =
          playlists ? spotifyPlaylistOffset : spotifyTrackOffset;
      lcd.drawString(String(offset + 1) + "-" + String(offset + count), 120,
                     283);
    }
  }
  lcd.setTextDatum(top_left);
}

bool captureSpotifyPlaylist(SpotifyPlaylist playlist, int index, int numItems,
                            int totalItems) {
  if (index >= SPOTIFY_PAGE_SIZE) return false;
  auto& row = spotifyResult.playlists[index];
  strlcpy(row.id, playlist.id ? playlist.id : "", sizeof(row.id));
  strlcpy(row.name, playlist.name ? playlist.name : "", sizeof(row.name));
  strlcpy(row.uri, playlist.uri ? playlist.uri : "", sizeof(row.uri));
  spotifyResult.playlistCount = index + 1;
  spotifyResult.playlistTotal = totalItems;
  return true;
}

bool captureSpotifyTrack(SpotifyPlaylistTrack track, int index, int numItems,
                         int totalItems) {
  if (index >= SPOTIFY_PAGE_SIZE) return false;
  auto& row = spotifyResult.tracks[index];
  strlcpy(row.name, track.name ? track.name : "", sizeof(row.name));
  strlcpy(row.artist, track.artistName ? track.artistName : "", sizeof(row.artist));
  strlcpy(row.uri, track.uri ? track.uri : "", sizeof(row.uri));
  spotifyResult.trackCount = index + 1;
  spotifyResult.trackTotal = totalItems;
  return true;
}

void captureSpotifyPlayerDetails(PlayerDetails details) {
  spotifyResult.volume = constrain(details.device.volumePercent, 0, 100);
  spotifyResult.volumeValid = true;
}

bool captureSpotifyDevice(SpotifyDevice device, int index, int numDevices) {
  if (index >= SPOTIFY_PAGE_SIZE) return false;
  auto& row = spotifyResult.devices[index];
  strlcpy(row.id, device.id ? device.id : "", sizeof(row.id));
  strlcpy(row.name, device.name ? device.name : "", sizeof(row.name));
  strlcpy(row.type, device.type ? device.type : "", sizeof(row.type));
  row.active = device.isActive;
  row.restricted = device.isRestricted;
  row.volume = device.volumePercent;
  if (device.isActive) {
    spotifyResult.volume = constrain(device.volumePercent, 0, 100);
    spotifyResult.volumeValid = true;
  }
  spotifyResult.deviceCount = index + 1;
  return true;
}

bool spotifyStatusCanRetry(int status) {
  return status < 0 || status == 408 || status == 429 || status >= 500;
}

bool isSpotifyControlJob(SpotifyJob job) {
  return job == SpotifyJob::PREVIOUS || job == SpotifyJob::NEXT ||
         job == SpotifyJob::TOGGLE || job == SpotifyJob::PLAY_TRACK ||
         job == SpotifyJob::VOLUME || job == SpotifyJob::TRANSFER;
}

void spotifyNetworkTask(void* parameter) {
  const SpotifyJob job =
      static_cast<SpotifyJob>(reinterpret_cast<uintptr_t>(parameter));
  int status = -1;
  spotifyApi.lastHttpStatus = 0;
  spotifyApi.requestTimeoutMs =
      isSpotifyControlJob(job)
          ? 1800
          : (job == SpotifyJob::PLAYLISTS || job == SpotifyJob::TRACKS ||
             job == SpotifyJob::DEVICES || job == SpotifyJob::ARTWORK)
                ? 6500
                : 3500;

  for (int attempt = 0; attempt < 2; attempt++) {
    if (spotifyTls.aborted()) break;
    if (job == SpotifyJob::AUTH) {
      spotifyApi.lateInit(SPOTIFY_CLIENT_ID, SPOTIFY_CLIENT_SECRET, SPOTIFY_REFRESH_TOKEN);
      status = spotifyApi.refreshAccessToken() ? 200 : -1;
    } else if (job == SpotifyJob::REFRESH) {
      status = spotifyApi.getCurrentlyPlaying(handleSpotifyCurrentlyPlaying,
                                               SPOTIFY_MARKET);
    } else if (job == SpotifyJob::ARTWORK) {
      uint8_t* image = nullptr;
      int imageLength = 0;
      spotifyTls.setInsecure();
      const bool success =
          spotifyApi.getImage(spotifyRequest.artUrl, &image, &imageLength);
      if (success && image != nullptr && imageLength > 0) {
        spotifyResult.artwork = image;
        spotifyResult.artworkLength = imageLength;
        status = 200;
      } else {
        free(image);
        status = -1;
      }
    } else if (job == SpotifyJob::PLAYLISTS) {
      spotifyResult.playlistCount = 0;
      spotifyResult.playlistTotal = 0;
      status = spotifyApi.getCurrentUserPlaylists(
          captureSpotifyPlaylist, SPOTIFY_PAGE_SIZE, spotifyRequest.playlistOffset);
    } else if (job == SpotifyJob::TRACKS) {
      spotifyResult.trackCount = 0;
      spotifyResult.trackTotal = 0;
      status = spotifyApi.getPlaylistItems(
          spotifyRequest.playlistId, captureSpotifyTrack, SPOTIFY_PAGE_SIZE,
          spotifyRequest.trackOffset);
    } else if (job == SpotifyJob::PREVIOUS) {
      status = spotifyApi.previousTrack() ? 204 : -1;
    } else if (job == SpotifyJob::NEXT) {
      status = spotifyApi.nextTrack() ? 204 : -1;
    } else if (job == SpotifyJob::TOGGLE) {
      const bool success = spotifyRequest.playing ? spotifyApi.play()
                                                       : spotifyApi.pause();
      status = success ? 204 : -1;
    } else if (job == SpotifyJob::PLAY_TRACK) {
      char body[220];
      snprintf(body, sizeof(body),
               "{\"context_uri\":\"%s\",\"offset\":{\"uri\":\"%s\"}}",
               spotifyRequest.playlistUri, spotifyRequest.trackUri);
      status = spotifyApi.playAdvanced(body) ? 204 : -1;
    } else if (job == SpotifyJob::PLAYER_DETAILS) {
      status =
          spotifyApi.getPlayerDetails(captureSpotifyPlayerDetails,
                                      SPOTIFY_MARKET);
    } else if (job == SpotifyJob::VOLUME) {
      status = spotifyApi.setVolume(spotifyRequest.volume) ? 204 : -1;
    } else if (job == SpotifyJob::DEVICES) {
      spotifyResult.deviceCount = 0;
      status = spotifyApi.getDevices(captureSpotifyDevice);
    } else if (job == SpotifyJob::TRANSFER) {
      status =
          spotifyApi.transferPlayback(spotifyRequest.deviceId, true) ? 204 : -1;
    }

    if (spotifyTls.aborted() || job == SpotifyJob::AUTH ||
        isSpotifyControlJob(job) || !spotifyStatusCanRetry(status) ||
        attempt == 1 ||
        isSpotifyControlJob(spotifyQueuedJob)) {
      break;
    }
    spotifyTls.stop();
    vTaskDelay(pdMS_TO_TICKS(350));
  }

  spotifyTls.stop();
  if (isSpotifyControlJob(job) && status == -1 && spotifyApi.lastHttpStatus > 0)
    status = spotifyApi.lastHttpStatus;
  Serial.printf("[spotify] job=%u status=%d elapsed=%lu stack=%u\n",
                static_cast<unsigned>(job), status,
                static_cast<unsigned long>(millis() - spotifyJobStartedAt),
                static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
  spotifyCompletedStatus = status;
  spotifyCompletedJob = job;
  // Release publishes the complete snapshot; main owns all UI and busy flags.
  spotifyJobResultReady.store(true);
  vTaskDelete(nullptr);
}

bool startSpotifyJob(SpotifyJob job) {
  if (currentScreen != Screen::SPOTIFY) return false;
  if (spotifyJobBusy || spotifyJobResultReady) {
    if (isSpotifyControlJob(job) || !isSpotifyControlJob(spotifyQueuedJob)) {
      spotifyQueuedJob = job;
    }
    if (isSpotifyControlJob(job) && !isSpotifyControlJob(spotifyActiveJob)) {
      spotifyTls.cancel();
    }
    return false;
  }
  if (!codexBuddyBleIdle() || dotaRefreshBusy || deadlockAppBusy()) {
    spotifyQueuedJob = job;
    return false;
  }
  if (ESP.getFreeHeap() < 65000 ||
      heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) < 20000) {
    spotifyRetryAt = millis() + 5000;
    spotifyQueuedJob = job;
    Serial.println("[spotify] request deferred: low heap");
    return false;
  }
  spotifyResult = {};
  spotifyRequest.playlistOffset = spotifyPlaylistOffset;
  spotifyRequest.trackOffset = spotifyTrackOffset;
  spotifyRequest.volume = spotifyPendingVolume;
  spotifyRequest.playing = spotifyToggleTargetPlaying;
  strlcpy(spotifyRequest.playlistId, spotifySelectedPlaylistId, sizeof(spotifyRequest.playlistId));
  strlcpy(spotifyRequest.playlistUri, spotifySelectedPlaylistUri, sizeof(spotifyRequest.playlistUri));
  strlcpy(spotifyRequest.trackUri, spotifyPendingTrackUri, sizeof(spotifyRequest.trackUri));
  strlcpy(spotifyRequest.deviceId, spotifyPendingDeviceId, sizeof(spotifyRequest.deviceId));
  strlcpy(spotifyRequest.artUrl, spotifyAlbumArtUrl, sizeof(spotifyRequest.artUrl));
  spotifyTls.beginJob(job == SpotifyJob::AUTH ? 12000 :
                      isSpotifyControlJob(job) ? 6000 : 10000);
  spotifyTls.setInsecure();
  spotifyJobStartedAt = millis();
  spotifyJobBusy = true;
  spotifyActiveJob = job;
  const BaseType_t created = xTaskCreatePinnedToCore(
      spotifyNetworkTask, "spotify-net", 8192,
      reinterpret_cast<void*>(static_cast<uintptr_t>(job)), 1,
      nullptr, 0);
  if (created != pdPASS) {
    spotifyJobBusy = false;
    spotifyActiveJob = SpotifyJob::NONE;
    spotifyCompletedJob = job;
    spotifyCompletedStatus = -99;
    spotifyJobResultReady = true;
    return false;
  }
  return true;
}

void loadSpotifyPlaylists() {
  spotifyView = SpotifyView::PLAYLISTS;
  spotifyPlaylistCount = 0;
  spotifyPlaylistTotal = 0;
  strlcpy(spotifyListMessage, "LOADING...", sizeof(spotifyListMessage));
  drawSpotifyListRows();
  startSpotifyJob(SpotifyJob::PLAYLISTS);
}

void loadSpotifyTracks() {
  spotifyView = SpotifyView::TRACKS;
  spotifyTrackCount = 0;
  spotifyTrackTotal = 0;
  strlcpy(spotifyListMessage, "LOADING...", sizeof(spotifyListMessage));
  drawSpotifyListRows();
  startSpotifyJob(SpotifyJob::TRACKS);
}

void loadSpotifyDevices() {
  spotifyView = SpotifyView::DEVICES;
  spotifyDeviceCount = 0;
  strlcpy(spotifyListMessage, "LOADING DEVICES...",
          sizeof(spotifyListMessage));
  drawSpotifyListRows();
  startSpotifyJob(SpotifyJob::DEVICES);
}

void spotifyAdjustVolume(int delta) {
  if (!spotifyAuthorized) return;
  const int baseVolume = spotifyVolumeKnown ? spotifyVolume : 50;
  spotifyPendingVolume = constrain(baseVolume + delta, 0, 100);
  spotifyVolume = spotifyPendingVolume;
  spotifyVolumeKnown = true;
  startSpotifyJob(SpotifyJob::VOLUME);
}

void spotifySelectDevice(int row) {
  if (row < 0 || row >= spotifyDeviceCount ||
      spotifyDevices[row].restricted || spotifyDevices[row].id[0] == '\0') {
    return;
  }
  strlcpy(spotifyPendingDeviceId, spotifyDevices[row].id,
          sizeof(spotifyPendingDeviceId));
  strlcpy(spotifyListMessage, "SWITCHING DEVICE...",
          sizeof(spotifyListMessage));
  drawSpotifyListRows();
  startSpotifyJob(SpotifyJob::TRANSFER);
}

void drawSpotifySetupMessage(const char* title, const char* detail) {
  lcd.setRotation(0);
  currentScreen = Screen::SPOTIFY;
  lcd.fillScreen(TFT_BLACK);
  usePixelFont(2);
  lcd.setTextColor(makeColor565(30, 215, 96), TFT_BLACK);
  lcd.setTextDatum(middle_center);
  lcd.drawString(title, 120, 118);
  usePixelFont(1);
  lcd.setTextColor(TFT_WHITE, TFT_BLACK);
  lcd.drawString(detail, 120, 154);
  lcd.setTextColor(makeColor565(130, 130, 130), TFT_BLACK);
  lcd.drawString("SWIPE UP FROM BOTTOM TO EXIT", 120, 190);
  lcd.setTextDatum(top_left);
}

int spotifyJpegDraw(JPEGDRAW* block) {
  if (currentScreen != Screen::SPOTIFY ||
      spotifyView != SpotifyView::NOW_PLAYING || block->y >= 92 ||
      block->x >= 92) {
    return 0;
  }
  const bool oldSwap = lcd.getSwapBytes();
  lcd.setSwapBytes(false);
  lcd.pushImage(block->x, block->y, block->iWidth, block->iHeight,
                block->pPixels);
  lcd.setSwapBytes(oldSwap);
  return 1;
}

void displaySpotifyAlbumArt() {
  if (spotifyArtworkData == nullptr || spotifyArtworkLength <= 0) return;
  if (currentScreen == Screen::SPOTIFY &&
      spotifyView == SpotifyView::NOW_PLAYING &&
      spotifyJpeg.openRAM(spotifyArtworkData, spotifyArtworkLength,
                          spotifyJpegDraw)) {
    spotifyJpeg.setPixelType(RGB565_BIG_ENDIAN);
    spotifyJpeg.decode(13, 13, spotifyJpeg.getWidth() >= 160 ? JPEG_SCALE_QUARTER : 0);
    spotifyJpeg.close();
    strlcpy(spotifyDisplayedArtUrl, spotifyAlbumArtUrl,
            sizeof(spotifyDisplayedArtUrl));
    spotifyImageChanged = false;
  }
  // JPEG is consumed synchronously. Do not retain the compressed download
  // across app switches or the next TLS handshake.
  free(spotifyArtworkData);
  spotifyArtworkData = nullptr;
  spotifyArtworkLength = 0;
}

void handleSpotifyCurrentlyPlaying(CurrentlyPlaying playing) {
  if (playing.trackUri == nullptr) return;
  const char* nextTrack = playing.trackName ? playing.trackName : "";
  const char* nextArtist =
      playing.numArtists > 0 && playing.artists[0].artistName
          ? playing.artists[0].artistName
          : "";
  const char* nextAlbum = playing.albumName ? playing.albumName : "";
  strlcpy(spotifyResult.track, nextTrack, sizeof(spotifyResult.track));
  strlcpy(spotifyResult.artist, nextArtist, sizeof(spotifyResult.artist));
  strlcpy(spotifyResult.album, nextAlbum, sizeof(spotifyResult.album));
  if (playing.numImages > 0) {
    // Prefer the medium image (typically 300px), subject to the 32KB cap.
    // The 64px cover fits this screen and avoids holding a 300px JPEG while
    // TLS already consumes most of the non-PSRAM board's available RAM.
    SpotifyImage image = playing.albumImages[playing.numImages - 1];
    if (image.url) strlcpy(spotifyResult.artUrl, image.url, sizeof(spotifyResult.artUrl));
  }
  spotifyResult.metadataValid = true;
  spotifyResult.duration = max(0L, playing.durationMs);
  spotifyResult.progress = max(0L, playing.progressMs);
  spotifyResult.playing = playing.isPlaying;
  spotifyResult.capturedAt = millis();
}

void initializeSpotify() {
  if (diagnosticWifiPaused) return;
  if (!spotifyConfigReady()) {
    drawSpotifySetupMessage("SPOTIFY SETUP REQUIRED",
                            "EDIT spotify_config.h THEN FLASH");
    return;
  }

  drawSpotifySetupMessage("SPOTIFY", "CONNECTING IN BACKGROUND...");
  if (dotaRefreshBusy || deadlockAppBusy()) return;
  if (WiFi.status() != WL_CONNECTED) {
    if (!spotifyConnecting) {
      WiFi.mode(WIFI_STA);
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
      spotifyWifiStartedAt = millis();
      spotifyConnecting = true;
    }
    return;
  }
  spotifyConnecting = false;
  spotifyInitialized = true;
  startSpotifyJob(SpotifyJob::AUTH);
}

void openSpotify() {
  lcd.setRotation(0);
  currentScreen = Screen::SPOTIFY;
  if (!codexBuddyBleIdle()) {
    spotifyDeferredOpen = true;
    drawSpotifySetupMessage("SPOTIFY", "PREPARING NETWORK...");
    return;
  }
  spotifyDeferredOpen = false;
  if (!spotifyInitialized || !spotifyAuthorized || WiFi.status() != WL_CONNECTED) {
    initializeSpotify();
  } else {
    drawSpotifyBase();
    spotifyRequestDueMs = 0;
  }
}

void applySpotifyJobResult() {
  if (!spotifyJobResultReady) return;
  const SpotifyJob job = spotifyCompletedJob;
  const int status = spotifyCompletedStatus;
  spotifyJobResultReady = false;
  spotifyCompletedJob = SpotifyJob::NONE;
  spotifyJobBusy = false;
  spotifyActiveJob = SpotifyJob::NONE;
  const bool spotifyVisible = currentScreen == Screen::SPOTIFY;

  if (job == SpotifyJob::AUTH) {
    spotifyAuthorized = status == 200;
    spotifyRetryAt = spotifyAuthorized ? 0 : millis() + 10000;
    if (spotifyVisible) {
      if (spotifyAuthorized) {
        drawSpotifyBase();
        spotifyRequestDueMs = 0;
      } else {
        drawSpotifySetupMessage("SPOTIFY AUTH FAILED", "WILL RETRY IN BACKGROUND");
      }
    }
  } else if (job == SpotifyJob::REFRESH) {
    if (status == 200) {
      if (spotifyResult.metadataValid) {
        spotifyMetadataChanged = strcmp(spotifyTrackName, spotifyResult.track) != 0 ||
            strcmp(spotifyArtistName, spotifyResult.artist) != 0 ||
            strcmp(spotifyAlbumName, spotifyResult.album) != 0;
        Serial.printf("[spotify] playback snapshot track-changed=%u progress=%lu\n",
                      spotifyMetadataChanged, static_cast<unsigned long>(spotifyResult.progress));
        spotifyPlaybackChanged = spotifyIsPlaying != spotifyResult.playing;
        strlcpy(spotifyTrackName, spotifyResult.track, sizeof(spotifyTrackName));
        strlcpy(spotifyArtistName, spotifyResult.artist, sizeof(spotifyArtistName));
        strlcpy(spotifyAlbumName, spotifyResult.album, sizeof(spotifyAlbumName));
        if (spotifyResult.artUrl[0] && strcmp(spotifyDisplayedArtUrl, spotifyResult.artUrl) != 0) {
          strlcpy(spotifyAlbumArtUrl, spotifyResult.artUrl, sizeof(spotifyAlbumArtUrl));
          spotifyImageChanged = true;
        }
        spotifySongDurationMs = spotifyResult.duration;
        spotifyPausedProgressMs = spotifyResult.progress;
        spotifyIsPlaying = spotifyResult.playing;
        spotifySongStartMs = spotifyIsPlaying ? spotifyResult.capturedAt - spotifyResult.progress : 0;
      }
      if (spotifyVisible && spotifyView == SpotifyView::NOW_PLAYING) {
        if (spotifyMetadataChanged) drawSpotifyText();
        drawSpotifyProgress(spotifyPausedProgressMs, spotifySongDurationMs);
        if (spotifyPlaybackChanged) drawSpotifyControls();
      }
      spotifyMetadataChanged = false;
      spotifyPlaybackChanged = false;
      if (spotifyVisible && spotifyQueuedJob == SpotifyJob::NONE) {
        if (!spotifyVolumeKnown) {
          spotifyQueuedJob = SpotifyJob::PLAYER_DETAILS;
        } else if (spotifyImageChanged) {
          spotifyQueuedJob = SpotifyJob::ARTWORK;
        }
      }
    } else if (status == 204) {
      const bool displayChanged =
          spotifyTrackName[0] != '\0' || spotifyIsPlaying;
      spotifySongStartMs = 0;
      spotifyTrackName[0] = '\0';
      spotifyArtistName[0] = '\0';
      spotifyAlbumName[0] = '\0';
      spotifyIsPlaying = false;
      spotifyPausedProgressMs = 0;
      if (displayChanged && spotifyVisible &&
          spotifyView == SpotifyView::NOW_PLAYING) {
        drawSpotifyText();
        drawSpotifyControls();
      }
    }
  } else if (job == SpotifyJob::ARTWORK) {
    spotifyArtworkData = spotifyResult.artwork;
    spotifyArtworkLength = spotifyResult.artworkLength;
    spotifyResult.artwork = nullptr;
    if (status == 200) {
      // Only associate the downloaded pixels with the URL of that request.
      if (strcmp(spotifyRequest.artUrl, spotifyAlbumArtUrl) == 0) {
        displaySpotifyAlbumArt();
      } else {
        free(spotifyArtworkData);
        spotifyArtworkData = nullptr;
        spotifyArtworkLength = 0;
      }
    } else if (spotifyArtworkData != nullptr) {
      free(spotifyArtworkData);
      spotifyArtworkData = nullptr;
      spotifyArtworkLength = 0;
    }
  } else if (job == SpotifyJob::PLAYLISTS) {
    memcpy(spotifyPlaylists, spotifyResult.playlists, sizeof(spotifyPlaylists));
    spotifyPlaylistCount = spotifyResult.playlistCount;
    spotifyPlaylistTotal = spotifyResult.playlistTotal;
    spotifyListMessage[0] = '\0';
    if (status == 401 || status == 403) {
      snprintf(spotifyListMessage, sizeof(spotifyListMessage),
               "PLAYLIST DENIED [%d]", status);
    } else if (status != 200) {
      snprintf(spotifyListMessage, sizeof(spotifyListMessage),
               "PLAYLIST ERROR [%d]", status);
    } else if (spotifyPlaylistCount == 0) {
      strlcpy(spotifyListMessage, "NO PLAYLISTS",
              sizeof(spotifyListMessage));
    }
    if (spotifyVisible && spotifyView == SpotifyView::PLAYLISTS) {
      drawSpotifyListRows();
    }
  } else if (job == SpotifyJob::TRACKS) {
    memcpy(spotifyTracks, spotifyResult.tracks, sizeof(spotifyTracks));
    spotifyTrackCount = spotifyResult.trackCount;
    spotifyTrackTotal = spotifyResult.trackTotal;
    spotifyListMessage[0] = '\0';
    if (status == 403) {
      strlcpy(spotifyListMessage, "PLAYLIST NOT AVAILABLE",
              sizeof(spotifyListMessage));
    } else if (status != 200) {
      snprintf(spotifyListMessage, sizeof(spotifyListMessage),
               "TRACK ERROR [%d]", status);
    } else if (spotifyTrackCount == 0) {
      strlcpy(spotifyListMessage, "NO TRACKS",
              sizeof(spotifyListMessage));
    }
    if (spotifyVisible && spotifyView == SpotifyView::TRACKS) {
      drawSpotifyListRows();
    }
  } else if (job == SpotifyJob::PREVIOUS || job == SpotifyJob::NEXT) {
    if (spotifyVisible && spotifyView == SpotifyView::NOW_PLAYING) drawSpotifyControls();
    spotifyRequestDueMs = 0;
  } else if (job == SpotifyJob::TOGGLE) {
    if (status == 204) {
      if (!spotifyRequest.playing) {
        spotifyPausedProgressMs =
            spotifySongStartMs == 0
                ? spotifyPausedProgressMs
                : min(millis() - spotifySongStartMs, spotifySongDurationMs);
        spotifySongStartMs = 0;
      } else {
        spotifySongStartMs = millis() - spotifyPausedProgressMs;
      }
      spotifyIsPlaying = spotifyRequest.playing;
    }
    if (spotifyVisible && spotifyView == SpotifyView::NOW_PLAYING) {
      drawSpotifyControls();
    }
    spotifyRequestDueMs = 0;
  } else if (job == SpotifyJob::PLAY_TRACK) {
    if (status == 204) {
      spotifyIsPlaying = true;
      if (spotifyVisible) drawSpotifyBase();
      spotifyRequestDueMs = 0;
    } else {
      snprintf(spotifyListMessage, sizeof(spotifyListMessage),
               "PLAY ERROR [%d]", status);
      if (spotifyVisible && spotifyView == SpotifyView::TRACKS) {
        drawSpotifyListRows();
      }
    }
  } else if (job == SpotifyJob::PLAYER_DETAILS) {
    if (spotifyResult.volumeValid && spotifyQueuedJob != SpotifyJob::VOLUME) {
      spotifyVolume = spotifyResult.volume;
      spotifyVolumeKnown = true;
    }
    if (spotifyVisible && spotifyImageChanged && spotifyQueuedJob == SpotifyJob::NONE) {
      spotifyQueuedJob = SpotifyJob::ARTWORK;
    }
  } else if (job == SpotifyJob::VOLUME) {
    if (status != 204) spotifyVolumeKnown = false;
  } else if (job == SpotifyJob::DEVICES) {
    memcpy(spotifyDevices, spotifyResult.devices, sizeof(spotifyDevices));
    spotifyDeviceCount = spotifyResult.deviceCount;
    if (spotifyResult.volumeValid) {
      spotifyVolume = spotifyResult.volume;
      spotifyVolumeKnown = true;
    }
    spotifyListMessage[0] = '\0';
    if (status != 200) {
      snprintf(spotifyListMessage, sizeof(spotifyListMessage),
               "DEVICE ERROR [%d]", status);
    } else if (spotifyDeviceCount == 0) {
      strlcpy(spotifyListMessage, "NO ACTIVE DEVICES",
              sizeof(spotifyListMessage));
    }
    if (spotifyVisible && spotifyView == SpotifyView::DEVICES) {
      drawSpotifyListRows();
    }
  } else if (job == SpotifyJob::TRANSFER) {
    if (status == 204) {
      spotifyVolumeKnown = false;
      if (spotifyVisible) drawSpotifyBase();
      spotifyRequestDueMs = 0;
    } else {
      snprintf(spotifyListMessage, sizeof(spotifyListMessage),
               "DEVICE SWITCH ERROR [%d]", status);
      if (spotifyVisible && spotifyView == SpotifyView::DEVICES) {
        drawSpotifyListRows();
      }
    }
  }

  if (!spotifyVisible) spotifyQueuedJob = SpotifyJob::NONE;
  if (spotifyVisible && !spotifyJobBusy && spotifyQueuedJob != SpotifyJob::NONE) {
    const SpotifyJob queued = spotifyQueuedJob;
    spotifyQueuedJob = SpotifyJob::NONE;
    startSpotifyJob(queued);
  }
}

void spotifyLoop() {
  applySpotifyJobResult();
  if (currentScreen != Screen::SPOTIFY) return;
  if (WiFi.status() != WL_CONNECTED && codexBuddyBleIdle() && !dotaRefreshBusy) {
    if (static_cast<int32_t>(millis() - spotifyRetryAt) >= 0) {
      if (spotifyConnecting && millis() - spotifyWifiStartedAt >= 12000) {
        spotifyConnecting = false;
        spotifyRetryAt = millis() + 10000;
        drawSpotifySetupMessage("WIFI NOT CONNECTED", "YOU CAN STILL EXIT THIS APP");
      } else if (!spotifyConnecting) {
        initializeSpotify();
      }
    }
    return;
  }
  if (spotifyConnecting && WiFi.status() == WL_CONNECTED) {
    spotifyConnecting = false;
    if (spotifyAuthorized) { drawSpotifyBase(); spotifyRequestDueMs = 0; }
  }
  if (!spotifyJobBusy && spotifyQueuedJob != SpotifyJob::NONE &&
      codexBuddyBleIdle() && !dotaRefreshBusy &&
      static_cast<int32_t>(millis() - spotifyRetryAt) >= 0) {
    const SpotifyJob queued = spotifyQueuedJob;
    spotifyQueuedJob = SpotifyJob::NONE;
    startSpotifyJob(queued);
  }
  if (currentScreen == Screen::SPOTIFY && spotifyDeferredOpen &&
      codexBuddyBleIdle()) {
    openSpotify();
    return;
  }
  if (!spotifyAuthorized && !spotifyJobBusy && !spotifyJobResultReady &&
      !spotifyDeferredOpen && static_cast<int32_t>(millis() - spotifyRetryAt) >= 0) {
    if (spotifyConnecting && millis() - spotifyWifiStartedAt >= 12000) {
      spotifyConnecting = false;
      spotifyRetryAt = millis() + 10000;
      drawSpotifySetupMessage("WIFI NOT CONNECTED", "YOU CAN STILL EXIT THIS APP");
    } else if (!spotifyConnecting || WiFi.status() == WL_CONNECTED) {
      initializeSpotify();
    }
    return;
  }
  if (!spotifyAuthorized || currentScreen != Screen::SPOTIFY ||
      spotifyView != SpotifyView::NOW_PLAYING) {
    return;
  }
  if (static_cast<int32_t>(millis() - spotifyRequestDueMs) >= 0 &&
      static_cast<int32_t>(millis() - spotifyRetryAt) >= 0 && !spotifyJobBusy &&
      !spotifyJobResultReady && spotifyQueuedJob == SpotifyJob::NONE) {
    startSpotifyJob(SpotifyJob::REFRESH);
    spotifyRequestDueMs = millis() + 10000;
  }
  if (spotifySongStartMs != 0 && static_cast<int32_t>(millis() - spotifyProgressDueMs) >= 0) {
    const uint32_t progress =
        min(millis() - spotifySongStartMs, spotifySongDurationMs);
    drawSpotifyProgress(progress, spotifySongDurationMs);
    spotifyProgressDueMs = millis() + 500;
  }
}

void spotifyNavigate(bool next) {
  if (!spotifyAuthorized) return;
  startSpotifyJob(next ? SpotifyJob::NEXT : SpotifyJob::PREVIOUS);
  if (spotifyView == SpotifyView::NOW_PLAYING) {
    const uint16_t playerBg = makeColor565(8, 11, 10);
    lcd.fillRect(10, 116, 82, 16, playerBg);
    usePixelFont(1);
    lcd.setTextDatum(middle_center);
    lcd.setTextColor(C_AMBER, playerBg);
    lcd.drawString(next ? "NEXT..." : "PREV...", 51, 124);
    lcd.setTextDatum(top_left);
  }
}

void spotifyTogglePlayback() {
  if (!spotifyAuthorized) return;
  spotifyToggleTargetPlaying = !spotifyIsPlaying;
  spotifyIsPlaying = spotifyToggleTargetPlaying;
  if (currentScreen == Screen::SPOTIFY &&
      spotifyView == SpotifyView::NOW_PLAYING) {
    drawSpotifyControls();
  }
  startSpotifyJob(SpotifyJob::TOGGLE);
}

void spotifyOpenPlaylist(int row) {
  if (row < 0 || row >= spotifyPlaylistCount) return;
  strlcpy(spotifySelectedPlaylistId, spotifyPlaylists[row].id,
          sizeof(spotifySelectedPlaylistId));
  strlcpy(spotifySelectedPlaylistName, spotifyPlaylists[row].name,
          sizeof(spotifySelectedPlaylistName));
  strlcpy(spotifySelectedPlaylistUri, spotifyPlaylists[row].uri,
          sizeof(spotifySelectedPlaylistUri));
  spotifyTrackOffset = 0;
  loadSpotifyTracks();
}

void spotifyPlayTrack(int row) {
  if (row < 0 || row >= spotifyTrackCount) return;
  strlcpy(spotifyPendingTrackUri, spotifyTracks[row].uri,
          sizeof(spotifyPendingTrackUri));
  strlcpy(spotifyListMessage, "STARTING TRACK...",
          sizeof(spotifyListMessage));
  drawSpotifyListRows();
  startSpotifyJob(SpotifyJob::PLAY_TRACK);
}

void spotifyListTap(uint16_t x, uint16_t y) {
  if (y <= 36) {
    if (x <= 55) {
      if (spotifyView == SpotifyView::TRACKS) {
        spotifyView = SpotifyView::PLAYLISTS;
        spotifyListMessage[0] = '\0';
        drawSpotifyListRows();
      } else {
        drawSpotifyBase();
        spotifyRequestDueMs = 0;
      }
    } else if (x >= 178 && x <= 207) {
      if (spotifyView == SpotifyView::PLAYLISTS &&
          spotifyPlaylistOffset >= SPOTIFY_PAGE_SIZE) {
        spotifyPlaylistOffset -= SPOTIFY_PAGE_SIZE;
        loadSpotifyPlaylists();
      } else if (spotifyView == SpotifyView::TRACKS &&
                 spotifyTrackOffset >= SPOTIFY_PAGE_SIZE) {
        spotifyTrackOffset -= SPOTIFY_PAGE_SIZE;
        loadSpotifyTracks();
      }
    } else if (x >= 208) {
      if (spotifyView == SpotifyView::PLAYLISTS &&
          spotifyPlaylistCount == SPOTIFY_PAGE_SIZE &&
          spotifyPlaylistOffset + spotifyPlaylistCount <
              spotifyPlaylistTotal) {
        spotifyPlaylistOffset += SPOTIFY_PAGE_SIZE;
        loadSpotifyPlaylists();
      } else if (spotifyView == SpotifyView::TRACKS &&
                 spotifyTrackCount == SPOTIFY_PAGE_SIZE &&
                 spotifyTrackOffset + spotifyTrackCount <
                     spotifyTrackTotal) {
        spotifyTrackOffset += SPOTIFY_PAGE_SIZE;
        loadSpotifyTracks();
      }
    }
    return;
  }
  if (y >= 40 && y < 270) {
    const int row = (y - 40) / 46;
    if (spotifyView == SpotifyView::PLAYLISTS) {
      spotifyOpenPlaylist(row);
    } else if (spotifyView == SpotifyView::DEVICES) {
      spotifySelectDevice(row);
    } else {
      spotifyPlayTrack(row);
    }
  }
}

bool configReady() {
  return strcmp(WIFI_SSID, "YOUR_WIFI_SSID") != 0 &&
         strcmp(DOTA_ACCOUNT_ID, "YOUR_OPENDOTA_ACCOUNT_ID") != 0 &&
         strlen(WIFI_SSID) > 0 && strlen(DOTA_ACCOUNT_ID) > 0;
}

bool connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return true;
  refreshStatus = RefreshStatus::CONNECTING;
  forceDotaRedraw = true;
  const uint32_t started = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (dotaCancelled || millis() - started > 12000) return false;
    vTaskDelay(pdMS_TO_TICKS(250));
  }
  return true;
}

bool fetchMatches(MatchInfo* output, size_t& outputCount,
                  RefreshStatus& failure) {
  refreshStatus = RefreshStatus::LOADING;
  forceDotaRedraw = true;
  const String url = "https://api.opendota.com/api/players/" +
                     String(DOTA_ACCOUNT_ID) + "/recentMatches";
  BoundedSpotifyClient client;
  client.beginJob(10000);
  client.setInsecure();
  HTTPClient http;
  http.useHTTP10(true);
  http.setTimeout(3000);
  http.setConnectTimeout(3000);
  http.setReuse(false);
  if (!http.begin(client, url)) {
    failure = RefreshStatus::HTTP_FAILED;
    return false;
  }
  http.addHeader("Accept", "application/json");
  http.addHeader("Accept-Encoding", "identity");
  http.addHeader("User-Agent", "ESP32-Dota-Display/2.0");
  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
    failure = RefreshStatus::HTTP_FAILED;
    return false;
  }
  // Parse only the eight fields actually rendered. Avoid keeping a full
  // response String and an unfiltered JSON document in RAM simultaneously.
  JsonDocument filter;
  const char* fields[] = {"hero_id", "kills", "deaths", "assists", "duration",
                          "player_slot", "radiant_win"};
  for (const char* field : fields) filter[0][field] = true;
  JsonDocument doc;
  const DeserializationError error = deserializeJson(
      doc, http.getStream(), DeserializationOption::Filter(filter),
      DeserializationOption::NestingLimit(8));
  http.end();
  if (error || !doc.is<JsonArray>()) {
    failure = RefreshStatus::JSON_FAILED;
    return false;
  }

  JsonArray matches = doc.as<JsonArray>();
  outputCount = min(static_cast<size_t>(10), matches.size());
  for (size_t i = 0; i < outputCount; ++i) {
    JsonObject match = matches[i].as<JsonObject>();
    const int slot = match["player_slot"] | 0;
    const bool radiantWin = match["radiant_win"] | false;
    const bool radiant = slot < 128;
    output[i].heroId = match["hero_id"] | 0;
    output[i].kills = match["kills"] | 0;
    output[i].deaths = match["deaths"] | 0;
    output[i].assists = match["assists"] | 0;
    output[i].durationMinutes = (match["duration"] | 0) / 60;
    output[i].won = (radiant && radiantWin) || (!radiant && !radiantWin);
  }
  return true;
}

void refreshWorker(void*) {
  if (!configReady()) {
    refreshStatus = RefreshStatus::NOT_CONFIGURED;
  } else {
    RefreshStatus failure = RefreshStatus::HTTP_FAILED;
    MatchInfo local[10];
    size_t count = 0;
    if (!connectWiFi()) {
      refreshStatus = RefreshStatus::WIFI_FAILED;
    } else if (dotaCancelled || !fetchMatches(local, count, failure)) {
      refreshStatus = failure;
    } else {
      portENTER_CRITICAL(&matchMux);
      stagedMatchCount = count;
      for (size_t i = 0; i < count; ++i) stagedMatches[i] = local[i];
      stagedDataReady = true;
      portEXIT_CRITICAL(&matchMux);
      refreshStatus = RefreshStatus::SUCCESS;
    }
  }
  nextAttemptIntervalMs =
      refreshStatus == RefreshStatus::SUCCESS ? REFRESH_INTERVAL_MS
                                               : RETRY_INTERVAL_MS;
  forceDotaRedraw = true;
  dotaRefreshBusy = false;
  vTaskDelete(nullptr);
}

void startDotaRefresh() {
  if (diagnosticWifiPaused) return;
  if (dotaRefreshBusy || spotifyJobBusy || spotifyJobResultReady || deadlockAppBusy() || !codexBuddyBleIdle()) return;
  if (ESP.getFreeHeap() < 65000 || heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) < 20000) return;
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  }
  dotaCancelled = false;
  dotaRefreshBusy = true;
  dotaRefreshStartedAt = millis();
  lastAttemptMs = millis();
  if (xTaskCreatePinnedToCore(refreshWorker, "dota-refresh", 8192, nullptr, 1,
                              nullptr, 0) != pdPASS) {
    dotaRefreshBusy = false;
    refreshStatus = RefreshStatus::HTTP_FAILED;
    forceDotaRedraw = true;
  }
}

void acceptStagedMatches() {
  if (!stagedDataReady) return;
  portENTER_CRITICAL(&matchMux);
  recentMatchCount = stagedMatchCount;
  for (size_t i = 0; i < recentMatchCount; ++i) {
    recentMatches[i] = stagedMatches[i];
  }
  stagedDataReady = false;
  portEXIT_CRITICAL(&matchMux);
  forceDotaRedraw = true;
}

void setBrightnessFromX(uint16_t x) {
  const uint8_t newBrightness =
      constrain(map(x, 74, 214, 10, 100), 10, 100);
  if (newBrightness == brightness) return;
  brightness = newBrightness;
  lcd.setBrightness(map(brightness, 0, 100, 0, 255));
  drawBrightnessControl();
}

void launchHomeApp(uint16_t x, uint16_t y) {
  const int appIndex = homeAppAt(x, y);
  if (appIndex < 0) return;
  const Screen app = HOME_APPS[appIndex].screen;
  if (app == Screen::SETTINGS) {
    drawSettings();
  } else if (app == Screen::DOTA) {
    drawDota();
    if (!dotaRefreshBusy &&
        (refreshStatus == RefreshStatus::IDLE ||
         millis() - lastAttemptMs >= nextAttemptIntervalMs)) {
      startDotaRefresh();
    }
  } else if (app == Screen::SPOTIFY) {
    openSpotify();
  } else if (app == Screen::CODEX_BUDDY) {
    openCodexBuddy();
  } else if (app == Screen::SYNTH) {
    currentScreen = Screen::SYNTH;
    synthAppBegin(&lcd);
  } else if (app == Screen::DEADLOCK) {
    currentScreen = Screen::DEADLOCK;
    deadlockAppBegin(&lcd, scaledHeroIcon, &spotifyTls);
  } else {
    drawPlaceholder(2);
  }
}

void handleTap(uint16_t x, uint16_t y) {
  switch (currentScreen) {
    case Screen::HOME:
      if (y >= HOME_NAV_Y && y <= 305) {
        if (x < 100 && homePage > 0) {
          --homePage;
          drawHome();
        } else if (x > 140 && homePage + 1 < HOME_PAGE_COUNT) {
          ++homePage;
          drawHome();
        }
      } else {
        launchHomeApp(x, y);
      }
      break;
    case Screen::SETTINGS:
      if (y >= 96 && y <= 140 && x >= 65 && x <= 225) {
        setBrightnessFromX(x);
      } else if (y >= 176 && y <= 247) {
        language = x < 120 ? Language::ENGLISH : Language::CHINESE;
        preferences.putUChar("language", static_cast<uint8_t>(language));
        drawSettings();
      }
      break;
    case Screen::DOTA:
      // The app owns no system navigation or shell controls.
      break;
    case Screen::DEADLOCK:
      deadlockAppTap(x, y);
      break;
    case Screen::SPOTIFY:
      if (spotifyView == SpotifyView::NOW_PLAYING) {
        const int centerDx = static_cast<int>(x) - 128;
        const int centerDy = static_cast<int>(y) - 245;
        if (x >= 5 && x <= 52 && y >= 274 && y <= 310) {
          spotifyPlaylistOffset = 0;
          loadSpotifyPlaylists();
        } else if (centerDx * centerDx + centerDy * centerDy <= 625) {
          spotifyTogglePlayback();
        } else if (x >= 68 && x <= 108 && y >= 224 && y <= 267) {
          spotifyNavigate(false);
        } else if (x >= 148 && x <= 190 && y >= 224 && y <= 267) {
          spotifyNavigate(true);
        } else if (x >= 107 && x <= 149 && y >= 186 && y <= 224) {
          spotifyAdjustVolume(10);
        } else if (x >= 107 && x <= 149 && y >= 267 && y <= 305) {
          spotifyAdjustVolume(-10);
        }
      } else {
        spotifyListTap(x, y);
      }
      break;
    case Screen::SYNTH:
      synthAppTap(x, y);
      break;
    default:
      break;
  }
}

bool handleSpotifyReleaseGesture(uint32_t held) {
  return false;
}

void pollTouch() {
  uint16_t x = 0;
  uint16_t y = 0;
  const bool pressed = lcd.getTouch(&x, &y);

  if (pressed && !touchDown) {
    touchDown = true;
    gestureConsumed = false;
    touchStartX = touchLastX = x;
    touchStartY = touchLastY = y;
    touchStartedAt = millis();
    if (currentScreen == Screen::HOME) {
      homePressedApp = homeAppAt(x, y);
      drawHomeAppFeedback(homePressedApp, true);
    }
    if (currentScreen == Screen::SYNTH) {
      synthAppTouch(x, y);
    }
  } else if (pressed && touchDown) {
    touchLastX = x;
    touchLastY = y;

    if (currentScreen == Screen::HOME && homePressedApp >= 0 &&
        (abs(static_cast<int>(x) - touchStartX) >= 18 ||
         abs(static_cast<int>(y) - touchStartY) >= 18)) {
      drawHomeAppFeedback(homePressedApp, false);
      homePressedApp = -1;
    }

    if (currentScreen == Screen::SETTINGS && touchStartY >= 96 &&
        touchStartY <= 140 && y >= 90 && y <= 150) {
      setBrightnessFromX(x);
      gestureConsumed = true;
    }
    if (currentScreen == Screen::SYNTH) {
      synthAppTouch(x, y);
    }

  } else if (!pressed && touchDown) {
    const int dx = abs(static_cast<int>(touchLastX) -
                       static_cast<int>(touchStartX));
    const int dy = abs(static_cast<int>(touchLastY) -
                       static_cast<int>(touchStartY));
    const uint32_t held = millis() - touchStartedAt;
    if (currentScreen == Screen::HOME) {
      drawHomeAppFeedback(homePressedApp, false);
      homePressedApp = -1;
    }
    const int homeGestureStartY =
        max(0, static_cast<int>(lcd.height()) - 18);
    const bool homeGesture =
        currentScreen != Screen::HOME && touchStartY >= homeGestureStartY &&
        static_cast<int>(touchStartY) - static_cast<int>(touchLastY) >= 75 &&
        dx <= 45;
    if (currentScreen == Screen::SYNTH) {
      synthAppRelease();
    }
    const bool pageGesture = currentScreen == Screen::HOME &&
                             dx >= 45 && dx > dy * 2 && held < 1500;
    if (!gestureConsumed && pageGesture) {
      gestureConsumed = true;
      if (touchLastX < touchStartX && homePage + 1 < HOME_PAGE_COUNT) {
        ++homePage;
        drawHome();
      } else if (touchLastX > touchStartX && homePage > 0) {
        --homePage;
        drawHome();
      }
    } else if (!gestureConsumed && homeGesture) {
      gestureConsumed = true;
      drawHome();
    } else if (!gestureConsumed && handleSpotifyReleaseGesture(held)) {
      gestureConsumed = true;
    } else if (!gestureConsumed && dx < 18 && dy < 18 && held < 700) {
      handleTap(touchLastX, touchLastY);
    }
    if (currentScreen == Screen::SETTINGS && touchStartY >= 96 &&
        touchStartY <= 140) {
      // Commit once on release instead of wearing NVS during every drag frame.
      preferences.putUChar("brightness", brightness);
    }
    touchDown = false;
  }
}

void configureTouchCalibration() {
  constexpr const char* key = "touch-cal";
  uint16_t calibration[8];
  if (preferences.getBytesLength(key) == sizeof(calibration)) {
    preferences.getBytes(key, calibration, sizeof(calibration));
    lcd.setTouchCalibrate(calibration);
    Serial.println("Touch calibration loaded from NVS");
    return;
  }

  lcd.fillScreen(C_BG);
  usePixelFont(2);
  lcd.setTextColor(C_LIME, C_BG);
  lcd.setCursor(17, 92);
  lcd.print("TOUCH CALIBRATION");
  usePixelFont(1);
  lcd.setTextColor(C_TEXT, C_BG);
  lcd.setCursor(30, 128);
  lcd.print("TAP EACH OF 4 TARGETS");
  lcd.setTextColor(C_MUTED, C_BG);
  lcd.setCursor(45, 150);
  lcd.print("USE A FIRM, SHORT TAP");
  delay(1800);

  lcd.calibrateTouch(calibration, C_LIME, C_BG, 14);
  preferences.putBytes(key, calibration, sizeof(calibration));
  lcd.setTouchCalibrate(calibration);
  Serial.printf(
      "Touch calibrated: %u,%u %u,%u %u,%u %u,%u\n",
      calibration[0], calibration[1], calibration[2], calibration[3],
      calibration[4], calibration[5], calibration[6], calibration[7]);
}

void beginCydPreferences() {
  // One-time migration from firmware versions before the CYD OS redesign.
  const char legacyNamespace[] = {
      't', 'a', 'c', 't', 'i', 'c', 'a', 'l', '-', 'o', 's', '\0'};
  Preferences legacy;
  legacy.begin(legacyNamespace, true);
  const bool hasBrightness = legacy.isKey("brightness");
  const bool hasLanguage = legacy.isKey("language");
  const size_t calibrationLength = legacy.getBytesLength("touch-cal");
  const uint8_t legacyBrightness =
      hasBrightness ? legacy.getUChar("brightness", 78) : 78;
  const uint8_t legacyLanguage =
      hasLanguage ? legacy.getUChar("language", 0) : 0;
  uint16_t legacyCalibration[8] = {};
  if (calibrationLength == sizeof(legacyCalibration)) {
    legacy.getBytes("touch-cal", legacyCalibration,
                    sizeof(legacyCalibration));
  }
  legacy.end();

  preferences.begin("cyd-os", false);
  if (!preferences.isKey("brightness") && hasBrightness) {
    preferences.putUChar("brightness", legacyBrightness);
  }
  if (!preferences.isKey("language") && hasLanguage) {
    preferences.putUChar("language", legacyLanguage);
  }
  if (preferences.getBytesLength("touch-cal") == 0 &&
      calibrationLength == sizeof(legacyCalibration)) {
    preferences.putBytes("touch-cal", legacyCalibration,
                         sizeof(legacyCalibration));
  }
}

void setup() {
  Serial.begin(115200);
  // All current apps use Wi-Fi or BLE, never Bluetooth Classic. Release its
  // reserved RAM at boot, rather than waiting for the first Buddy launch.
  // NimBLE releases this same unused region during its own initialization.
  const uint32_t heapBeforeBtRelease = ESP.getFreeHeap();
  const esp_err_t btRelease = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
  Serial.printf("[memory] Classic BT release=%d reclaimed=%u\n", btRelease,
                ESP.getFreeHeap() - heapBeforeBtRelease);
  lcd.init();
  lcd.setRotation(0);
  lcd.setColorDepth(16);

  beginCydPreferences();
  brightness = constrain(preferences.getUChar("brightness", 78), 10, 100);
  language = static_cast<Language>(
      min(preferences.getUChar("language", 0), static_cast<uint8_t>(1)));
  lcd.setBrightness(map(brightness, 0, 100, 0, 255));

  configureTouchCalibration();
  drawHome();
  systemHealthBegin();
  Serial.println("CYD OS ready: ILI9341 240x320 + XPT2046");
}

// Bounded serial diagnostics for reproducible on-device stress testing.
// No credential dumps or arbitrary command execution.
void systemDiagnosticsLoop() {
  static char command[64];
  static size_t used = 0;
  static int stressRemaining = 0;
  static int stressStep = 0;
  static uint32_t stressDueAt = 0;
  for (int i = 0; i < 32 && Serial.available(); ++i) {
    const char c = Serial.read();
    if (c == '\r') continue;
    if (c != '\n') {
      if (used + 1 < sizeof(command)) command[used++] = c;
      else used = 0;
      continue;
    }
    command[used] = '\0';
    used = 0;
    if (strcmp(command, "diag") == 0) {
      systemHealthReport(static_cast<uint8_t>(currentScreen), spotifyJobBusy || dotaRefreshBusy);
      Serial.printf("[diag] spotify=%u job=%u queue=%u ble-idle=%u dota=%u\n",
                    spotifyAuthorized, static_cast<unsigned>(spotifyActiveJob),
                    static_cast<unsigned>(spotifyQueuedJob), codexBuddyBleIdle(), dotaRefreshBusy.load());
      deadlockAppDiagnostics();
    } else if (strcmp(command, "selftest") == 0) {
      runNetworkSelfTest();
    } else if (strncmp(command, "stress ", 7) == 0) {
      stressRemaining = constrain(atoi(command + 7), 0, 30) * 12;
      stressStep = 0;
      stressDueAt = millis();
      Serial.printf("[stress] scheduled transitions=%d\n", stressRemaining);
    } else if (strcmp(command, "home") == 0) {
      stressRemaining = 0;
      drawHome();
    } else if (strcmp(command, "spotify") == 0) {
      drawHome();
      openSpotify();
    } else if (strcmp(command, "deadlock") == 0) {
      drawHome();
      currentScreen = Screen::DEADLOCK;
      deadlockAppBegin(&lcd, scaledHeroIcon, &spotifyTls);
    } else if (strcmp(command, "deadlock-next") == 0 && currentScreen == Screen::DEADLOCK) {
      deadlockAppTap(210, 300);
    } else if (strcmp(command, "deadlock-prev") == 0 && currentScreen == Screen::DEADLOCK) {
      deadlockAppTap(25, 300);
    } else if (strcmp(command, "next") == 0 && currentScreen == Screen::SPOTIFY) {
      spotifyNavigate(true);
    } else if (strcmp(command, "playlists") == 0 && currentScreen == Screen::SPOTIFY && spotifyAuthorized) {
      loadSpotifyPlaylists();
    } else if (strcmp(command, "wifi-off") == 0 && !spotifyJobBusy && !dotaRefreshBusy) {
      diagnosticWifiPaused = true;
      WiFi.setAutoReconnect(false);
      WiFi.mode(WIFI_OFF);
      Serial.println("[diag] Wi-Fi paused for offline test");
    } else if (strcmp(command, "wifi-on") == 0) {
      diagnosticWifiPaused = false;
      WiFi.setAutoReconnect(true);
      WiFi.mode(WIFI_STA);
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
      Serial.println("[diag] Wi-Fi restored");
    }
  }
  if (stressRemaining > 0 && static_cast<int32_t>(millis() - stressDueAt) >= 0) {
    const uint32_t started = millis();
    drawHome();
    if ((stressStep % 2) == 0) {
      switch ((stressStep / 2) % 6) {
        case 0: drawSettings(); break;
        case 1: drawDota(); startDotaRefresh(); break;
        case 2: openSpotify(); break;
        case 3: openCodexBuddy(); break;
        case 4:
          currentScreen = Screen::SYNTH;
          synthAppBegin(&lcd);
          synthAppTouch(140, 170);
          break;
        case 5:
          currentScreen = Screen::DEADLOCK;
          deadlockAppBegin(&lcd, scaledHeroIcon, &spotifyTls);
          break;
      }
    }
    ++stressStep;
    --stressRemaining;
    stressDueAt = millis() + 2000;
    Serial.printf("[stress] step=%d remaining=%d transition=%lu screen=%u heap=%u\n",
                  stressStep, stressRemaining, static_cast<unsigned long>(millis() - started),
                  static_cast<unsigned>(currentScreen), ESP.getFreeHeap());
    if (stressRemaining == 0) {
      drawHome();
      Serial.println("[stress] DONE");
      systemHealthReport(static_cast<uint8_t>(currentScreen), spotifyJobBusy || dotaRefreshBusy);
    }
  }
}

void loop() {
  systemHealthStage(1, static_cast<uint8_t>(currentScreen));
  pollTouch();
  systemHealthStage(2, static_cast<uint8_t>(currentScreen));
  acceptStagedMatches();

  if (currentScreen == Screen::DOTA) {
    if (forceDotaRedraw || refreshStatus != shownRefreshStatus) {
      drawDota();
    }
    if (!dotaRefreshBusy &&
        refreshStatus != RefreshStatus::NOT_CONFIGURED &&
        millis() - lastAttemptMs >= nextAttemptIntervalMs) {
      startDotaRefresh();
    }
  }
  systemHealthStage(3, static_cast<uint8_t>(currentScreen));
  spotifyLoop();
  systemHealthStage(4, static_cast<uint8_t>(currentScreen));
  codexBuddyLoop();
  systemHealthStage(6, static_cast<uint8_t>(currentScreen));
  deadlockAppLoop(!spotifyJobBusy && !spotifyJobResultReady && !dotaRefreshBusy && codexBuddyBleIdle(), diagnosticWifiPaused);
  systemHealthStage(5, static_cast<uint8_t>(currentScreen));
  systemDiagnosticsLoop();
  if (spotifyJobBusy && millis() - spotifyJobStartedAt > 45000) {
    Serial.println("[health] TLS worker stalled; safe restart");
    ESP.restart();
  }
  if (dotaRefreshBusy && millis() - dotaRefreshStartedAt > 45000) {
    Serial.println("[health] Dota worker stalled; safe restart");
    ESP.restart();
  }
  systemHealthTick(static_cast<uint8_t>(currentScreen), spotifyJobBusy || dotaRefreshBusy || deadlockAppBusy());
  delay(8);
}

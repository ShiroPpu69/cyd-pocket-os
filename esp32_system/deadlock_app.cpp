#include "deadlock_app.h"
#include "LGFX_CYD.h"
#include "bounded_spotify_client.h"
#include "deadlock_hero_assets.h"
#include "user_config.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <time.h>

namespace {
struct Match {
  uint32_t id;
  uint32_t startTime, souls;
  bool hasSouls;
  uint16_t hero;
  uint16_t kills, deaths, assists, minutes;
  uint8_t outcome;
};
Match matches[10], staged[10];
size_t count = 0, stagedCount = 0;
LGFX_CYD* lcd = nullptr;
uint16_t* iconBuffer = nullptr;
// TLS contexts themselves reserve substantial RAM even before connecting.
// The shell's network arbiter guarantees exclusive use of this shared client.
BoundedSpotifyClient* sharedTransport = nullptr;
BoundedSpotifyClient& transport() { return *sharedTransport; }
std::atomic<bool> busy{false}, ready{false};
bool active = false, requested = false, connecting = false;
uint32_t startedAt = 0, wifiStartedAt = 0, nextRefresh = 0;
int page = 0, status = 0, completedStatus = 0;
constexpr uint16_t BG = 0x0841, TEXT = 0xf7be, MUTED = 0x7bef, GOLD = 0xfdc8;
constexpr size_t ROWS = 4;

void matchTime(uint32_t timestamp, char* buffer, size_t capacity) {
  if (!timestamp) { snprintf(buffer, capacity, "TIME UNKNOWN"); return; }
  // Format the API's match start timestamp without depending on NTP or global TZ.
  const time_t local = static_cast<time_t>(timestamp) + 8 * 3600;
  struct tm date;
  gmtime_r(&local, &date);
  // Avoid strftime: its locale/timezone helpers exceed this board's IRAM budget.
  snprintf(buffer, capacity, "%02d/%02d/%02d %02d:%02d", (date.tm_year + 1900) % 100,
           date.tm_mon + 1, date.tm_mday, date.tm_hour, date.tm_min);
}

void soulCount(const Match& match, char* buffer, size_t capacity) {
  if (!match.hasSouls) { snprintf(buffer, capacity, "--"); return; }
  if (match.souls < 1000) snprintf(buffer, capacity, "%lu", static_cast<unsigned long>(match.souls));
  else snprintf(buffer, capacity, "%lu,%03lu", static_cast<unsigned long>(match.souls / 1000),
                static_cast<unsigned long>(match.souls % 1000));
}

void text(const char* value, int x, int y, uint16_t color, uint8_t size = 1) {
  lcd->setFont(&fonts::Font0);
  lcd->setTextSize(size);
  lcd->setTextColor(color);
  lcd->setCursor(x, y);
  lcd->print(value);
}

void draw(bool full = true) {
  if (!active || !lcd) return;
  lcd->setRotation(0);
  if (full) lcd->fillScreen(BG);
  else lcd->fillRect(0, 0, 240, 60, BG);
  lcd->fillRect(8, 9, 3, 18, GOLD);
  text("DEADLOCK", 18, 8, TEXT, 2);
  lcd->drawRoundRect(175, 6, 57, 24, 5, 0x39c7);
  text(busy || connecting ? "SYNC.." : status < 0 || status >= 400 ? "RETRY" : "UPDATE", 184, 14, GOLD);
  text("MATCH JOURNAL", 8, 33, MUTED);
  text("UTC+8", 196, 33, MUTED);
  unsigned wins = 0;
  for (size_t i = 0; i < count; ++i) if (matches[i].outcome == 1) ++wins;
  char summary[40];
  snprintf(summary, sizeof(summary), "%02u RECENT  /  %02u WINS", static_cast<unsigned>(count), wins);
  lcd->fillRoundRect(8, 45, 224, 13, 3, 0x18c3);
  text(summary, 15, 48, GOLD);
  if (!full && count) return;
  if (!count) {
    const char* message = busy || connecting ? "LOADING MATCH HISTORY" :
                          status == 200 ? "NO RECORDED MATCHES" :
                          status == -10 ? "WAITING FOR WI-FI" :
                          status ? "NETWORK ERROR - TAP REFRESH" : "WAITING FOR NETWORK";
    text(message, 14, 139, TEXT);
    text("BOTTOM SWIPE: HOME", 55, 279, MUTED);
  }
  for (size_t row = 0; row < ROWS && page * ROWS + row < count; ++row) {
    const Match& match = matches[page * ROWS + row];
    const int y = 64 + row * 55;
    const uint16_t tile = 0x10a2;
    const uint16_t result = match.outcome == 1 ? 0x6dd1 : match.outcome == 2 ? 0xf36c : GOLD;
    lcd->fillRoundRect(7, y, 226, 51, 5, tile);
    lcd->drawRoundRect(7, y, 226, 51, 5, 0x2945);
    lcd->fillRoundRect(7, y + 8, 3, 35, 1, result);
    const DeadlockHeroAsset* hero = deadlockHeroAsset(match.hero);
    if (hero && hero->pixels && iconBuffer) {
      for (int dy = 0; dy < 40; ++dy) for (int dx = 0; dx < 40; ++dx) {
        const int sx = dx * 24 / 40, sy = dy * 24 / 40;
        const bool opaque = pgm_read_byte(hero->mask + sy * 3 + sx / 8) & (0x80 >> (sx & 7));
        iconBuffer[dy * 40 + dx] = opaque ? pgm_read_word(hero->pixels + sy * 24 + sx) : tile;
      }
      const bool swapped = lcd->getSwapBytes();
      lcd->setSwapBytes(true);
      lcd->pushImage(13, y + 5, 40, 40, iconBuffer);
      lcd->setSwapBytes(swapped);
    } else {
      char heroId[12];
      snprintf(heroId, sizeof(heroId), "H%u", match.hero);
      text(heroId, 16, y + 18, MUTED);
    }
    char heroName[25];
    snprintf(heroName, sizeof(heroName), "%.20s", hero ? hero->name : "UNKNOWN HERO");
    text(heroName, 61, y + 6, TEXT);
    lcd->fillRoundRect(193, y + 4, 33, 12, 3, match.outcome == 1 ? 0x1944 : match.outcome == 2 ? 0x38c3 : 0x2945);
    text(match.outcome == 1 ? "WIN" : match.outcome == 2 ? "LOSS" : "N/A", 197, y + 6, result);
    char stats[40];
    text("K / D / A", 61, y + 19, MUTED);
    snprintf(stats, sizeof(stats), "%u / %u / %u", match.kills, match.deaths, match.assists);
    text(stats, 61, y + 29, TEXT);
    text("SOULS", 172, y + 19, 0x6d5c);
    soulCount(match, stats, sizeof(stats));
    text(stats, 166, y + 29, 0xa71f);
    matchTime(match.startTime, stats, sizeof(stats));
    text(stats, 61, y + 41, MUTED);
    snprintf(stats, sizeof(stats), "%um", match.minutes);
    text(stats, 203, y + 41, MUTED);
  }
  lcd->drawRoundRect(8, 289, 43, 23, 5, page ? GOLD : 0x2945);
  text("<", 25, 294, page ? GOLD : MUTED, 2);
  char footer[28];
  snprintf(footer, sizeof(footer), "PAGE %u / %u", page + 1, max(1, static_cast<int>((count + ROWS - 1) / ROWS)));
  text(footer, 88, 298, TEXT);
  lcd->drawRoundRect(189, 289, 43, 23, 5, (page + 1) * ROWS < count ? GOLD : 0x2945);
  text(">", 205, 294, (page + 1) * ROWS < count ? GOLD : MUTED, 2);
  lcd->fillRoundRect(100, 316, 40, 2, 1, 0x39c7);
}

int delimiter(Stream& stream) {
  const uint32_t start = millis();
  while (!transport().aborted() && millis() - start < 3000) {
    if (stream.available()) {
      const int c = stream.read();
      if (c != ' ' && c != '\r' && c != '\n' && c != '\t') return c;
    } else if (!transport().connected()) return -1;
    delay(1);
  }
  return -1;
}

void worker(void*) {
  HTTPClient http;
  http.useHTTP10(true);
  http.setReuse(false);
  http.setConnectTimeout(3000);
  http.setTimeout(1500);
  const String url = String("https://api.deadlock-api.com/v1/players/") + DOTA_ACCOUNT_ID + "/match-history";
  stagedCount = 0;
  int result = -1;
  if (http.begin(transport(), url)) {
    http.addHeader("Accept", "application/json");
    http.addHeader("Accept-Encoding", "identity");
    result = http.GET();
    if (result == 200) {
      JsonDocument filter;
      for (const char* key : {"match_id", "hero_id", "player_kills", "player_deaths", "player_assists", "match_duration_s", "player_match_outcome", "player_team", "match_result", "start_time", "net_worth"}) filter[key] = true;
      Stream& stream = http.getStream();
      if (delimiter(stream) != '[') result = -2;
      else {
        // The API returns full history and has no documented limit parameter.
        // Parse one object at a time, retain only ten rows, then close early.
        while (stagedCount < 10 && !transport().aborted()) {
          int c = delimiter(stream);
          if (c == ']') break;
          if (c == ',') c = delimiter(stream);
          if (c != '{') { result = -2; break; }
          // Restore the opening brace via a tiny Stream prefix adapter.
          class ObjectStream : public Stream {
            Stream& source; bool prefix = true;
           public:
            explicit ObjectStream(Stream& source) : source(source) { setTimeout(1500); }
            int available() override { return prefix ? 1 : source.available(); }
            int read() override { if (prefix) { prefix = false; return '{'; } return source.read(); }
            int peek() override { return prefix ? '{' : source.peek(); }
            void flush() override {}
            size_t write(uint8_t) override { return 0; }
          } object(stream);
          JsonDocument doc;
          if (deserializeJson(doc, object, DeserializationOption::Filter(filter), DeserializationOption::NestingLimit(8))) { result = -2; break; }
          Match& match = staged[stagedCount++];
          match.id = doc["match_id"] | 0UL;
          match.startTime = doc["start_time"] | 0UL;
          match.hasSouls = !doc["net_worth"].isNull();
          match.souls = doc["net_worth"] | 0UL;
          match.hero = doc["hero_id"] | 0;
          match.kills = doc["player_kills"] | 0;
          match.deaths = doc["player_deaths"] | 0;
          match.assists = doc["player_assists"] | 0;
          match.minutes = (doc["match_duration_s"].as<uint32_t>() + 30) / 60;
          match.outcome = doc["player_match_outcome"].isNull()
              ? (doc["player_team"].as<int>() == doc["match_result"].as<int>() ? 1 : 2)
              : doc["player_match_outcome"].as<uint8_t>();
          if (!match.id || !match.hero) { result = -2; break; }
        }
      }
    }
    http.end();
  }
  transport().stop();
  if (transport().aborted()) result = -3;
  completedStatus = result;
  Serial.printf("[deadlock] status=%d matches=%u elapsed=%lu stack=%u\n", result,
                static_cast<unsigned>(stagedCount), static_cast<unsigned long>(millis() - startedAt),
                static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
  ready.store(true);
  vTaskDelete(nullptr);
}
}

void deadlockAppBegin(LGFX_CYD* display, uint16_t* sharedIconBuffer, BoundedSpotifyClient* sharedTls) {
  lcd = display; iconBuffer = sharedIconBuffer; sharedTransport = sharedTls; active = true; page = 0;
  requested = count == 0 || static_cast<int32_t>(millis() - nextRefresh) >= 0;
  draw();
}
void deadlockAppEnd() {
  active = requested = connecting = false;
  if (busy && sharedTransport) transport().cancel();
}
bool deadlockAppBusy() { return busy.load() || ready.load(); }
void deadlockAppLoop(bool networkAllowed, bool wifiPaused) {
  if (ready.exchange(false)) {
    busy = false;
    status = completedStatus;
    if (status == 200) { count = stagedCount; memcpy(matches, staged, count * sizeof(Match)); }
    nextRefresh = millis() + (status == 200 ? 180000 : 60000);
    if (active) draw(status == 200 || !count);
  }
  if (!active) return;
  if (busy && millis() - startedAt > 45000) {
    Serial.println("[deadlock] worker timeout; safe restart"); ESP.restart();
  }
  if (!requested || busy || !networkAllowed || wifiPaused || !sharedTransport) return;
  if (WiFi.status() != WL_CONNECTED) {
    if (!connecting) { WiFi.mode(WIFI_STA); WiFi.begin(WIFI_SSID, WIFI_PASSWORD); connecting = true; wifiStartedAt = millis(); draw(!count); }
    if (millis() - wifiStartedAt >= 12000) { requested = connecting = false; status = -10; draw(); }
    return;
  }
  connecting = false;
  if (ESP.getFreeHeap() < 65000 || heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) < 20000) return;
  transport().beginJob(12000); transport().setInsecure();
  busy = true; requested = false; startedAt = millis(); draw(!count);
  if (xTaskCreatePinnedToCore(worker, "deadlock-net", 8192, nullptr, 1, nullptr, 0) != pdPASS) {
    busy = false; status = -4; draw();
  }
}
void deadlockAppTap(uint16_t x, uint16_t y) {
  if (y < 31 && x >= 160) { requested = true; return; }
  if (y >= 289 && y < 312) {
    if (x < 60 && page > 0) --page;
    else if (x > 180 && (page + 1) * ROWS < count) ++page;
    else return;
    draw();
  }
}
void deadlockAppDiagnostics() {
  Serial.printf("[deadlock-diag] account=%s status=%d count=%u page=%d busy=%u\n", DOTA_ACCOUNT_ID,
                status, static_cast<unsigned>(count), page, busy.load());
  if (count && static_cast<size_t>(page * ROWS) < count) {
    const Match& match = matches[page * ROWS];
    Serial.printf("[deadlock-diag] first-visible=%lu hero=%u outcome=%u KDA=%u/%u/%u minutes=%u\n",
                  static_cast<unsigned long>(match.id), match.hero, match.outcome,
                  match.kills, match.deaths, match.assists, match.minutes);
    char date[24], souls[24];
    matchTime(match.startTime, date, sizeof(date)); soulCount(match, souls, sizeof(souls));
    Serial.printf("[deadlock-diag] start=%lu time=%s UTC+8 souls=%s source=net_worth\n",
                  static_cast<unsigned long>(match.startTime), date, souls);
  }
}

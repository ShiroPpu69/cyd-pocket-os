#include "system_health.h"
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <esp_task_wdt.h>

namespace {
RTC_NOINIT_ATTR uint32_t healthMagic;
RTC_NOINIT_ATTR uint8_t lastStage;
RTC_NOINIT_ATTR uint8_t lastScreen;
uint32_t lastLoopAt = 0;
uint32_t maxLoopGap = 0;
uint32_t nextReportAt = 0;
bool watchdogReady = false;
}

void systemHealthBegin() {
  Serial.printf("[health] reset=%d previous-screen=%u previous-stage=%u\n",
                esp_reset_reason(), healthMagic == 0x43594448 ? lastScreen : 255,
                healthMagic == 0x43594448 ? lastStage : 255);
  healthMagic = 0x43594448;
  const esp_task_wdt_config_t config = {
      .timeout_ms = 12000,
      .idle_core_mask = (1U << portNUM_PROCESSORS) - 1,
      .trigger_panic = true,
  };
  esp_err_t result = esp_task_wdt_init(&config);
  if (result == ESP_ERR_INVALID_STATE) result = esp_task_wdt_reconfigure(&config);
  if (result == ESP_OK) {
    watchdogReady = esp_task_wdt_status(nullptr) == ESP_OK || esp_task_wdt_add(nullptr) == ESP_OK;
  }
  Serial.printf("[health] UI watchdog=%s timeout=12s\n", watchdogReady ? "on" : "off");
  lastLoopAt = millis();
  nextReportAt = lastLoopAt + 30000;
}

void systemHealthStage(uint8_t stage, uint8_t screen) {
  lastStage = stage;
  lastScreen = screen;
}

void systemHealthReport(uint8_t screen, bool networkBusy) {
  Serial.printf("[health] uptime=%lu screen=%u net=%u heap=%u min=%u largest=%u loop-max=%lu stack=%u\n",
                static_cast<unsigned long>(millis()), screen, networkBusy,
                ESP.getFreeHeap(), ESP.getMinFreeHeap(),
                heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
                static_cast<unsigned long>(maxLoopGap),
                static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
}

void systemHealthTick(uint8_t screen, bool networkBusy) {
  const uint32_t now = millis();
  maxLoopGap = max(maxLoopGap, now - lastLoopAt);
  lastLoopAt = now;
  if (watchdogReady) esp_task_wdt_reset();
  if (static_cast<int32_t>(now - nextReportAt) >= 0) {
    nextReportAt = now + 30000;
    if (!heap_caps_check_integrity_all(false)) {
      Serial.println("[health] heap corruption detected; restarting");
      ESP.restart();
    }
    systemHealthReport(screen, networkBusy);
  }
}

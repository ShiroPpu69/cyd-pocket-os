#include "buddy_mao_animator.h"

#include <AnimatedGIF.h>
#include <esp_heap_caps.h>

#include "LGFX_CYD.h"
#include "buddy_axe_gifs.h"

namespace {

constexpr int16_t kCanvasWidth = 144;
constexpr int16_t kCanvasHeight = 156;
constexpr uint16_t kBackground = 0x0000;
constexpr uint32_t kVariantDwellMs = 5000;
constexpr uint32_t kAnimationPauseMs = 3000;

struct GifAsset {
  const uint8_t* data;
  size_t length;
};

const GifAsset kSleep = {BUDDY_AXE_SLEEP, BUDDY_AXE_SLEEP_LEN};
const GifAsset kIdle[] = {
    {BUDDY_AXE_IDLE_0, BUDDY_AXE_IDLE_0_LEN},
    {BUDDY_AXE_IDLE_1, BUDDY_AXE_IDLE_1_LEN},
};
const GifAsset kBusy = {BUDDY_AXE_BUSY, BUDDY_AXE_BUSY_LEN};
const GifAsset kAttention = {BUDDY_AXE_ATTENTION, BUDDY_AXE_ATTENTION_LEN};
const GifAsset kCompleted = {BUDDY_AXE_COMPLETED, BUDDY_AXE_COMPLETED_LEN};
const GifAsset kCelebrate = {BUDDY_AXE_CELEBRATE, BUDDY_AXE_CELEBRATE_LEN};
const GifAsset kDizzy = {BUDDY_AXE_DIZZY, BUDDY_AXE_DIZZY_LEN};
const GifAsset kHeart = {BUDDY_AXE_HEART, BUDDY_AXE_HEART_LEN};

LGFX_CYD* target = nullptr;
AnimatedGIF gif;
BuddyMaoState currentState = BuddyMaoState::IDLE;
bool useCelebrate = false;
bool initialized = false;
bool gifOpen = false;
uint8_t idleVariant = 0;
uint32_t nextFrameAt = 0;
uint32_t pauseUntil = 0;
uint32_t variantStartedAt = 0;
int16_t targetX = 0;
int16_t targetY = 0;

const GifAsset& selectedAsset() {
  switch (currentState) {
    case BuddyMaoState::SLEEP:
      return kSleep;
    case BuddyMaoState::BUSY:
      return kBusy;
    case BuddyMaoState::ATTENTION:
      return kAttention;
    case BuddyMaoState::COMPLETED:
      return useCelebrate ? kCelebrate : kCompleted;
    case BuddyMaoState::DIZZY:
      return kDizzy;
    case BuddyMaoState::HEART:
      return kHeart;
    case BuddyMaoState::IDLE:
    default:
      return kIdle[idleVariant];
  }
}

void drawGifLine(GIFDRAW* draw) {
  uint16_t scanline[kCanvasWidth];
  const int y = draw->iY + draw->y;
  if (y < 0 || y >= kCanvasHeight) return;

  uint8_t* pixels = draw->pPixels;
  const uint8_t transparent = draw->ucTransparent;
  const bool hasTransparency = draw->ucHasTransparency;
  int x = draw->iX;
  int width = draw->iWidth;
  if (x < 0) {
    pixels -= x;
    width += x;
    x = 0;
  }
  if (x + width > kCanvasWidth) width = kCanvasWidth - x;
  if (width <= 0) return;

  for (int i = 0; i < width; ++i) {
    const uint8_t index = pixels[i];
    scanline[i] = hasTransparency && index == transparent
                      ? kBackground
                      : draw->pPalette[index];
  }
  // AnimatedGIF's big-endian palette is already in the byte order expected
  // by LovyanGFX's raw RGB565 pushImage path.
  target->pushImage(targetX + x, targetY + y, width, 1, scanline);
}

bool openSelectedGif(bool clearFrame) {
  if (!initialized) return false;
  if (gifOpen) {
    gif.close();
    gifOpen = false;
  }
  if (clearFrame && target) {
    target->fillRect(targetX, targetY, kCanvasWidth, kCanvasHeight,
                     kBackground);
  }

  const GifAsset& asset = selectedAsset();
  gifOpen = gif.openFLASH(const_cast<uint8_t*>(asset.data),
                         static_cast<int>(asset.length), drawGifLine);
  nextFrameAt = 0;
  pauseUntil = 0;
  variantStartedAt = millis();
  if (!gifOpen) {
    Serial.printf("[buddy/axe] GIF open failed: %d\n", gif.getLastError());
  }
  return gifOpen;
}

}  // namespace

bool buddyMaoBegin(LGFX_CYD* display, int16_t x, int16_t y) {
  target = display;
  targetX = x;
  targetY = y;
  if (!initialized) {
    // This matches AnimatedGIF's official LovyanGFX example: direct LCD
    // scanlines use a pre-swapped (big-endian) RGB565 palette.
    gif.begin(GIF_PALETTE_RGB565_BE);
    initialized = true;
  }
  Serial.printf("[buddy/axe] begin heap=%u largest=%u\n", ESP.getFreeHeap(),
                heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
  currentState = BuddyMaoState::IDLE;
  useCelebrate = false;
  idleVariant = 0;
  return openSelectedGif(true);
}

void buddyMaoEnd() {
  if (gifOpen) {
    gif.close();
    gifOpen = false;
  }
}

void buddyMaoSetState(BuddyMaoState state, bool celebrate) {
  celebrate = state == BuddyMaoState::COMPLETED && celebrate;
  if (state == currentState && celebrate == useCelebrate) return;
  currentState = state;
  useCelebrate = celebrate;
  idleVariant = 0;
  openSelectedGif(true);
}

bool buddyMaoTick() {
  if (!initialized || !target) return false;
  const uint32_t now = millis();

  if (currentState == BuddyMaoState::IDLE &&
      now - variantStartedAt >= kVariantDwellMs) {
    idleVariant = (idleVariant + 1) % 2;
    openSelectedGif(false);
  }

  if (!gifOpen) {
    if (pauseUntil != 0 && static_cast<int32_t>(now - pauseUntil) >= 0) {
      openSelectedGif(false);
    }
    return false;
  }
  if (static_cast<int32_t>(now - nextFrameAt) < 0) return false;

  int delayMs = 0;
  if (!gif.playFrame(false, &delayMs)) {
    gif.close();
    gifOpen = false;
    pauseUntil = now + kAnimationPauseMs;
    return false;
  }

  nextFrameAt = now + (delayMs > 0 ? delayMs : 100);
  delay(1);
  return true;
}

BuddyMaoState buddyMaoStateFromName(const char* state) {
  if (!state) return BuddyMaoState::IDLE;
  if (strcmp(state, "sleep") == 0) return BuddyMaoState::SLEEP;
  if (strcmp(state, "busy") == 0) return BuddyMaoState::BUSY;
  if (strcmp(state, "attention") == 0) return BuddyMaoState::ATTENTION;
  if (strcmp(state, "completed") == 0 || strcmp(state, "celebrate") == 0) {
    return BuddyMaoState::COMPLETED;
  }
  if (strcmp(state, "dizzy") == 0) return BuddyMaoState::DIZZY;
  if (strcmp(state, "heart") == 0) return BuddyMaoState::HEART;
  return BuddyMaoState::IDLE;
}

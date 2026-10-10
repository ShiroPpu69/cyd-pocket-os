#include "synth_app.h"

#include "LGFX_CYD.h"
#include "src/ESP32Synth/ESP32Synth.h"

namespace {

constexpr uint16_t kBg = 0x0841;
constexpr uint16_t kPanel = 0x10C3;
constexpr uint16_t kLine = 0x326A;
constexpr uint16_t kText = 0xF7BE;
constexpr uint16_t kMuted = 0x7BEF;
constexpr uint16_t kGreen = 0x05FF;
constexpr uint16_t kBlue = 0x049F;
constexpr uint16_t kAmber = 0xFDC8;
constexpr uint8_t kAudioPin = 26;
constexpr int kKeyTop = 121;
constexpr int kKeyBottom = 239;
constexpr int kKeyCount = 24;

LGFX_CYD* lcd = nullptr;
ESP32Synth synth;
bool running = false;
bool notePlaying = false;
int activeKey = -1;
uint8_t morph = 127;
uint8_t resonance = 160;
int8_t octave = 3;

volatile uint32_t peakPhase = 2147483648UL;
volatile uint32_t spanPhase = 2147483647UL;
volatile int32_t filterF = 0;
volatile int32_t filterQ = 0;
volatile int32_t filterGain = 32768;
int32_t filterLow = 0;
int32_t filterHigh = 0;
int32_t filterBand = 0;

void renderMorphWave(Voice* voice, int32_t* mixBuffer, int samples,
                     int32_t startEnv, int32_t envStep) {
  uint32_t phase = voice->phase;
  const uint32_t increment = voice->phaseInc;
  int32_t envelope = startEnv;
  const int32_t volume = ((uint32_t)voice->vol * 255) >> 8;
  const uint32_t peak = peakPhase;
  const uint32_t span = spanPhase;
  const int32_t f = filterF;
  const int32_t q = filterQ;
  const int32_t gain = filterGain;

  for (int i = 0; i < samples; ++i) {
    int32_t raw;
    if (phase < peak) {
      const uint32_t fraction =
          static_cast<uint32_t>((static_cast<uint64_t>(phase) * 65535ULL) /
                                peak);
      raw = static_cast<int32_t>(fraction) - 32768;
    } else {
      const uint32_t remainder = phase - peak;
      const uint32_t fraction =
          static_cast<uint32_t>((static_cast<uint64_t>(remainder) *
                                 65535ULL) /
                                span);
      raw = 32767 - static_cast<int32_t>(fraction);
    }
    phase += increment;

    raw = (raw * gain) >> 15;
    filterLow += (f * filterBand) >> 15;
    filterLow = constrain(filterLow, -32768, 32767);
    filterHigh = raw - filterLow - ((q * filterBand) >> 15);
    filterHigh = constrain(filterHigh, -32768, 32767);
    filterBand += (f * filterHigh) >> 15;
    filterBand = constrain(filterBand, -32768, 32767);

    int32_t safeEnvelope = envelope >> 14;
    safeEnvelope &= ~(safeEnvelope >> 31);
    const int32_t finalVolume = (safeEnvelope * volume) >> 14;
    mixBuffer[i] += (filterLow * finalVolume) >> 16;
    envelope += envStep;
  }
  voice->phase = phase;
}

void updateDsp(float cutoffHz) {
  const float shape = 0.01f + (morph / 255.0f) * 0.98f;
  peakPhase = static_cast<uint32_t>(shape * 4294967295.0f);
  spanPhase = 4294967295UL - peakPhase;
  if (peakPhase == 0) peakPhase = 1;
  if (spanPhase == 0) spanPhase = 1;
  cutoffHz = constrain(cutoffHz, 80.0f, 6500.0f);
  float damping = 1.0f - (resonance / 255.0f);
  if (damping < 0.05f) damping = 0.05f;
  filterF =
      static_cast<int32_t>(2.0f * sinf(PI * cutoffHz / 48000.0f) * 32768.0f);
  filterQ = static_cast<int32_t>(damping * 32768.0f);
  float gain = min(1.0f, damping * 2.5f);
  filterGain = static_cast<int32_t>(gain * 32768.0f);
}

bool blackKey(int index) {
  const int note = index % 12;
  return note == 1 || note == 3 || note == 6 || note == 8 || note == 10;
}

void drawKey(int index, bool pressed = false) {
  if (!lcd || index < 0 || index >= kKeyCount) return;
  const int x = index * 320 / kKeyCount;
  const int nextX = (index + 1) * 320 / kKeyCount;
  const uint16_t base =
      pressed ? kBlue : blackKey(index) ? 0x18E3 : 0xCE59;
  lcd->fillRect(x, kKeyTop, nextX - x, kKeyBottom - kKeyTop + 1, base);
  lcd->drawRect(x, kKeyTop, nextX - x, kKeyBottom - kKeyTop + 1,
                pressed ? kGreen : TFT_BLACK);
}

void drawControl(int x, const char* label, int value, uint16_t color) {
  lcd->fillRoundRect(x, 27, 98, 31, 5, kPanel);
  lcd->drawRoundRect(x, 27, 98, 31, 5, kLine);
  lcd->setTextFont(1);
  lcd->setTextSize(1);
  lcd->setTextColor(color, kPanel);
  lcd->setCursor(x + 7, 39);
  lcd->printf("%s %d", label, value);
}

void drawScope() {
  lcd->fillRect(6, 64, 308, 51, TFT_BLACK);
  lcd->drawRect(5, 63, 310, 53, kLine);
  const float shape = 0.01f + (morph / 255.0f) * 0.98f;
  int previousY = 89;
  for (int x = 0; x < 306; ++x) {
    float phase = static_cast<float>(x) / 153.0f;
    phase -= static_cast<int>(phase);
    const float value =
        phase < shape ? (phase / shape) * 2.0f - 1.0f
                      : 1.0f - ((phase - shape) / (1.0f - shape)) * 2.0f;
    const int y = constrain(89 - static_cast<int>(value * 21), 66, 112);
    if (x > 0) lcd->drawLine(7 + x - 1, previousY, 7 + x, y, kGreen);
    previousY = y;
  }
}

void drawUi() {
  lcd->fillScreen(kBg);
  lcd->fillRect(0, 0, 320, 21, kPanel);
  lcd->setTextFont(1);
  lcd->setTextSize(1);
  lcd->setTextColor(kGreen, kPanel);
  lcd->setCursor(7, 7);
  lcd->print("ESP32SYNTH");
  lcd->setTextColor(kMuted, kPanel);
  lcd->setCursor(202, 7);
  lcd->print("DAC26 > SPEAKER");
  drawControl(5, "MORPH", morph, kGreen);
  drawControl(111, "RESO", resonance, kAmber);
  drawControl(217, "OCT", octave, kBlue);
  drawScope();
  for (int i = 0; i < kKeyCount; ++i) drawKey(i);
}

uint32_t frequencyForKey(int key) {
  const int midi = 12 * (octave + 1) + key;
  return static_cast<uint32_t>(
      44000.0f * powf(2.0f, (static_cast<float>(midi) - 69.0f) / 12.0f));
}

void playKey(int key, uint16_t y) {
  key = constrain(key, 0, kKeyCount - 1);
  if (key != activeKey) {
    if (activeKey >= 0) drawKey(activeKey);
    synth.noteOff(0);
    activeKey = key;
    drawKey(activeKey, true);
    synth.noteOn(0, frequencyForKey(activeKey), 255);
    notePlaying = true;
  }
  const int constrainedY =
      y < kKeyTop ? kKeyTop : y > kKeyBottom ? kKeyBottom : y;
  const float cutoff =
      200.0f + (kKeyBottom - constrainedY) *
                   (6300.0f / (kKeyBottom - kKeyTop));
  updateDsp(cutoff);
}

}  // namespace

bool synthAppBegin(LGFX_CYD* display) {
  lcd = display;
  lcd->setRotation(1);
  drawUi();
  if (!running) {
    // Board schematic: GPIO26 (DAC2) -> RC low-pass -> SC8002B/FM8002A
    // amplifier -> onboard/P4 speaker. PWM is the wrong output path here.
    running = synth.begin(kAudioPin);
    if (running) {
      synth.setMasterVolume(255);
      synth.setCustomWave(0, renderMorphWave);
      synth.setEnv(0, 5, 40, 220, 90);
      updateDsp(2200.0f);
    } else {
      synth.end();
      lcd->fillRoundRect(74, 78, 172, 28, 5, 0xE289);
      lcd->setTextColor(TFT_WHITE, 0xE289);
      lcd->drawCentreString("AUDIO START FAILED", 160, 87, 1);
    }
  }
  return running;
}

void synthAppEnd() {
  if (notePlaying) synth.noteOff(0);
  notePlaying = false;
  activeKey = -1;
  if (running) {
    const uint32_t started = millis();
    synth.end();
    Serial.printf("[synth] stopped elapsed=%lu heap=%u\n",
                  static_cast<unsigned long>(millis() - started), ESP.getFreeHeap());
    running = false;
  }
}

void synthAppTouch(uint16_t x, uint16_t y) {
  if (!running || y < kKeyTop || y > kKeyBottom) return;
  playKey(static_cast<int>(x) * kKeyCount / 320, y);
}

void synthAppRelease() {
  if (!running || !notePlaying) return;
  synth.noteOff(0);
  notePlaying = false;
  if (activeKey >= 0) drawKey(activeKey);
  activeKey = -1;
}

void synthAppTap(uint16_t x, uint16_t y) {
  if (!running || y < 25 || y > 60) return;
  if (x < 106) {
    morph = min(255, static_cast<int>(morph) + 32);
    if (morph == 255) morph = 0;
    drawControl(5, "MORPH", morph, kGreen);
    drawScope();
  } else if (x < 213) {
    resonance = min(255, static_cast<int>(resonance) + 24);
    if (resonance == 255) resonance = 16;
    drawControl(111, "RESO", resonance, kAmber);
  } else {
    octave = octave >= 6 ? 1 : octave + 1;
    drawControl(217, "OCT", octave, kBlue);
  }
  updateDsp(2200.0f);
}

#pragma once
#include <WiFiClientSecure.h>
#include <atomic>

// Only the network worker owns this client. UI cancellation is a flag, never
// a cross-core stop()/delete of a live TLS context.
class BoundedSpotifyClient : public WiFiClientSecure {
 public:
  void beginJob(uint32_t budgetMs) {
    cancelled.store(false);
    deadline = millis() + budgetMs;
    setHandshakeTimeout(3);
  }
  void cancel() { cancelled.store(true); }
  bool aborted() const {
    return cancelled.load() || static_cast<int32_t>(millis() - deadline) >= 0;
  }
  int connect(const char* host, uint16_t port) override {
    if (aborted()) return 0;
    if (connecting) return WiFiClientSecure::connect(host, port);
    connecting = true;
    const int result = WiFiClientSecure::connect(host, port, connectBudget());
    connecting = false;
    return result;
  }
  int connect(IPAddress ip, uint16_t port) override {
    if (aborted()) return 0;
    if (connecting) return WiFiClientSecure::connect(ip, port);
    connecting = true;
    const int result = WiFiClientSecure::connect(ip, port, connectBudget());
    connecting = false;
    return result;
  }
  size_t write(uint8_t value) override { return write(&value, 1); }
  size_t write(const uint8_t* data, size_t length) override {
    if (!keepAlive()) return 0;
    return WiFiClientSecure::write(data, length);
  }
  int available() override { return keepAlive() ? WiFiClientSecure::available() : 0; }
  int read() override { return keepAlive() ? WiFiClientSecure::read() : -1; }
  int read(uint8_t* data, size_t length) override {
    return keepAlive() ? WiFiClientSecure::read(data, length) : -1;
  }
  size_t readBytes(char* data, size_t length) override {
    return readBytes(reinterpret_cast<uint8_t*>(data), length);
  }
  size_t readBytes(uint8_t* data, size_t length) override {
    size_t received = 0;
    uint32_t lastDataAt = millis();
    while (received < length && !aborted()) {
      const int pending = available();
      if (pending > 0) {
        const int amount = read(data + received, min(length - received, static_cast<size_t>(pending)));
        if (amount <= 0) break;
        received += amount;
        lastDataAt = millis();
      } else {
        if (!connected() || millis() - lastDataAt >= getTimeout()) break;
        delay(1);
      }
    }
    if (aborted()) WiFiClientSecure::stop();
    return received;
  }
  int peek() override { return keepAlive() ? WiFiClientSecure::peek() : -1; }
  uint8_t connected() override { return keepAlive() ? WiFiClientSecure::connected() : 0; }

 private:
  std::atomic<bool> cancelled{false};
  uint32_t deadline = 0;
  // Arduino's timeout overload dispatches back to the virtual two-argument
  // overload. Guard that dispatch so it reaches the base implementation.
  bool connecting = false;
  int32_t connectBudget() {
    const int32_t remaining = static_cast<int32_t>(deadline - millis());
    return min<int32_t>(3000, max<int32_t>(1, remaining));
  }
  bool keepAlive() {
    if (!aborted()) return true;
    WiFiClientSecure::stop();
    return false;
  }
};

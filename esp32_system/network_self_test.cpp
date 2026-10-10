#include "network_self_test.h"
#include "src/SpotifyArduino.h"
#include <esp_heap_caps.h>

namespace {
class ResponseClient : public Client {
 public:
  explicit ResponseClient(const char* response) : response(response) {}
  int connect(IPAddress, uint16_t) override { position = 0; open = true; return 1; }
  int connect(const char*, uint16_t) override { position = 0; open = true; return 1; }
  size_t write(uint8_t) override { return 1; }
  size_t write(const uint8_t*, size_t size) override { return size; }
  int available() override { return open ? strlen(response) - position : 0; }
  int read() override { return available() ? response[position++] : -1; }
  int read(uint8_t* buffer, size_t size) override {
    size = min(size, static_cast<size_t>(available()));
    memcpy(buffer, response + position, size);
    position += size;
    return size;
  }
  int peek() override { return available() ? response[position] : -1; }
  void flush() override {}
  void stop() override { open = false; }
  uint8_t connected() override { return available() > 0; }
  operator bool() override { return connected(); }
 private:
  const char* response;
  size_t position = 0;
  bool open = false;
};
}

void runNetworkSelfTest() {
  const char* responses[] = {
      "HTTP/1.0 200 OK\r\nContent-Length: 4\r\nConnection: close\r\n\r\nTESTEXCESS_BYTES",
      "HTTP/1.0 200 OK\r\nContent-Length: 9\r\nConnection: close\r\n\r\nTEST",
      "HTTP/1.0 200 OK\r\nContent-Length: 40000\r\nConnection: close\r\n\r\nTEST",
  };
  for (int i = 0; i < 3; ++i) {
    ResponseClient client(responses[i]);
    SpotifyArduino api(client);
    char url[] = "https://test.invalid/cover";
    uint8_t* data = nullptr;
    int length = 0;
    const bool success = api.getImage(url, &data, &length);
    const bool pass = i == 0 ? success && length == 4 && data && memcmp(data, "TEST", 4) == 0
                            : !success && data == nullptr && length == 0;
    free(data);
    Serial.printf("[selftest] artwork-%d=%s heap-integrity=%u\n", i,
                  pass ? "PASS" : "FAIL", heap_caps_check_integrity_all(false));
  }
  ResponseClient client(responses[0]);
  SpotifyArduino api(client);
  char invalidUrl[] = "https://test.invalid";
  uint8_t* data = nullptr;
  int length = 0;
  const bool invalid = api.getImage(invalidUrl, &data, &length);
  free(data);
  Serial.printf("[selftest] missing-path=%s\n", !invalid ? "PASS" : "FAIL");
}

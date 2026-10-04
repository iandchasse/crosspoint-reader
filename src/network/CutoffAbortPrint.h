#pragma once

#include <HalPowerManager.h>
#include <Print.h>

// Wraps the web server's client for a whole-file stream (WebDAV GET, /download).
// Such a stream holds loop() until the file ends, and the low-battery cutoff is
// only evaluated from loop(). write() returns 0 once the cutoff has latched
// (always false on boards without one), which the stream loops treat as a failed
// write: the body is cut short, and the next loop pass sleeps.
class CutoffAbortPrint : public Print {
 public:
  explicit CutoffAbortPrint(Print& sink) : _sink(sink) {}

  size_t write(uint8_t byte) override { return write(&byte, 1); }

  size_t write(const uint8_t* buffer, size_t size) override {
    if (powerManager.lowBatteryCutoffTripped()) return 0;
    return _sink.write(buffer, size);
  }

  using Print::write;

 private:
  Print& _sink;
};

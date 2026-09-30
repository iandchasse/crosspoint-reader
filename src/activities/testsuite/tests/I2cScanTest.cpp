#if CROSSPOINT_TEST_SUITE
// P2: I2C sensor-bus scan (bring-up B7, B9). Address-only probes through
// freeink::i2c::scanSensorBus(), which uses a running bus as its driver left it
// (only the clock changes for the 400 kHz pass, and is put back). The idle line
// levels are read only while a driver owns the bus: nothing here reconfigures
// the pins or runs the stuck-bus recovery.
#include <Arduino.h>
#include <BoardConfig.h>
#include <HalClock.h>
#include <HalGPIO.h>
#include <Memory.h>
#include <SharedI2c.h>
#include <driver/gpio.h>
#include <esp32-hal-periman.h>

#include <cstdio>
#include <cstring>

#include "TestFactories.h"
#include "TestNames.h"
#include "activities/testsuite/TestCatalog.h"
#include "activities/testsuite/TestScreen.h"

namespace testsuite {
namespace {

constexpr uint32_t STANDARD_HZ = 100000;
constexpr uint32_t FAST_HZ = 400000;
constexpr uint8_t FIRST_ADDR = 0x08;
constexpr uint8_t LAST_ADDR = 0x77;
constexpr int MAX_ROWS = 10;  // addresses listed on screen

constexpr char INTRO[] =
    "Address-only probes of 0x08-0x77 at 100 and 400 kHz. Confirm: scan with J6 empty. Right: scan with an "
    "accessory on J6. An idle FT6336U can miss a key-started scan: tap Scan instead.";

enum class Level : uint8_t { NotRead, High, Low, Busy };

const char* levelName(const Level level) {
  switch (level) {
    case Level::High:
      return "HIGH";
    case Level::Low:
      return "LOW";
    case Level::Busy:
      return "busy";
    case Level::NotRead:
      break;
  }
  return "n/a";
}

// Idle level of a bus line a driver owns (its pad input is enabled). A few tries
// of eight reads, so a transfer from another task is not taken for a stuck line.
Level readIdleLevel(const int8_t pin, const peripheral_bus_type_t owner) {
  if (pin < 0 || perimanGetPinBusType(static_cast<uint8_t>(pin)) != owner) return Level::NotRead;
  Level seen = Level::Low;
  for (int attempt = 0; attempt < 3; ++attempt) {
    int high = 0;
    for (int i = 0; i < 8; ++i) {
      high += gpio_get_level(static_cast<gpio_num_t>(pin));
      delayMicroseconds(50);
    }
    if (high == 8) return Level::High;
    if (high != 0) seen = Level::Busy;
    delay(2);
  }
  return seen;
}

// What the board profile puts at `addr` on the sensor bus. `rtc`/`touch` say
// which group it belongs to.
const char* identify(const uint8_t addr, bool& rtc, bool& touch) {
  const auto& s = BoardConfig::ACTIVE.sensors;
  const auto& t = BoardConfig::ACTIVE.touch;
  rtc = false;
  touch = false;
  if (s.rtcAddr != 0 && addr == s.rtcAddr) {
    rtc = true;
    return rtcTypeName(s.rtcType);
  }
  if (s.rtcFallbackAddr != 0 && addr == s.rtcFallbackAddr) {
    rtc = true;
    return rtcTypeName(s.rtcFallbackType);
  }
  if (s.tempHumidityAddr != 0 && addr == s.tempHumidityAddr) return "temp/humidity";
  if (s.imuAddr != 0 && addr == s.imuAddr) return "IMU";
  if (!freeink::i2c::isSensorBus(t.sda, t.scl)) return nullptr;
  if (t.i2cAddress != 0 && addr == t.i2cAddress) {
    touch = true;
    return t.controller != BoardConfig::TouchController::None ? touchControllerName(t.controller) : "touch (J4)";
  }
  if ((t.fallbackAddr != 0 && addr == t.fallbackAddr) || (t.i2cAddressAlt != 0 && addr == t.i2cAddressAlt)) {
    touch = true;
    return touchControllerName(t.fallbackController != BoardConfig::TouchController::None ? t.fallbackController
                                                                                          : t.controller);
  }
  return nullptr;
}

// "0x38,0x68" into buf; "none" when empty.
void formatAddresses(char* buf, const size_t size, const freeink::i2c::ScanResult& scan) {
  buf[0] = '\0';
  size_t len = 0;
  for (uint8_t addr = FIRST_ADDR; addr <= LAST_ADDR && len + 6 < size; ++addr) {
    if (!scan.acked(addr)) continue;
    len += static_cast<size_t>(snprintf(buf + len, size - len, "%s0x%02x", len == 0 ? "" : ",", addr));
  }
  if (len == 0) snprintf(buf, size, "none");
}

class I2cScanTest final : public TestScreen {
 public:
  I2cScanTest(const TestInfo& info, GfxRenderer& renderer, MappedInputManager& mappedInput)
      : TestScreen(info, renderer, mappedInput) {}

 private:
  const char* slotLabel(const Slot slot) const override {
    switch (slot) {
      case Slot::Confirm:
        return "Scan";
      case Slot::Right:
        return "Scan+J6";
      default:
        return nullptr;
    }
  }

  void onSlot(const Slot slot) override {
    if (slot == Slot::Confirm) scan(/*j6Fitted=*/false);
    if (slot == Slot::Right) scan(/*j6Fitted=*/true);
  }

  void scan(const bool j6Fitted) {
    const auto& s = BoardConfig::ACTIVE.sensors;
    sda = readIdleLevel(s.i2cSda, ESP32_BUS_TYPE_I2C_MASTER_SDA);
    scl = readIdleLevel(s.i2cScl, ESP32_BUS_TYPE_I2C_MASTER_SCL);
    standard = freeink::i2c::scanSensorBus(STANDARD_HZ, FIRST_ADDR, LAST_ADDR);
    fast = freeink::i2c::scanSensorBus(FAST_HZ, FIRST_ADDR, LAST_ADDR);
    scanned = true;
    j6 = j6Fitted;
    touchActive = gpio.hasTouch();

    char fails[96] = "";
    const auto fail = [&fails](const char* what) {
      if (fails[0] != '\0') strncat(fails, "+", sizeof(fails) - strlen(fails) - 1);
      strncat(fails, what, sizeof(fails) - strlen(fails) - 1);
    };
    if (!standard.ok || !fast.ok) fail("bus");
    if (memcmp(standard.found, fast.found, sizeof(standard.found)) != 0) fail("400k_differs");
    if (sda == Level::Low || sda == Level::Busy || scl == Level::Low || scl == Level::Busy) fail("idle_level");

    int rtcs = 0;
    bool touchSeen = false;
    unknown = 0;
    for (uint8_t addr = FIRST_ADDR; addr <= LAST_ADDR; ++addr) {
      if (!standard.acked(addr) && !fast.acked(addr)) continue;
      bool isRtc = false;
      bool isTouch = false;
      if (identify(addr, isRtc, isTouch) == nullptr) ++unknown;
      if (isRtc) ++rtcs;
      if (isTouch) touchSeen = true;
    }
    // The board carries one RTC footprint's part (U13 or U14).
    if (rtcs == 0) fail("no_rtc");
    if (rtcs > 1) fail("two_rtcs");
    if (touchActive && !standard.acked(BoardConfig::ACTIVE.touch.i2cAddress)) fail("touch_no_ack");
    if (!touchActive && touchSeen) fail("touch_not_probed");
    if (!j6 && unknown > 0) fail("unexpected_device");

    char found100[56];
    char found400[56];
    formatAddresses(found100, sizeof(found100), standard);
    formatAddresses(found400, sizeof(found400), fast);
    record(fails[0] == '\0' ? Verdict::Pass : Verdict::Fail,
           "j6=%s owner=%s hz=%lu,%lu sda=%s scl=%s found100=%s found400=%s rtcs=%d rtc_at_boot=%d touch=%s "
           "unknown=%d fails=%s",
           j6 ? "fitted" : "empty", standard.wasRunning ? "driver" : "none", static_cast<unsigned long>(standard.hz),
           static_cast<unsigned long>(fast.hz), levelName(sda), levelName(scl), found100, found400, rtcs,
           halClock.isAvailable() ? 1 : 0, touchActive ? "active" : "absent", unknown,
           fails[0] == '\0' ? "none" : fails);
  }

  void drawBody(TestPage& page) const override {
    const auto& s = BoardConfig::ACTIVE.sensors;
    page.paragraph(INTRO, 4);
    page.gap(4);
    page.rowf("Bus", "SDA IO%d, SCL IO%d, %lu kHz", s.i2cSda, s.i2cScl, static_cast<unsigned long>(s.i2cHz / 1000));
    if (!scanned) return;
    page.rowf("Driver on the bus", "%s", standard.wasRunning ? "yes (RTC or touch)" : "no: started for the scan");
    page.rowf("Idle SDA / SCL", "%s / %s", levelName(sda), levelName(scl));
    page.rowf("J6", "%s", j6 ? "accessory fitted" : "empty");
    page.gap(4);
    page.setCompact(true);
    int rows = 0;
    for (uint8_t addr = FIRST_ADDR; addr <= LAST_ADDR && rows < MAX_ROWS; ++addr) {
      const bool at100 = standard.acked(addr);
      const bool at400 = fast.acked(addr);
      if (!at100 && !at400) continue;
      bool isRtc = false;
      bool isTouch = false;
      const char* name = identify(addr, isRtc, isTouch);
      char label[40];
      snprintf(label, sizeof(label), "0x%02x  %s", addr, name != nullptr ? name : j6 ? "J6 accessory?" : "UNEXPECTED");
      page.rowf(label, "100k %s, 400k %s", at100 ? "ACK" : "-", at400 ? "ACK" : "-");
      ++rows;
    }
    if (rows == 0) page.row("No device answered", "");
    if (touchActive && !standard.acked(BoardConfig::ACTIVE.touch.i2cAddress)) {
      char label[40];
      snprintf(label, sizeof(label), "0x%02x  touch (found at boot)", BoardConfig::ACTIVE.touch.i2cAddress);
      page.row(label, "NO ACK: tap Scan");
    }
    page.setCompact(false);
  }

  freeink::i2c::ScanResult standard;
  freeink::i2c::ScanResult fast;
  Level sda = Level::NotRead;
  Level scl = Level::NotRead;
  int unknown = 0;
  bool scanned = false;
  bool j6 = false;
  bool touchActive = false;
};

}  // namespace

std::unique_ptr<Activity> createI2cScanTest(const TestInfo& info, GfxRenderer& renderer,
                                            MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<I2cScanTest>(info, renderer, mappedInput);
}

}  // namespace testsuite
#endif

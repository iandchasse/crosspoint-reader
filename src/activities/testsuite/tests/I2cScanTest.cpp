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

#include "BootProbes.h"
#include "TestFactories.h"
#include "TestNames.h"
#include "TextBuffer.h"
#include "activities/testsuite/BoardOptions.h"
#include "activities/testsuite/TestCatalog.h"
#include "activities/testsuite/TestScreen.h"
#include "activities/testsuite/TestSuiteAccess.h"

namespace testsuite {
namespace {

constexpr uint32_t STANDARD_HZ = 100000;
constexpr uint32_t FAST_HZ = 400000;
constexpr uint8_t FIRST_ADDR = 0x08;
constexpr uint8_t LAST_ADDR = 0x77;
constexpr int MAX_ROWS = 10;  // addresses listed on screen

constexpr char INTRO[] =
    "Address-only probes of 0x08-0x77 at 100 and 400 kHz. Confirm: scan with J6 empty. Right: scan with an "
    "accessory on J6. An idle FT6336U can miss a key-started scan: tap Scan instead. A device at the touch or RTC "
    "address that failed its identity check at boot was left alone.";
constexpr char CUT_DOWN_NOTE[] = "Cut-down board (OPT): J6 is gone, so the J6 scan is N/A.";

// The RTC is optional (rtc-devices): with none answering and none begun at
// boot, the operator says whether one is fitted, as in T13.
constexpr int QUESTION_RTC_FITTED = 1;
constexpr const char* RTC_FITTED_ANSWERS[] = {"None fitted", "U13 or U14 fitted"};
constexpr char RTC_FITTED_HEADING[] = "RTC fitted?";
constexpr char RTC_FITTED_BODY[] = "Nothing answers at 0x68 or 0x51. Is an RTC (U13 DS3231MZ or U14 RV-8263) fitted?";

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
        return options::cutDown() ? nullptr : "Scan+J6";
      default:
        return nullptr;
    }
  }

  void onSlot(const Slot slot) override {
    if (slot == Slot::Confirm) scan(/*j6Fitted=*/false);
    if (slot == Slot::Right && !options::cutDown()) scan(/*j6Fitted=*/true);
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
    // Where the controller found at boot answers: a GT911 on the fallback can sit
    // at the alternate address (0x14), which only the driver knows.
    touchAddr = TestSuiteAccess::input(gpio).touchDiagnostics().address;
    if (touchAddr == 0) touchAddr = BoardConfig::ACTIVE.touch.i2cAddress;
    // Identity checks at boot (TouchConfig::requireFocalTechId, SensorsConfig::rtcIdentityCheck).
    touchRejected = touchIdRejected();
    rtcRejected = static_cast<uint8_t>(rtcRejectedAddress());

    fails[0] = '\0';
    const auto fail = [this](const char* what) { addFailure(fails, sizeof(fails), what); };
    if (!standard.ok || !fast.ok) fail("bus");
    if (memcmp(standard.found, fast.found, sizeof(standard.found)) != 0) fail("400k_differs");
    if (sda == Level::Low || sda == Level::Busy || scl == Level::Low || scl == Level::Busy) fail("idle_level");

    rtcs = 0;
    bool touchSeen = false;
    bool touchForeign = false;
    bool rtcForeign = false;
    unknown = 0;
    for (uint8_t addr = FIRST_ADDR; addr <= LAST_ADDR; ++addr) {
      if (!standard.acked(addr) && !fast.acked(addr)) continue;
      bool isRtc = false;
      bool isTouch = false;
      if (identify(addr, isRtc, isTouch) == nullptr) ++unknown;
      // A device the boot's identity check refused is not the RTC or the touch tail.
      if (isRtc && addr == rtcRejected) {
        rtcForeign = true;
      } else if (isRtc) {
        ++rtcs;
      }
      if (isTouch && touchRejected && addr == BoardConfig::ACTIVE.touch.i2cAddress) {
        touchForeign = true;
      } else if (isTouch) {
        touchSeen = true;
      }
    }
    // At most one RTC footprint's part (U13 or U14); one HalClock began at boot
    // must still answer.
    if (rtcs == 0 && halClock.isAvailable()) fail("rtc_lost");
    if (rtcs > 1) fail("two_rtcs");
    if (touchActive && !standard.acked(touchAddr)) fail("touch_no_ack");
    if (!touchActive && touchSeen) fail("touch_not_probed");
    if (!j6 && unknown > 0) fail("unexpected_device");
    // With J6 empty, only the touch tail or U13 can answer there: either failed its check.
    if (!j6 && touchForeign) fail("touch_id_rejected");
    if (!j6 && rtcForeign) fail("rtc_id_rejected");

    if (rtcs == 0 && !halClock.isAvailable()) {
      ask(QUESTION_RTC_FITTED, RTC_FITTED_HEADING, RTC_FITTED_BODY, RTC_FITTED_ANSWERS, 2);
      return;
    }
    recordScan("n/a");
  }

  // Back on the question records nothing.
  void onAnswer(const int question, const int answer) override {
    if (question != QUESTION_RTC_FITTED || answer < 0) return;
    if (answer == 1) addFailure(fails, sizeof(fails), "no_rtc");
    recordScan(answer == 0 ? "no" : "yes");
  }

  void recordScan(const char* rtcFitted) {
    char found100[56];
    char found400[56];
    formatAddresses(found100, sizeof(found100), standard);
    formatAddresses(found400, sizeof(found400), fast);
    char touchId[24];
    char rtcId[24];
    formatTouchIdToken(touchId, sizeof(touchId));
    formatRtcIdToken(rtcId, sizeof(rtcId));
    record(fails[0] == '\0' ? Verdict::Pass : Verdict::Fail,
           "j6=%s owner=%s hz=%lu,%lu sda=%s scl=%s found100=%s found400=%s rtcs=%d rtc_at_boot=%d rtc_fitted=%s "
           "touch=%s touch_addr=0x%02x touch_id=%s rtc_id=%s unknown=%d fails=%s",
           options::cutDown() ? "n/a"
           : j6               ? "fitted"
                              : "empty",
           standard.wasRunning ? "driver" : "none", static_cast<unsigned long>(standard.hz),
           static_cast<unsigned long>(fast.hz), levelName(sda), levelName(scl), found100, found400, rtcs,
           halClock.isAvailable() ? 1 : 0, rtcFitted, touchActive ? "active" : "absent", touchAddr, touchId, rtcId,
           unknown, fails[0] == '\0' ? "none" : fails);
  }

  void drawBody(TestPage& page) const override {
    const auto& s = BoardConfig::ACTIVE.sensors;
    page.paragraph(INTRO, 5);
    if (options::cutDown()) page.paragraph(CUT_DOWN_NOTE, 2);
    page.gap(4);
    page.rowf("Bus", "SDA IO%d, SCL IO%d, %lu kHz", s.i2cSda, s.i2cScl, static_cast<unsigned long>(s.i2cHz / 1000));
    if (!scanned) return;
    page.rowf("Driver on the bus", "%s", standard.wasRunning ? "yes (RTC or touch)" : "no: started for the scan");
    page.rowf("Idle SDA / SCL", "%s / %s", levelName(sda), levelName(scl));
    page.rowf("J6", "%s", options::cutDown() ? "N/A: cut-down board" : j6 ? "accessory fitted" : "empty");
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
      if (isRtc && addr == rtcRejected) name = "not a DS3231, left alone";
      if (isTouch && touchRejected && addr == BoardConfig::ACTIVE.touch.i2cAddress) name = "not FocalTech, left alone";
      char label[40];
      snprintf(label, sizeof(label), "0x%02x  %s", addr, name != nullptr ? name : j6 ? "J6 accessory?" : "UNEXPECTED");
      page.rowf(label, "100k %s, 400k %s", at100 ? "ACK" : "-", at400 ? "ACK" : "-");
      ++rows;
    }
    if (rows == 0) page.row("No device answered", "");
    if (touchActive && !standard.acked(touchAddr)) {
      char label[40];
      snprintf(label, sizeof(label), "0x%02x  touch (found at boot)", touchAddr);
      page.row(label, "NO ACK: tap Scan");
    }
    page.setCompact(false);
  }

  freeink::i2c::ScanResult standard;
  freeink::i2c::ScanResult fast;
  Level sda = Level::NotRead;
  Level scl = Level::NotRead;
  int unknown = 0;
  int rtcs = 0;
  uint8_t touchAddr = 0;
  uint8_t rtcRejected = 0;  // the address the RTC's boot identity check refused
  bool touchRejected = false;
  char fails[96] = "";  // kept while the RTC question is open
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

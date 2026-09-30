#if CROSSPOINT_TEST_SUITE
#include "RtcAccess.h"

#include <Arduino.h>
#include <HalClock.h>
#include <SharedI2c.h>

#include "activities/testsuite/TestSuiteAccess.h"

namespace testsuite::rtc {
namespace {

constexpr unsigned long TICK_POLL_MS = 5;
bool written = false;

}  // namespace

bool present() { return halClock.isAvailable(); }

Rtc& driver() { return TestSuiteAccess::rtc(halClock); }

int64_t toEpoch(const Rtc::DateTime& utc) {
  // Days from the civil date (Howard Hinnant's algorithm), as HalClock does.
  int y = utc.year;
  const int m = utc.month;
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153u * static_cast<unsigned>(m + (m > 2 ? -3 : 9)) + 2u) / 5u + utc.day - 1u;
  const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
  const int64_t days = static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(doe) - 719468;
  return days * 86400 + utc.hour * 3600L + utc.minute * 60L + utc.second;
}

void fromEpoch(const int64_t epoch, Rtc::DateTime& utc) {
  int64_t days = epoch / 86400;
  int64_t rem = epoch % 86400;
  if (rem < 0) {
    rem += 86400;
    --days;
  }
  utc.hour = static_cast<uint8_t>(rem / 3600);
  utc.minute = static_cast<uint8_t>(rem % 3600 / 60);
  utc.second = static_cast<uint8_t>(rem % 60);
  // 1970-01-01 was a Thursday.
  utc.weekday = static_cast<uint8_t>(((days % 7) + 11) % 7);
  // The civil date from days (Hinnant).
  const int64_t z = days + 719468;
  const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  const auto doe = static_cast<unsigned>(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  const unsigned d = doy - (153 * mp + 2) / 5 + 1;
  const unsigned mo = mp < 10 ? mp + 3 : mp - 9;
  utc.year = static_cast<uint16_t>(static_cast<int64_t>(yoe) + era * 400 + (mo <= 2 ? 1 : 0));
  utc.month = static_cast<uint8_t>(mo);
  utc.day = static_cast<uint8_t>(d);
}

bool readAtTick(uint32_t& epoch, unsigned long& atMs) {
  if (!present()) return false;
  Rtc& clock = driver();
  Rtc::DateTime first{};
  if (!clock.now(first)) return false;
  const unsigned long start = millis();
  while (millis() - start < TICK_WAIT_MS) {
    delay(TICK_POLL_MS);
    const unsigned long readAt = millis();
    Rtc::DateTime now{};
    if (!clock.now(now)) return false;
    if (now.second != first.second) {
      epoch = static_cast<uint32_t>(toEpoch(now));
      atMs = readAt;
      return true;
    }
  }
  return false;
}

bool readRegisters(const uint8_t addr, const uint8_t reg, uint8_t* dst, const uint8_t len) {
  auto& wire = freeink::i2c::sensorWire();
  wire.beginTransmission(addr);
  wire.write(reg);
  if (wire.endTransmission(false) != 0) return false;
  if (wire.requestFrom(addr, len, static_cast<uint8_t>(true)) < len) return false;
  for (uint8_t i = 0; i < len; ++i) dst[i] = static_cast<uint8_t>(wire.read());
  return true;
}

void noteWritten() {
  written = true;
  TestSuiteAccess::dropClockCache(halClock);
}

bool writtenThisBoot() { return written; }

}  // namespace testsuite::rtc
#endif

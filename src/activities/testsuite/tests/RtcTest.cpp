#if CROSSPOINT_TEST_SUITE
// T13: the RTC (bring-up B7). Which part answers (U13 DS3231MZ at 0x68 or U14
// RV-8263-C7 at 0x51), a dump of its registers and the bits the board relies
// on, the time, a 60 s check against the CPU clock, a 1000-read bench at 100
// and 400 kHz, and a time set by hand (no Wi-Fi). The only writes are
// Rtc::set()'s: the time registers, then OSF cleared (EN32KHZ with it). The
// control register is never written here, so EOSC stays 0; the bus clock goes
// back to its own after the bench.
#include <Arduino.h>
#include <BoardConfig.h>
#include <HalClock.h>
#include <Logging.h>
#include <Memory.h>
#include <SharedI2c.h>

#include <cstdio>
#include <cstring>
#include <ctime>

#include "RtcAccess.h"
#include "TestFactories.h"
#include "TestNames.h"
#include "TextBuffer.h"
#include "activities/testsuite/TestCatalog.h"
#include "activities/testsuite/TestScreen.h"

namespace testsuite {
namespace {

using RtcType = BoardConfig::RtcType;

// DS3231: time 0x00-0x06, control 0x0E, status 0x0F, aging 0x10, temperature 0x11-0x12.
constexpr uint8_t DS_REG_COUNT = 0x13;
constexpr uint8_t DS_CONTROL = 0x0E;
constexpr uint8_t DS_STATUS = 0x0F;
constexpr uint8_t DS_AGING = 0x10;
constexpr uint8_t DS_TEMP_MSB = 0x11;
constexpr uint8_t DS_TEMP_LSB = 0x12;
constexpr uint8_t DS_EOSC = 0x80;
constexpr uint8_t DS_INTCN = 0x04;
constexpr uint8_t DS_OSF = 0x80;
constexpr uint8_t DS_EN32KHZ = 0x08;
constexpr uint8_t DS_BSY = 0x04;
// PCF85063 map (RV-8263-C7): Control_1, Control_2, Offset, RAM, then the time from 0x04.
constexpr uint8_t PCF_REG_COUNT = 0x0B;
constexpr uint8_t PCF_CONTROL1 = 0x00;
constexpr uint8_t PCF_CONTROL2 = 0x01;
constexpr uint8_t PCF_SECONDS = 0x04;
constexpr uint8_t PCF_STOP = 0x20;
constexpr uint8_t PCF_CLKOUT_MASK = 0x07;  // 111 = CLKOUT off
constexpr uint8_t PCF_OS = 0x80;

// Plausible indoors (proposal).
constexpr int TEMP_MIN_C = 10;
constexpr int TEMP_MAX_C = 40;
constexpr unsigned long SECONDS_CHECK_MS = 60000;
constexpr long SECONDS_TOLERANCE_MS = 1000;
constexpr int BENCH_READS = 1000;
constexpr uint32_t BENCH_SLOW_HZ = 100000;
constexpr uint32_t BENCH_FAST_HZ = 400000;
// A written time must read back within this of what was written.
constexpr int64_t SET_READBACK_MAX_S = 2;
constexpr unsigned long REDRAW_MS = 15000;

constexpr int QUESTION_TIMING = 1;
constexpr int QUESTION_FITTED = 2;
constexpr const char* TIMING_ANSWERS[] = {"60 s against the CPU clock", "400 kHz bench, 1000 reads each", "Cancel"};
constexpr char TIMING_HEADING[] = "Timing test";
constexpr char TIMING_BODY[] =
    "The 60 s check times the RTC's seconds against the CPU clock. The bench reads the time 1000 times at 100 kHz, "
    "then at 400 kHz (the touch controller shares the bus), and puts the clock back.";
constexpr const char* FITTED_ANSWERS[] = {"None fitted", "U13 or U14 fitted"};
constexpr char FITTED_HEADING[] = "RTC fitted?";
constexpr char FITTED_BODY[] = "Nothing answers at 0x68 or 0x51. Is an RTC (U13 DS3231MZ or U14 RV-8263) fitted?";

constexpr char INTRO[] =
    "Confirm records the register checks. Left sets the time by hand (local time). Right: the 60 s check or the "
    "400 kHz bench. After a cell insert OSF reads 1 until the time is set (here or T15).";
constexpr char SET_INTRO[] =
    "Left/Right (or Up/Down) change the field, Confirm moves to the next; on the last it writes. Back cancels. "
    "Seconds are written as 0: write on the minute.";

enum class Field : uint8_t { Year, Month, Day, Hour, Minute };
constexpr int FIELD_COUNT = 5;

struct Dump {
  bool ok = false;
  uint8_t addr = 0;
  RtcType type = RtcType::None;
  uint8_t len = 0;
  uint8_t raw[DS_REG_COUNT] = {};
  unsigned long latencyUs = 0;
};

struct Bench {
  bool ran = false;
  uint32_t hz = 0;
  int reads = 0;
  int errors = 0;
  unsigned long meanUs = 0;
  unsigned long maxUs = 0;
};

uint8_t bcd(const uint8_t v) { return static_cast<uint8_t>((v >> 4) * 10U + (v & 0x0FU)); }

int daysInMonth(const int year, const int month) {
  constexpr int DAYS[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  const bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
  return month == 2 && leap ? 29 : DAYS[(month - 1) % 12];
}

class RtcTest final : public TestScreen {
 public:
  RtcTest(const TestInfo& info, GfxRenderer& renderer, MappedInputManager& mappedInput)
      : TestScreen(info, renderer, mappedInput) {}

 private:
  enum class Mode : uint8_t { View, Set };

  void onStart() override {
    probe();
    readDump();
    validTime = halClock.hasValidTime();
  }

  void onTick() override {
    if (!secondsRunning) return;
    const unsigned long now = millis();
    if (now - secondsStartMs >= SECONDS_CHECK_MS) {
      finishSecondsCheck();
    } else if (now - drawnAt >= REDRAW_MS) {
      redraw();
    }
  }

  const char* slotLabel(const Slot slot) const override {
    if (mode == Mode::Set) {
      switch (slot) {
        case Slot::Confirm:
          return field == Field::Minute ? "Write" : "Next";
        case Slot::Left:
          return "-";
        case Slot::Right:
          return "+";
        default:
          return nullptr;
      }
    }
    if (secondsRunning) return nullptr;
    switch (slot) {
      case Slot::Confirm:
        return "Record";
      case Slot::Left:
        return dump.addr != 0 ? "Set time" : nullptr;
      case Slot::Right:
        return dump.addr != 0 ? "Timing" : nullptr;
      default:
        return nullptr;
    }
  }

  void onSlot(const Slot slot) override {
    if (mode == Mode::Set) {
      if (slot == Slot::Confirm) nextField();
      if (slot == Slot::Left) adjustField(-1);
      if (slot == Slot::Right) adjustField(+1);
      return;
    }
    if (secondsRunning) return;
    if (slot == Slot::Confirm) recordRegisters();
    if (slot == Slot::Left && dump.addr != 0) startSet();
    if (slot == Slot::Right && dump.addr != 0) ask(QUESTION_TIMING, TIMING_HEADING, TIMING_BODY, TIMING_ANSWERS, 3);
  }

  void onStep(const int direction) override {
    if (mode == Mode::Set) adjustField(direction);
  }

  bool onBack() override {
    if (mode != Mode::Set) return false;
    mode = Mode::View;
    redraw();
    return true;
  }

  void onAnswer(const int question, const int answer) override {
    if (question == QUESTION_TIMING) {
      if (answer == 0) startSecondsCheck();
      if (answer == 1) runBench();
      return;
    }
    if (question == QUESTION_FITTED && answer >= 0) {
      // The board runs without an RTC (rtc-devices: optional); a fitted one must answer.
      record(answer == 0 ? Verdict::Pass : Verdict::Fail, "action=registers rtc=none fitted=%s",
             answer == 0 ? "no" : "yes");
    }
  }

  void redraw() {
    drawnAt = millis();
    requestUpdate();
  }

  // --- reads -----------------------------------------------------------------

  void probe() {
    const auto& s = BoardConfig::ACTIVE.sensors;
    ackPrimary = s.rtcAddr != 0 && ackAt(s.rtcAddr);
    ackFallback = s.rtcFallbackAddr != 0 && ackAt(s.rtcFallbackAddr);
  }

  static bool ackAt(const uint8_t addr) {
    const auto scan = freeink::i2c::scanSensorBus(0, addr, addr);
    return scan.ok && scan.acked(addr);
  }

  // The RTC HalClock began at boot, or else the first profile address that answers now.
  void resolveRtc(uint8_t& addr, RtcType& type) const {
    const auto& s = BoardConfig::ACTIVE.sensors;
    if (rtc::present()) {
      addr = rtc::driver().address();
      type = rtc::driver().type();
    } else if (ackPrimary) {
      addr = s.rtcAddr;
      type = s.rtcType;
    } else if (ackFallback) {
      addr = s.rtcFallbackAddr;
      type = s.rtcFallbackType;
    } else {
      addr = 0;
      type = RtcType::None;
    }
  }

  void readDump() {
    Dump next;
    resolveRtc(next.addr, next.type);
    if (next.addr != 0) {
      next.len = next.type == RtcType::Ds3231 ? DS_REG_COUNT : PCF_REG_COUNT;
      const unsigned long start = micros();
      next.ok = rtc::readRegisters(next.addr, 0x00, next.raw, next.len);
      next.latencyUs = micros() - start;
      ++reads;
      if (!next.ok) ++errors;
    }
    dump = next;
  }

  bool isDs3231() const { return dump.type == RtcType::Ds3231; }

  // The time in the registers, whatever the valid flag says.
  bool registerTime(Rtc::DateTime& utc) const {
    if (!dump.ok) return false;
    const uint8_t* t = isDs3231() ? dump.raw : dump.raw + PCF_SECONDS;
    utc.second = bcd(t[0] & 0x7F);
    utc.minute = bcd(t[1] & 0x7F);
    utc.hour = bcd(t[2] & 0x3F);
    if (isDs3231()) {
      utc.day = bcd(t[4] & 0x3F);
      utc.month = bcd(t[5] & 0x1F);
    } else {
      utc.day = bcd(t[3] & 0x3F);
      utc.month = bcd(t[5] & 0x1F);
    }
    utc.year = static_cast<uint16_t>(2000 + bcd(t[6]));
    return utc.month >= 1 && utc.month <= 12 && utc.day >= 1 && utc.day <= 31;
  }

  // The flag that says the time was lost: OSF (DS3231) or OS (PCF85063).
  bool timeLostFlag() const {
    if (!dump.ok) return false;
    return isDs3231() ? (dump.raw[DS_STATUS] & DS_OSF) != 0 : (dump.raw[PCF_SECONDS] & PCF_OS) != 0;
  }

  // Degrees C x 4.
  int temperatureQuarters() const {
    const auto msb = static_cast<int8_t>(dump.raw[DS_TEMP_MSB]);
    return msb * 4 + (dump.raw[DS_TEMP_LSB] >> 6);
  }

  // --- register checks -------------------------------------------------------

  void recordRegisters() {
    probe();
    readDump();
    validTime = halClock.hasValidTime();
    if (!ackPrimary && !ackFallback && dump.addr == 0) {
      ask(QUESTION_FITTED, FITTED_HEADING, FITTED_BODY, FITTED_ANSWERS, 2);
      return;
    }
    char fails[96] = "";
    if (ackPrimary && ackFallback) addFailure(fails, sizeof(fails), "two_rtcs");
    if (!ackPrimary && !ackFallback) addFailure(fails, sizeof(fails), "no_ack");
    if (!rtc::present()) addFailure(fails, sizeof(fails), "not_begun");
    if (!dump.ok) addFailure(fails, sizeof(fails), "read");
    char bits[96] = "";
    if (dump.ok && isDs3231()) {
      const uint8_t control = dump.raw[DS_CONTROL];
      const uint8_t status = dump.raw[DS_STATUS];
      if ((status & DS_EN32KHZ) != 0 && BoardConfig::ACTIVE.sensors.rtcLowPower)
        addFailure(fails, sizeof(fails), "en32khz");
      if ((control & DS_EOSC) != 0) addFailure(fails, sizeof(fails), "eosc");
      if (setDone && (status & DS_OSF) != 0) addFailure(fails, sizeof(fails), "osf_after_set");
      const int quarters = temperatureQuarters();
      if (quarters < TEMP_MIN_C * 4 || quarters > TEMP_MAX_C * 4) addFailure(fails, sizeof(fails), "temp");
      snprintf(bits, sizeof(bits), "osf=%d en32khz=%d bsy=%d eosc=%d intcn=%d temp=%d.%02d aging=%d",
               (status & DS_OSF) != 0 ? 1 : 0, (status & DS_EN32KHZ) != 0 ? 1 : 0, (status & DS_BSY) != 0 ? 1 : 0,
               (control & DS_EOSC) != 0 ? 1 : 0, (control & DS_INTCN) != 0 ? 1 : 0, quarters / 4,
               (quarters < 0 ? -quarters : quarters) % 4 * 25, static_cast<int8_t>(dump.raw[DS_AGING]));
    } else if (dump.ok) {
      const uint8_t control1 = dump.raw[PCF_CONTROL1];
      const uint8_t control2 = dump.raw[PCF_CONTROL2];
      if ((control1 & PCF_STOP) != 0) addFailure(fails, sizeof(fails), "stop");
      if (BoardConfig::ACTIVE.sensors.rtcLowPower && (control2 & PCF_CLKOUT_MASK) != PCF_CLKOUT_MASK) {
        addFailure(fails, sizeof(fails), "clkout");
      }
      if (setDone && (dump.raw[PCF_SECONDS] & PCF_OS) != 0) addFailure(fails, sizeof(fails), "os_after_set");
      snprintf(bits, sizeof(bits), "os=%d stop=%d clkout=%u", (dump.raw[PCF_SECONDS] & PCF_OS) != 0 ? 1 : 0,
               (control1 & PCF_STOP) != 0 ? 1 : 0, static_cast<unsigned>(control2 & PCF_CLKOUT_MASK));
    }
    char when[24] = "unreadable";
    Rtc::DateTime utc{};
    if (registerTime(utc)) {
      snprintf(when, sizeof(when), "%04u-%02u-%02uT%02u:%02u:%02uZ", utc.year, utc.month, utc.day, utc.hour, utc.minute,
               utc.second);
    }
    const auto& s = BoardConfig::ACTIVE.sensors;
    record(fails[0] == '\0' ? Verdict::Pass : Verdict::Fail,
           "action=registers rtc=%s@0x%02x ack_0x%02x=%d ack_0x%02x=%d %s time=%s valid=%d reads=%d errors=%d "
           "latency=%luus set_here=%d fails=%s",
           rtcTypeName(dump.type), dump.addr, s.rtcAddr, ackPrimary ? 1 : 0, s.rtcFallbackAddr, ackFallback ? 1 : 0,
           bits, when, validTime ? 1 : 0, reads, errors, dump.latencyUs, setDone ? 1 : 0,
           fails[0] == '\0' ? "none" : fails);
  }

  // --- set by hand -----------------------------------------------------------

  void startSet() {
    Rtc::DateTime utc{};
    struct tm local{};
    if (registerTime(utc) && utc.year >= 2024 && !timeLostFlag()) {
      const time_t epoch = static_cast<time_t>(rtc::toEpoch(utc));
      localtime_r(&epoch, &local);
      edit[0] = local.tm_year + 1900;
      edit[1] = local.tm_mon + 1;
      edit[2] = local.tm_mday;
      edit[3] = local.tm_hour;
      edit[4] = local.tm_min;
    } else {
      edit[0] = 2026;
      edit[1] = 1;
      edit[2] = 1;
      edit[3] = 12;
      edit[4] = 0;
    }
    field = Field::Year;
    mode = Mode::Set;
    redraw();
  }

  void nextField() {
    if (field == Field::Minute) {
      writeTime();
      return;
    }
    field = static_cast<Field>(static_cast<uint8_t>(field) + 1);
    redraw();
  }

  void adjustField(const int delta) {
    const int i = static_cast<int>(field);
    constexpr int MIN[] = {2024, 1, 1, 0, 0};
    const int max[] = {2099, 12, daysInMonth(edit[0], edit[1]), 23, 59};
    int value = edit[i] + delta;
    if (value < MIN[i]) value = max[i];
    if (value > max[i]) value = MIN[i];
    edit[i] = value;
    // A shorter month clamps the day.
    const int days = daysInMonth(edit[0], edit[1]);
    if (edit[2] > days) edit[2] = days;
    redraw();
  }

  void writeTime() {
    mode = Mode::View;
    struct tm local{};
    local.tm_year = edit[0] - 1900;
    local.tm_mon = edit[1] - 1;
    local.tm_mday = edit[2];
    local.tm_hour = edit[3];
    local.tm_min = edit[4];
    local.tm_sec = 0;
    local.tm_isdst = -1;
    const time_t epoch = mktime(&local);  // the configured zone (HalClock::setTimezone)
    Rtc::DateTime utc{};
    rtc::fromEpoch(static_cast<int64_t>(epoch), utc);
    const bool written = rtc::present() && rtc::driver().set(utc);
    if (written) {
      setDone = true;
      rtc::noteWritten();
    }
    readDump();
    validTime = halClock.hasValidTime();
    Rtc::DateTime back{};
    const bool readBack = registerTime(back);
    const int64_t drift = readBack ? rtc::toEpoch(back) - static_cast<int64_t>(epoch) : -1;
    char fails[48] = "";
    if (!written) addFailure(fails, sizeof(fails), "write");
    if (!readBack || drift < 0 || drift > SET_READBACK_MAX_S) addFailure(fails, sizeof(fails), "readback");
    if (timeLostFlag()) addFailure(fails, sizeof(fails), isDs3231() ? "osf_after_set" : "os_after_set");
    if (!validTime) addFailure(fails, sizeof(fails), "not_valid");
    LOG_INF("TEST", "T13: RTC set to %04d-%02d-%02d %02d:%02d local (epoch %ld)", edit[0], edit[1], edit[2], edit[3],
            edit[4], static_cast<long>(epoch));
    record(fails[0] == '\0' ? Verdict::Pass : Verdict::Fail,
           "action=set local=%04d-%02d-%02dT%02d:%02d utc=%04u-%02u-%02uT%02u:%02u:00Z readback_s=%ld lost_flag=%d "
           "valid=%d fails=%s",
           edit[0], edit[1], edit[2], edit[3], edit[4], utc.year, utc.month, utc.day, utc.hour, utc.minute,
           static_cast<long>(drift), timeLostFlag() ? 1 : 0, validTime ? 1 : 0, fails[0] == '\0' ? "none" : fails);
  }

  // --- 60 s check ------------------------------------------------------------

  void startSecondsCheck() {
    if (!rtc::readAtTick(secondsStartEpoch, secondsStartMs)) {
      ++errors;
      record(Verdict::Skip, "action=seconds reason=%s", halClock.hasValidTime() ? "no_tick" : "time_not_valid");
      return;
    }
    secondsRunning = true;
    LOG_INF("TEST", "T13: 60 s check started");
    redraw();
  }

  void finishSecondsCheck() {
    secondsRunning = false;
    uint32_t endEpoch = 0;
    unsigned long endMs = 0;
    if (!rtc::readAtTick(endEpoch, endMs)) {
      ++errors;
      record(Verdict::Fail, "action=seconds reason=no_tick_at_end");
      return;
    }
    const long rtcMs = static_cast<long>(endEpoch - secondsStartEpoch) * 1000L;
    const long cpuMs = static_cast<long>(endMs - secondsStartMs);
    secondsErrorMs = rtcMs - cpuMs;
    secondsDone = true;
    const bool ok = secondsErrorMs >= -SECONDS_TOLERANCE_MS && secondsErrorMs <= SECONDS_TOLERANCE_MS;
    record(ok ? Verdict::Pass : Verdict::Fail, "action=seconds rtc_s=%ld cpu_ms=%ld error_ms=%ld limit_ms=%ld",
           rtcMs / 1000L, cpuMs, secondsErrorMs, SECONDS_TOLERANCE_MS);
  }

  // --- bench -----------------------------------------------------------------

  void runBench() {
    auto& wire = freeink::i2c::sensorWire();
    const uint32_t ownHz = wire.getClock();
    slow = benchAt(BENCH_SLOW_HZ);
    fast = benchAt(BENCH_FAST_HZ);
    if (ownHz != 0) wire.setClock(ownHz);
    const bool ok = slow.ran && fast.ran && slow.errors == 0 && fast.errors == 0;
    record(ok ? Verdict::Pass : Verdict::Fail,
           "action=bench rtc=0x%02x reads=%d errors_100k=%d errors_400k=%d mean_100k=%luus max_100k=%luus "
           "mean_400k=%luus max_400k=%luus clock_back=%lu",
           dump.addr, BENCH_READS, slow.errors, fast.errors, slow.meanUs, slow.maxUs, fast.meanUs, fast.maxUs,
           static_cast<unsigned long>(wire.getClock()));
  }

  Bench benchAt(const uint32_t hz) const {
    Bench bench;
    if (dump.addr == 0) return bench;
    auto& wire = freeink::i2c::sensorWire();
    if (!wire.setClock(hz)) return bench;
    bench.ran = true;
    bench.hz = wire.getClock();
    uint64_t totalUs = 0;
    uint8_t buf[7];
    for (int i = 0; i < BENCH_READS; ++i) {
      const unsigned long start = micros();
      const bool ok = rtc::readRegisters(dump.addr, isDs3231() ? 0x00 : PCF_SECONDS, buf, sizeof(buf));
      const unsigned long took = micros() - start;
      ++bench.reads;
      if (!ok) ++bench.errors;
      totalUs += took;
      if (took > bench.maxUs) bench.maxUs = took;
      if (i % 100 == 99) delay(1);  // let the other tasks run
    }
    bench.meanUs = static_cast<unsigned long>(totalUs / static_cast<uint64_t>(bench.reads));
    return bench;
  }

  // --- screen ----------------------------------------------------------------

  void drawBody(TestPage& page) const override {
    if (mode == Mode::Set) {
      drawSet(page);
      return;
    }
    page.paragraph(INTRO, 4);
    page.gap(4);
    page.setCompact(true);
    const auto& s = BoardConfig::ACTIVE.sensors;
    page.rowf("Bus", "SDA IO%d, SCL IO%d, %lu kHz", s.i2cSda, s.i2cScl, static_cast<unsigned long>(s.i2cHz / 1000));
    char ackLabel[32];
    snprintf(ackLabel, sizeof(ackLabel), "ACK 0x%02x / 0x%02x", s.rtcAddr, s.rtcFallbackAddr);
    page.rowf(ackLabel, "%s / %s", ackPrimary ? "yes" : "no", ackFallback ? "yes" : "no");
    if (dump.addr == 0) {
      page.row("RTC", "none answered");
      page.setCompact(false);
      return;
    }
    page.rowf("RTC", "%s at 0x%02x%s", isDs3231() ? "DS3231MZ (U13)" : "RV-8263 (U14)", dump.addr,
              rtc::present() ? "" : " (not begun at boot)");
    Rtc::DateTime utc{};
    if (registerTime(utc)) {
      page.rowf("Registers' time (UTC)", "%04u-%02u-%02u %02u:%02u:%02u", utc.year, utc.month, utc.day, utc.hour,
                utc.minute, utc.second);
    } else {
      page.row("Registers' time (UTC)", dump.ok ? "not a date" : "read failed");
    }
    page.rowf("hasValidTime()", "%s", validTime ? "yes" : timeLostFlag() ? "no: time lost flag" : "no");
    if (dump.ok && isDs3231()) {
      const uint8_t control = dump.raw[DS_CONTROL];
      const uint8_t status = dump.raw[DS_STATUS];
      page.rowf("OSF / EN32KHZ / BSY (0x0F)", "%d / %d / %d", (status & DS_OSF) != 0 ? 1 : 0,
                (status & DS_EN32KHZ) != 0 ? 1 : 0, (status & DS_BSY) != 0 ? 1 : 0);
      page.rowf("EOSC / INTCN (0x0E)", "%d / %d", (control & DS_EOSC) != 0 ? 1 : 0, (control & DS_INTCN) != 0 ? 1 : 0);
      const int quarters = temperatureQuarters();
      page.rowf("Temperature / aging", "%d.%02d C / %d", quarters / 4, (quarters < 0 ? -quarters : quarters) % 4 * 25,
                static_cast<int8_t>(dump.raw[DS_AGING]));
    } else if (dump.ok) {
      page.rowf("OS / STOP / CLKOUT", "%d / %d / %u", (dump.raw[PCF_SECONDS] & PCF_OS) != 0 ? 1 : 0,
                (dump.raw[PCF_CONTROL1] & PCF_STOP) != 0 ? 1 : 0,
                static_cast<unsigned>(dump.raw[PCF_CONTROL2] & PCF_CLKOUT_MASK));
    }
    if (dump.ok) {
      for (uint8_t start = 0; start < dump.len; start = static_cast<uint8_t>(start + 7)) {
        char label[8];
        char hex[32] = "";
        snprintf(label, sizeof(label), "0x%02X", start);
        for (uint8_t i = start; i < dump.len && i < start + 7; ++i) appendf(hex, sizeof(hex), "%02X ", dump.raw[i]);
        page.row(label, hex);
      }
    }
    page.rowf("Reads / errors / last latency", "%d / %d / %lu us", reads, errors, dump.latencyUs);
    if (secondsRunning) {
      const unsigned long elapsed = (millis() - secondsStartMs) / 1000UL;
      page.rowf("60 s check", "running, %lu s in", elapsed);
    } else if (secondsDone) {
      page.rowf("60 s check", "RTC - CPU = %+ld ms", secondsErrorMs);
    }
    if (slow.ran || fast.ran) {
      page.rowf("Bench 100 kHz", "%d errors, mean %lu us, max %lu us", slow.errors, slow.meanUs, slow.maxUs);
      page.rowf("Bench 400 kHz", "%d errors, mean %lu us, max %lu us", fast.errors, fast.meanUs, fast.maxUs);
    }
    page.setCompact(false);
  }

  void drawSet(TestPage& page) const {
    page.heading("Set the time (local)");
    page.paragraph(SET_INTRO, 3);
    page.gap(8);
    constexpr const char* NAMES[] = {"Year", "Month", "Day", "Hour", "Minute"};
    for (int i = 0; i < FIELD_COUNT; ++i) {
      char label[24];
      snprintf(label, sizeof(label), "%s%s", i == static_cast<int>(field) ? "> " : "  ", NAMES[i]);
      page.rowf(label, i == 0 ? "%04d" : "%02d", edit[i]);
    }
  }

  Mode mode = Mode::View;
  Field field = Field::Year;
  int edit[FIELD_COUNT] = {};
  bool ackPrimary = false;
  bool ackFallback = false;
  bool validTime = false;
  bool setDone = false;
  Dump dump;
  int reads = 0;
  int errors = 0;
  bool secondsRunning = false;
  bool secondsDone = false;
  uint32_t secondsStartEpoch = 0;
  unsigned long secondsStartMs = 0;
  long secondsErrorMs = 0;
  Bench slow;
  Bench fast;
  unsigned long drawnAt = 0;
};

}  // namespace

std::unique_ptr<Activity> createRtcTest(const TestInfo& info, GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<RtcTest>(info, renderer, mappedInput);
}

}  // namespace testsuite
#endif

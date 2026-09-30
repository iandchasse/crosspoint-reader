#if CROSSPOINT_TEST_SUITE
// P1: system info and boot checks (bring-up B1). Read-only: nothing here writes
// an eFuse, consumes a reset record or changes a clock.
#include <Arduino.h>
#include <BatteryMonitor.h>
#include <BoardConfig.h>
#include <HalClock.h>
#include <HalFrontlight.h>
#include <HalGPIO.h>
#include <HalMemory.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <Memory.h>
#include <PowerManager.h>
#include <WiFi.h>
#include <esp_arduino_version.h>
#include <esp_efuse.h>
#include <esp_efuse_table.h>
#include <esp_idf_version.h>
#include <esp_sleep.h>
#include <esp_system.h>

#include <cstdio>
#include <cstring>

#include "PanelProbe.h"
#include "TestFactories.h"
#include "TestNames.h"
#include "TextBuffer.h"
#include "activities/testsuite/TestCatalog.h"
#include "activities/testsuite/TestScreen.h"
#include "activities/testsuite/TestSuite.h"
#include "activities/testsuite/TestSuiteAccess.h"
#include "network/FirmwareBoardTag.h"

namespace testsuite {
namespace {

// The module the board carries: ESP32-S3-WROOM-1-N16R8 (HARDWARE.md §4).
constexpr uint32_t EXPECTED_FLASH_BYTES = 16UL * 1024UL * 1024UL;
// 8 MB octal PSRAM, less what the allocator keeps for itself.
constexpr uint32_t MIN_PSRAM_BYTES = 7UL * 1024UL * 1024UL;
// With no input, the loop drops to HalPowerManager::LOW_POWER_FREQ after
// IDLE_POWER_SAVING_MS; well past that it counts as a failure.
constexpr unsigned long IDLE_CLOCK_LATE_MS = 10000;

constexpr char INTRO[] =
    "Open after a cold boot and after a power-button wake: each opening logs the panel controller and the boot. "
    "Leave the keys 3 s: the idle clock logs itself.";

class SystemInfoTest final : public TestScreen {
 public:
  SystemInfoTest(const TestInfo& info, GfxRenderer& renderer, MappedInputManager& mappedInput)
      : TestScreen(info, renderer, mappedInput) {}

 private:
  // Taken once when the screen opens; none of it changes while the board runs.
  struct Snapshot {
    const char* chip = "";
    uint16_t chipRevision = 0;
    uint8_t cores = 0;
    uint32_t flashBytes = 0;
    uint32_t psramBytes = 0;
    esp_reset_reason_t reset = ESP_RST_UNKNOWN;
    esp_sleep_wakeup_cause_t wake = ESP_SLEEP_WAKEUP_UNDEFINED;
    uint64_t ext1Mask = 0;
    freeink::PowerManager::BootResetRecord bootRecord;
    uint8_t abnormalNow = 0;
    bool abortedPending = false;
    bool pinPowerSelection = false;
    uint32_t normalMhz = 0;
    uint8_t touchAddr = 0;  // where the driver talks to the controller (a GT911 may use its alternate)
  };

  enum class IdleClock : uint8_t { Waiting, Reached, Held, Late };

  void onStart() override {
    snap.chip = ESP.getChipModel();
    snap.chipRevision = ESP.getChipRevision();
    snap.cores = ESP.getChipCores();
    snap.flashBytes = ESP.getFlashChipSize();
    snap.psramBytes = ESP.getPsramSize();
    snap.reset = esp_reset_reason();
    snap.wake = esp_sleep_get_wakeup_cause();
    snap.ext1Mask = snap.wake == ESP_SLEEP_WAKEUP_EXT1 ? esp_sleep_get_ext1_wakeup_status() : 0;
    // Non-consuming reads: main.cpp has already counted and taken these records.
    snap.bootRecord = freeink::PowerManager::bootResetRecord();
    snap.abnormalNow = freeink::PowerManager::peekAbnormalResets();
    snap.abortedPending = freeink::PowerManager::peekAbortedSleepInfo().aborted;
#if CONFIG_IDF_TARGET_ESP32S3
    // Informational only (HW-01, POWER.md §4); read, never written.
    snap.pinPowerSelection = esp_efuse_read_field_bit(ESP_EFUSE_PIN_POWER_SELECTION);
#endif
    snap.normalMhz = getCpuFrequencyMhz();
    snap.touchAddr = TestSuiteAccess::input(gpio).touchDiagnostics().address;
    if (snap.touchAddr == 0) snap.touchAddr = BoardConfig::ACTIVE.touch.i2cAddress;
    hostAttached = usbHostAttached();
    recordPanel();
    recordChecks();
  }

  void onTick() override {
    // The render task reads these; usbHostAttached() is loop-task only.
    const bool host = usbHostAttached();
    if (host != hostAttached) {
      hostAttached = host;
      requestUpdate();
    }
    if (idle == IdleClock::Reached) return;
    if (getCpuFrequencyMhz() <= static_cast<uint32_t>(HalPowerManager::LOW_POWER_FREQ)) {
      idle = IdleClock::Reached;
      idleAfterMs = msSinceInput();
      recordIdleClock();
      requestUpdate();
      return;
    }
    // A USB data host or the radio holds the full clock by design.
    const IdleClock now = host || WiFi.getMode() != WIFI_MODE_NULL ? IdleClock::Held
                          : msSinceInput() >= IDLE_CLOCK_LATE_MS   ? IdleClock::Late
                                                                   : IdleClock::Waiting;
    if (now != idle) {
      idle = now;
      if (idle == IdleClock::Late) recordIdleClock();
      requestUpdate();
    }
  }

  // The idle clock's own row, once per opening: PASS when it reached the low
  // clock, FAIL when it was late with nothing holding it. Held records nothing.
  void recordIdleClock() {
    if (idleRecorded) return;
    idleRecorded = true;
    if (idle == IdleClock::Reached) {
      record(Verdict::Pass, "action=idle_clock idle_clock=reached mhz=%d after_ms=%lu normal_mhz=%lu fails=none",
             HalPowerManager::LOW_POWER_FREQ, idleAfterMs, static_cast<unsigned long>(snap.normalMhz));
    } else {
      record(Verdict::Fail, "action=idle_clock idle_clock=late mhz=%lu after_ms=%lu fails=idle_clock",
             static_cast<unsigned long>(getCpuFrequencyMhz()), IDLE_CLOCK_LATE_MS);
    }
  }

  const char* slotLabel(const Slot slot) const override { return slot == Slot::Confirm ? "Record" : nullptr; }

  void onSlot(const Slot slot) override {
    if (slot != Slot::Confirm) return;
    recordPanel();
    recordChecks();
  }

  bool isBoard() const { return BoardConfig::ACTIVE.board == BoardConfig::Board::Silkscreen; }

  // The power button's bit in the ext1 wake mask.
  static uint64_t powerWakeBit() {
    const int8_t pin = BoardConfig::ACTIVE.input.power;
    return pin >= 0 ? (1ULL << pin) : 0;
  }

  // The panel controller the boot resolved (probed, kept across the sleep, or
  // forced by the build) with the probe's IDs, in its own row before the boot row.
  void recordPanel() {
    char fails[24];
    const Verdict verdict = panelProbeVerdict(fails, sizeof(fails));
    formatPanelProbe(panelLine, sizeof(panelLine));
    record(verdict, "action=panel %s fails=%s", panelLine, fails);
  }

  // The boot checks. The idle clock is logged here as it stands but judged in
  // its own row (recordIdleClock()).
  void recordChecks() {
    char failed[96] = "";
    const auto fail = [&failed](const char* what) { addFailure(failed, sizeof(failed), what); };
    if (!isBoard()) fail("board");
    if (snap.flashBytes != EXPECTED_FLASH_BYTES) fail("flash");
    if (snap.psramBytes < MIN_PSRAM_BYTES) fail("psram");
    if (snap.bootRecord.counted && snap.bootRecord.abnormalResets > 0) fail("abnormal_resets");
    if (snap.bootRecord.abortedSleepTaken && snap.bootRecord.abortedSleep.aborted) fail("aborted_sleep");
    // A deep-sleep wake has one source on this board: ext1 on the power button.
    if (snap.reset == ESP_RST_DEEPSLEEP &&
        (snap.wake != ESP_SLEEP_WAKEUP_EXT1 || (snap.ext1Mask & powerWakeBit()) == 0)) {
      fail("wake");
    }
    char panelFails[24];
    if (panelProbeVerdict(panelFails, sizeof(panelFails)) == Verdict::Fail) fail("panel");
    char panel[40];
    formatPanelToken(panel, sizeof(panel));
    // Read here, on the loop task: the render task keeps off the RTC's bus.
    rtcValid = halClock.isAvailable() && halClock.hasValidTime();

    char mask[24];
    formatWakeMask(mask, sizeof(mask), snap.ext1Mask);
    const auto& touch = BoardConfig::ACTIVE.touch;
    record(
        failed[0] == '\0' ? Verdict::Pass : Verdict::Fail,
        "action=boot board=%s tag=%.*s flash=%luMB psram=%luKB reset=%s wake=%s ext1=%s abnormal=%u now=%u aborted=%d "
        "pin_power_sel=%d sd=%d rtc=%d rtc_valid=%d touch=%s@0x%02x light=%d panel=%s idle_clock=%s fails=%s",
        BoardConfig::ACTIVE.name, static_cast<int>(board_tag::boardNameLen()), board_tag::boardName(),
        static_cast<unsigned long>(snap.flashBytes >> 20), static_cast<unsigned long>(snap.psramBytes >> 10),
        resetReasonName(snap.reset), wakeCauseName(snap.wake), mask, snap.bootRecord.abnormalResets, snap.abnormalNow,
        snap.bootRecord.abortedSleep.aborted ? 1 : 0, snap.pinPowerSelection ? 1 : 0, Storage.ready() ? 1 : 0,
        halClock.isAvailable() ? 1 : 0, rtcValid ? 1 : 0,
        gpio.hasTouch() ? touchControllerName(touch.controller) : "none", snap.touchAddr, Frontlight.present() ? 1 : 0,
        panel, idleClockName(), failed[0] == '\0' ? "none" : failed);
  }

  const char* idleClockName() const {
    switch (idle) {
      case IdleClock::Reached:
        return "reached";
      case IdleClock::Held:
        return "held";
      case IdleClock::Late:
        return "late";
      case IdleClock::Waiting:
        break;
    }
    return "waiting";
  }

  void drawBody(TestPage& page) const override {
    page.paragraph(INTRO, 3);
    page.gap(4);
    page.setCompact(true);
    page.rowf("Profile / board tag", "%s / %.*s%s", BoardConfig::ACTIVE.name,
              static_cast<int>(board_tag::boardNameLen()), board_tag::boardName(), isBoard() ? "" : " (wrong board)");
    page.row("Firmware", CROSSPOINT_VERSION);
    page.rowf("Chip", "%s rev %u.%u, %u cores", snap.chip, snap.chipRevision / 100U, snap.chipRevision % 100U,
              snap.cores);
    drawPanelProbe(page, /*brief=*/false);
    page.rowf("ESP-IDF / Arduino core", "%s / %s", esp_get_idf_version(), ESP_ARDUINO_VERSION_STR);
    page.rowf("Flash / PSRAM", "%lu MB%s / %lu KB%s", static_cast<unsigned long>(snap.flashBytes >> 20),
              snap.flashBytes == EXPECTED_FLASH_BYTES ? "" : " (want 16)",
              static_cast<unsigned long>(snap.psramBytes >> 10), snap.psramBytes >= MIN_PSRAM_BYTES ? "" : " (low)");
    const auto heap = HalMemory::getInternalHeap();
    const auto psram = HalMemory::getPsramHeap();
    page.rowf("Heap free (min) / PSRAM free", "%u (%u) / %u KB", static_cast<unsigned>(heap.freeBytes >> 10),
              static_cast<unsigned>(heap.minFreeBytes >> 10), static_cast<unsigned>(psram.freeBytes >> 10));
    switch (idle) {
      case IdleClock::Reached:
        page.rowf("Idle clock", "%d MHz after %.1f s (normal %lu)", HalPowerManager::LOW_POWER_FREQ,
                  static_cast<double>(idleAfterMs) / 1000.0, static_cast<unsigned long>(snap.normalMhz));
        break;
      case IdleClock::Held:
        page.rowf("Idle clock", "held at %lu MHz (USB host or radio)", static_cast<unsigned long>(snap.normalMhz));
        break;
      case IdleClock::Late:
        page.rowf("Idle clock", "NOT reached after %lu s", IDLE_CLOCK_LATE_MS / 1000UL);
        break;
      case IdleClock::Waiting:
        page.row("Idle clock", "waiting: leave the keys 3 s");
        break;
    }

    char mask[24];
    formatWakeMask(mask, sizeof(mask), snap.ext1Mask);
    page.rowf("Reset / wake / ext1 mask", "%s / %s / %s", resetReasonName(snap.reset), wakeCauseName(snap.wake), mask);
    if (snap.reset == ESP_RST_DEEPSLEEP) {
      const bool ok = snap.wake == ESP_SLEEP_WAKEUP_EXT1 && (snap.ext1Mask & powerWakeBit()) != 0;
      page.rowf("Power-button wake", "%s (IO%d)", ok ? "yes" : "NO", BoardConfig::ACTIVE.input.power);
    }
    page.rowf("Abnormal resets (boot / now), aborted sleep", "%u / %u, %s%s", snap.bootRecord.abnormalResets,
              snap.abnormalNow, snap.bootRecord.abortedSleep.aborted ? "YES" : "none",
              snap.abortedPending ? " (+1 pending)" : "");
    page.rowf("eFuse PIN_POWER_SELECTION", "%d (information)", snap.pinPowerSelection ? 1 : 0);

    page.rowf("SD card / RTC", "%s / %s", Storage.ready() ? "mounted" : "not mounted",
              !halClock.isAvailable() ? "absent"
              : rtcValid              ? "time valid"
                                      : "time lost");
    const auto& touch = BoardConfig::ACTIVE.touch;
    char touchText[24];
    if (gpio.hasTouch()) {
      snprintf(touchText, sizeof(touchText), "%s 0x%02x", touchControllerName(touch.controller), snap.touchAddr);
    } else {
      snprintf(touchText, sizeof(touchText), "not found");
    }
    page.rowf("Touch / front light", "%s / %s", touchText, Frontlight.present() ? "in this build" : "not built");
    page.rowf("USB_STAT / USB host", "%s / %s", chargeStateName(BatteryMonitor::chargeStatus()),
              hostAttached ? "attached" : "none");
    page.setCompact(false);
  }

  Snapshot snap;
  char panelLine[224] = "";  // the panel row being built (off the loop task's stack)
  IdleClock idle = IdleClock::Waiting;
  unsigned long idleAfterMs = 0;
  bool idleRecorded = false;
  bool hostAttached = false;
  bool rtcValid = false;
};

}  // namespace

std::unique_ptr<Activity> createSystemInfoTest(const TestInfo& info, GfxRenderer& renderer,
                                               MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<SystemInfoTest>(info, renderer, mappedInput);
}

}  // namespace testsuite
#endif

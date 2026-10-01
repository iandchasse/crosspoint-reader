#if CROSSPOINT_TEST_SUITE
// P3: the SD card (bring-up B4). The card and link as the mount left them (the
// SDK's bring-up record: type, capacity, CID, bus width, clock, power cycles),
// then a write-read-verify of a scratch file in /.silkscreen-test/ through
// HalStorage, the reader's own path: pseudo-random data, SHA-256 on both sides
// and a byte compare, with the MB/s for a baseline. On battery it needs the
// firmware-write floor (3.6 V), as a brown-out mid-write could damage the FAT
// the reader shares. The optional overlap runs Wi-Fi scans and FAST refreshes
// during the run (sd-clock), on USB only. The power-cycle mode (BRING_UP section
// 9) takes the card through the reader's own sleep shutdown (write-back, unmount,
// bus parked LOW, SD_VDD gated off) and parked mount, ten times, so 3V3 at U3.5
// can be scoped as SD_VDD switches on.
#include <Arduino.h>
#include <BoardConfig.h>
#include <FS.h>  // before SdFat (SDCardManager.h), as HalStorage.cpp includes them
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <SDCardManager.h>
#include <WiFi.h>
#include <esp_err.h>
#include <esp_random.h>

#include <cstdio>
#include <cstring>

#include "Sha256.h"
#include "Supply.h"
#include "TestFactories.h"
#include "TextBuffer.h"
#include "activities/testsuite/TestCatalog.h"
#include "activities/testsuite/TestResultLog.h"
#include "activities/testsuite/TestScreen.h"
#include "activities/testsuite/TestSuiteAccess.h"

namespace testsuite {
namespace {

using CardInfo = SDCardManager::CardInfo;
using CardType = SDCardManager::CardType;

// The scratch file ends in ".tmp": a run cut short by a sleep or a crash leaves
// it for the next boot to remove.
constexpr char TEST_FILE[] = "/.silkscreen-test/sdcheck.tmp";
constexpr uint32_t SIZES_MB[] = {8, 64, 200};
constexpr int SIZE_COUNT = static_cast<int>(sizeof(SIZES_MB) / sizeof(SIZES_MB[0]));
// Left steps through the verify sizes, then the power cycles.
constexpr int MODE_POWER_CYCLE = SIZE_COUNT;
constexpr int MODE_COUNT = SIZE_COUNT + 1;
// Power cycles: SD_VDD off long enough to fall below 0.5 V (R77 bleeds it in
// about 0.11 s with no card; the card adds capacitance), then the parked mount.
constexpr int POWER_CYCLES = 10;
constexpr unsigned long CYCLE_OFF_MS = 1000;
constexpr unsigned long CYCLE_GAP_MS = 1000;  // mounted between two cycles
constexpr size_t CHUNK = 32 * 1024;
static_assert(CHUNK % 4 == 0, "the generator writes 4 bytes at a time");
constexpr unsigned long SLICE_MS = 200;  // work per loop pass, so Back and repaints get through
constexpr unsigned long REDRAW_MS = 10000;
constexpr unsigned long OVERLAP_REDRAW_MS = 2000;  // FAST refreshes during an overlap run
constexpr unsigned long SUPPLY_CHECK_MS = 1000;    // the supply gates again during a run
constexpr uint64_t FREE_MARGIN_BYTES = 16ULL * 1024ULL * 1024ULL;
// The clock the link runs may sit a little below the host cap (divider steps).
constexpr uint32_t CLOCK_MIN_PERCENT = 90;
// SdmmcPins::maxFreqKhz 0 means the esp-idf default.
constexpr uint32_t SDMMC_DEFAULT_KHZ = 20000;

constexpr int QUESTION_FITTED = 1;
constexpr const char* FITTED_ANSWERS[] = {"No card fitted", "A card is fitted"};
constexpr char FITTED_HEADING[] = "SD card fitted?";
constexpr char FITTED_BODY[] =
    "The card is not mounted. With no card, the parked mount must fail cleanly and the menu say so; with a card, "
    "this is a failure.";

constexpr int QUESTION_SCOPE = 2;
constexpr const char* SCOPE_ANSWERS[] = {"Stayed above 3.0 V", "Dipped below 3.0 V", "Not scoped"};
constexpr const char* SCOPE_CODES[] = {"ok", "dip", "not_scoped"};
constexpr char SCOPE_HEADING[] = "3V3 at U3.5";
constexpr char SCOPE_BODY[] = "As SD_VDD switched on at each cycle, did 3V3 at U3.5 stay above 3.0 V?";

constexpr char INTRO[] =
    "Confirm writes, reads back and verifies a scratch file in /.silkscreen-test/, then removes it. Left: size, or "
    "SD power cycles. Right: overlap Wi-Fi scans and screen refreshes (USB only). Back stops a run. On battery a "
    "verify needs 3.6 V.";
constexpr char CYCLE_NOTE[] =
    "Power cycles: 10 times, the card is unmounted, its bus parked LOW and SD_VDD gated off for 1 s, then mounted "
    "again. Scope 3V3 at U3.5 (77.30, 83.20), trigger on SD_VDD (C36) rising: 3V3 must stay above 3.0 V.";

const char* cardTypeName(const CardType type) {
  switch (type) {
    case CardType::Sdsc:
      return "SDSC";
    case CardType::Sdhc:
      return "SDHC";
    case CardType::Sdxc:
      return "SDXC";
    case CardType::Mmc:
      return "MMC";
    case CardType::Unknown:
      break;
  }
  return "unknown";
}

// xorshift32: the same stream for the write and the compare.
void fill(uint8_t* buf, const size_t n, uint32_t& state) {
  for (size_t i = 0; i < n; i += 4) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    memcpy(buf + i, &state, 4);
  }
}

double mbPerSecond(const uint64_t bytes, const unsigned long ms) {
  return ms == 0 ? 0.0 : static_cast<double>(bytes) / 1000.0 / static_cast<double>(ms);
}

class SdCardTest final : public TestScreen {
 public:
  SdCardTest(const TestInfo& info, GfxRenderer& renderer, MappedInputManager& mappedInput)
      : TestScreen(info, renderer, mappedInput) {}

 private:
  // CycleOff: unmounted, bus parked, SD_VDD off. CycleOn: mounted between cycles.
  enum class Phase : uint8_t { Idle, Writing, Reading, CycleOff, CycleOn };

  struct Result {
    bool done = false;
    uint32_t sizeMb = 0;
    bool overlap = false;
    int overlapScans = 0;
    unsigned long writeMs = 0;
    unsigned long readMs = 0;
    bool shaMatch = false;
    long mismatchAt = -1;  // first differing byte, -1 none
    int ioErrors = 0;
    bool removed = false;
    char shaHex[2 * Sha256::SIZE + 1] = "";
  };

  bool running() const { return phase == Phase::Writing || phase == Phase::Reading; }
  bool cycling() const { return phase == Phase::CycleOff || phase == Phase::CycleOn; }
  bool powerCycleMode() const { return modeIndex == MODE_POWER_CYCLE; }
  bool skipLoopDelay() override { return running(); }
  // A run is bounded (200 MB at the slowest card still ends within the ceiling),
  // and so are the power cycles (about 25 s).
  bool preventAutoSleep() override { return running() || cycling(); }

  void onStart() override { readCardInfo(); }

  void onExit() override {
    if (running()) abortRun("exit");
    if (cycling()) stopCycles("exit", /*recordIt=*/false);
    buffer.reset();
    expected.reset();
    TestScreen::onExit();
  }

  bool onBack() override {
    if (cycling()) {
      stopCycles("back", /*recordIt=*/true);
      return true;
    }
    if (!running()) return false;
    abortRun("stopped");
    requestUpdate();
    return true;
  }

  const char* slotLabel(const Slot slot) const override {
    if (running() || cycling()) return nullptr;
    switch (slot) {
      case Slot::Confirm:
        if (!card.mounted) return "Record";
        return powerCycleMode() ? "Cycle" : "Run";
      case Slot::Left:
        return card.mounted ? "Mode" : nullptr;
      case Slot::Right:
        if (!card.mounted || powerCycleMode()) return nullptr;
        return overlap ? "Overlap off" : "Overlap on";
      default:
        return nullptr;
    }
  }

  void onSlot(const Slot slot) override {
    if (running() || cycling()) return;
    if (!card.mounted) {
      if (slot == Slot::Confirm) ask(QUESTION_FITTED, FITTED_HEADING, FITTED_BODY, FITTED_ANSWERS, 2);
      return;
    }
    if (slot == Slot::Confirm) {
      if (powerCycleMode()) {
        startCycles();
      } else {
        startRun();
      }
    }
    if (slot == Slot::Left) {
      modeIndex = (modeIndex + 1) % MODE_COUNT;
      requestUpdate();
    }
    if (slot == Slot::Right) {
      overlap = !overlap;
      requestUpdate();
    }
  }

  void onAnswer(const int question, const int answer) override {
    if (question == QUESTION_SCOPE) {
      recordCycles(answer);
      return;
    }
    if (question != QUESTION_FITTED || answer < 0) return;
    record(answer == 0 ? Verdict::Pass : Verdict::Fail, "mounted=0 fitted=%s attempts=%u last_err=0x%lx (%s)",
           answer == 0 ? "no" : "yes", card.mountAttempts, static_cast<unsigned long>(card.lastMountError),
           esp_err_to_name(static_cast<esp_err_t>(card.lastMountError)));
  }

  void onTick() override {
    if (cycling()) {
      cycleTick();
      return;
    }
    if (!running()) return;
    if (millis() - supplyCheckedAt >= SUPPLY_CHECK_MS && !supplyStillOk()) return;
    if (result.overlap) overlapWork();
    const unsigned long sliceStart = millis();
    while (running() && millis() - sliceStart < SLICE_MS) {
      if (phase == Phase::Writing) {
        writeChunk();
      } else {
        readChunk();
      }
    }
    if (running() && millis() - drawnAt >= (result.overlap ? OVERLAP_REDRAW_MS : REDRAW_MS)) redraw();
  }

  void redraw() {
    drawnAt = millis();
    requestUpdate();
  }

  // The start gates, again once a second during a run: USB pulled mid-run leaves
  // the overlap on the cell (PWR-02), or a plain run below the write floor. The
  // run stops (radio off, file removed) and records SKIP: the operator's pull,
  // not the board's fault.
  bool supplyStillOk() {
    supplyCheckedAt = millis();
    const Supply supply = readSupply();
    if (!supply.onBattery()) return true;
    const uint16_t floorMv = BoardConfig::powerPolicy().firmwareUpdateMinMv;
    const char* why = nullptr;
    if (result.overlap) {
      why = "usb_lost_overlap";
    } else if (floorMv != 0 && supply.pPlusMv < floorMv) {
      why = "battery_floor_during_run";
    } else {
      return true;
    }
    LOG_INF("TEST", "P3: on the cell at %u mV during the run (%s); stopping", supply.pPlusMv, why);
    const unsigned long doneMb = static_cast<unsigned long>(doneBytes >> 20);
    abortRun(why);
    record(Verdict::Skip, "reason=%s pplus=%u done_mb=%lu size_mb=%lu", why, supply.pPlusMv, doneMb,
           static_cast<unsigned long>(result.sizeMb));
    return false;
  }

  // --- card record -----------------------------------------------------------

  void readCardInfo() {
    // The SD manager has no lock of its own: HalStorage's, as its methods take it.
    SemaphoreHandle_t mutex = TestSuiteAccess::storageMutex();
    xSemaphoreTakeRecursive(mutex, portMAX_DELAY);
    auto& sd = SDCardManager::getInstance();
    card = sd.cardInfo();
    totalBytes = card.mounted ? sd.sdTotalBytes() : 0;
    usedBytes = card.mounted ? sd.sdUsedBytes() : 0;
    xSemaphoreGiveRecursive(mutex);
  }

  uint32_t expectedKhz() const {
    const uint16_t cap = BoardConfig::ACTIVE.sdmmc.maxFreqKhz;
    return cap != 0 ? cap : SDMMC_DEFAULT_KHZ;
  }

  bool widthOk() const { return card.busWidth == BoardConfig::ACTIVE.sdmmc.busWidth; }
  bool clockOk() const {
    return card.clockKhz <= expectedKhz() && card.clockKhz * 100U >= expectedKhz() * CLOCK_MIN_PERCENT;
  }

  // --- the run ---------------------------------------------------------------

  void startRun() {
    readCardInfo();
    const uint64_t bytes = static_cast<uint64_t>(SIZES_MB[modeIndex]) * 1024ULL * 1024ULL;
    if (powerManager.firmwareUpdateBlockedByBattery()) {
      record(Verdict::Skip, "reason=battery_floor size_mb=%lu", static_cast<unsigned long>(SIZES_MB[modeIndex]));
      return;
    }
    const Supply supply = readSupply();
    if (overlap && supply.onBattery()) {
      // PWR-02: no Wi-Fi TX under SD writes and refreshes on the cell.
      record(Verdict::Skip, "reason=overlap_on_battery pplus=%u", supply.pPlusMv);
      return;
    }
    if (totalBytes != 0 && totalBytes - usedBytes < bytes + FREE_MARGIN_BYTES) {
      record(Verdict::Skip, "reason=no_space free_mb=%lu size_mb=%lu",
             static_cast<unsigned long>((totalBytes - usedBytes) >> 20),
             static_cast<unsigned long>(SIZES_MB[modeIndex]));
      return;
    }
    if (!buffer) buffer = makeUniqueNoThrow<uint8_t[]>(CHUNK);
    if (!expected) expected = makeUniqueNoThrow<uint8_t[]>(CHUNK);
    if (!buffer || !expected) {
      LOG_ERR("TEST", "OOM: SD test buffers");
      record(Verdict::Fail, "reason=oom");
      return;
    }
    if (!Storage.exists(RESULTS_DIR) && !Storage.mkdir(RESULTS_DIR)) {
      record(Verdict::Fail, "reason=mkdir");
      return;
    }
    if (Storage.exists(TEST_FILE)) Storage.remove(TEST_FILE);
    file = Storage.open(TEST_FILE, O_WRONLY | O_CREAT | O_TRUNC);
    if (!file) {
      record(Verdict::Fail, "reason=open_write");
      return;
    }
    result = Result{};
    result.sizeMb = SIZES_MB[modeIndex];
    result.overlap = overlap;
    totalRun = bytes;
    doneBytes = 0;
    seed = esp_random() | 1U;  // xorshift never leaves 0
    stream = seed;
    sha.restart();
    phase = Phase::Writing;
    phaseStart = millis();
    supplyCheckedAt = phaseStart;
    if (result.overlap) {
      WiFi.mode(WIFI_STA);
      WiFi.scanNetworks(/*async=*/true);
    }
    LOG_INF("TEST", "P3: writing %lu MB%s", static_cast<unsigned long>(result.sizeMb),
            result.overlap ? " with Wi-Fi scans and refreshes" : "");
    redraw();
  }

  void writeChunk() {
    const size_t n = static_cast<size_t>(totalRun - doneBytes < CHUNK ? totalRun - doneBytes : CHUNK);
    fill(buffer.get(), n, stream);
    sha.update(buffer.get(), n);
    if (file.write(buffer.get(), n) != n) {
      ++result.ioErrors;
      endRun("write");
      return;
    }
    doneBytes += n;
    if (doneBytes < totalRun) return;
    if (!file.close()) {
      ++result.ioErrors;
      endRun("close_write");
      return;
    }
    result.writeMs = millis() - phaseStart;
    sha.finish(writeDigest);
    file = Storage.open(TEST_FILE, O_RDONLY);
    if (!file) {
      ++result.ioErrors;
      endRun("open_read");
      return;
    }
    doneBytes = 0;
    stream = seed;
    sha.restart();
    phase = Phase::Reading;
    phaseStart = millis();
    LOG_INF("TEST", "P3: wrote %lu MB in %lu ms; reading back", static_cast<unsigned long>(result.sizeMb),
            result.writeMs);
  }

  void readChunk() {
    const size_t n = static_cast<size_t>(totalRun - doneBytes < CHUNK ? totalRun - doneBytes : CHUNK);
    const int got = file.read(buffer.get(), n);
    if (got != static_cast<int>(n)) {
      ++result.ioErrors;
      endRun("read");
      return;
    }
    sha.update(buffer.get(), n);
    fill(expected.get(), n, stream);
    if (result.mismatchAt < 0 && memcmp(buffer.get(), expected.get(), n) != 0) {
      for (size_t i = 0; i < n; ++i) {
        if (buffer[i] != expected[i]) {
          result.mismatchAt = static_cast<long>(doneBytes + i);
          break;
        }
      }
    }
    doneBytes += n;
    if (doneBytes < totalRun) return;
    result.readMs = millis() - phaseStart;
    uint8_t readDigest[Sha256::SIZE];
    sha.finish(readDigest);
    result.shaMatch = memcmp(writeDigest, readDigest, Sha256::SIZE) == 0;
    Sha256::toHex(writeDigest, result.shaHex);
    endRun(nullptr);
  }

  void overlapWork() {
    const int16_t scan = WiFi.scanComplete();
    if (scan == WIFI_SCAN_RUNNING) return;
    WiFi.scanDelete();
    ++result.overlapScans;
    WiFi.scanNetworks(/*async=*/true);
  }

  // Closes and removes the scratch file, the radio off first; records unless stopped.
  void endRun(const char* failure) {
    phase = Phase::Idle;
    if (result.overlap && WiFi.getMode() != WIFI_MODE_NULL) {
      WiFi.scanDelete();
      WiFi.disconnect(true);
      WiFi.mode(WIFI_OFF);
    }
    if (file) file.close();
    result.removed = Storage.remove(TEST_FILE);
    result.done = true;
    readCardInfo();
    char fails[96] = "";
    if (failure != nullptr) addFailure(fails, sizeof(fails), failure);
    if (failure == nullptr && !result.shaMatch) addFailure(fails, sizeof(fails), "sha");
    if (result.mismatchAt >= 0) addFailure(fails, sizeof(fails), "data");
    if (!result.removed) addFailure(fails, sizeof(fails), "remove");
    if (!widthOk()) addFailure(fails, sizeof(fails), "bus_width");
    if (!clockOk()) addFailure(fails, sizeof(fails), "clock");
    record(fails[0] == '\0' ? Verdict::Pass : Verdict::Fail,
           "size_mb=%lu overlap=%d scans=%d write_mbps=%.2f read_mbps=%.2f sha_match=%d mismatch_at=%ld io_errors=%d "
           "removed=%d type=%s bus=%u clock_khz=%lu host_max_khz=%lu card_max_khz=%lu attempts=%u name=%s "
           "mid=0x%02x fails=%s",
           static_cast<unsigned long>(result.sizeMb), result.overlap ? 1 : 0, result.overlapScans,
           mbPerSecond(static_cast<uint64_t>(result.sizeMb) << 20, result.writeMs),
           mbPerSecond(static_cast<uint64_t>(result.sizeMb) << 20, result.readMs), result.shaMatch ? 1 : 0,
           result.mismatchAt, result.ioErrors, result.removed ? 1 : 0, cardTypeName(card.type), card.busWidth,
           static_cast<unsigned long>(card.clockKhz), static_cast<unsigned long>(card.hostMaxKhz),
           static_cast<unsigned long>(card.cardMaxKhz), card.mountAttempts, card.name, card.manufacturerId,
           fails[0] == '\0' ? "none" : fails);
  }

  // Back or leaving: nothing is recorded, the file goes.
  void abortRun(const char* why) {
    phase = Phase::Idle;
    if (result.overlap && WiFi.getMode() != WIFI_MODE_NULL) {
      WiFi.scanDelete();
      WiFi.disconnect(true);
      WiFi.mode(WIFI_OFF);
    }
    if (file) file.close();
    const bool removed = Storage.remove(TEST_FILE);
    LOG_INF("TEST", "P3: run %s after %lu of %lu MB; scratch file %s", why, static_cast<unsigned long>(doneBytes >> 20),
            static_cast<unsigned long>(totalRun >> 20), removed ? "removed" : "left for the next boot");
  }

  // --- power cycles (BRING_UP section 9) ---------------------------------------

  void startCycles() {
    cycles = CycleResult{};
    LOG_INF("TEST", "P3: %d SD power cycles, %lu ms off each", POWER_CYCLES, CYCLE_OFF_MS);
    powerOff();
  }

  // The reader's sleep shutdown (HalStorage takes its own lock): cached sectors
  // written back, unmounted, the bus parked LOW and SD_VDD gated off. No file of
  // this screen is open here.
  void powerOff() {
    Storage.prepareForDeepSleep();
    phase = Phase::CycleOff;
    phaseStart = millis();
    redraw();
  }

  // The boot's parked mount: SD_VDD switches on at its start, then the card is
  // initialised and sector 0 read. HalStorage::begin() takes no lock of its own.
  bool powerOn() {
    SemaphoreHandle_t mutex = TestSuiteAccess::storageMutex();
    xSemaphoreTakeRecursive(mutex, portMAX_DELAY);
    const bool ok = Storage.begin();
    xSemaphoreGiveRecursive(mutex);
    readCardInfo();
    const uint8_t attempts = card.mountAttempts;
    if (cycles.done < POWER_CYCLES) {
      cycles.attempts[cycles.done] = ok ? static_cast<char>('0' + (attempts < 9 ? attempts : 9)) : 'x';
    }
    ++cycles.done;
    if (attempts > cycles.maxAttempts) cycles.maxAttempts = attempts;
    if (!ok) {
      ++cycles.failed;
      cycles.lastError = card.lastMountError;
    }
    LOG_INF("TEST", "P3: power cycle %d: %s after %u power-on(s)", cycles.done, ok ? "mounted" : "NOT mounted",
            attempts);
    return ok;
  }

  void cycleTick() {
    const unsigned long now = millis();
    if (phase == Phase::CycleOff) {
      if (now - phaseStart < CYCLE_OFF_MS) return;
      if (!powerOn()) {
        finishCycles(/*failed=*/true);
        return;
      }
      phase = Phase::CycleOn;
      phaseStart = now;
      redraw();
      return;
    }
    if (now - phaseStart < CYCLE_GAP_MS) return;
    if (cycles.done >= POWER_CYCLES) {
      finishCycles(/*failed=*/false);
      return;
    }
    powerOff();
  }

  // Back or leaving: the card is mounted again if it is off.
  void stopCycles(const char* why, const bool recordIt) {
    if (phase == Phase::CycleOff) powerOn();
    phase = Phase::Idle;
    cycles.stop = why;
    LOG_INF("TEST", "P3: power cycles stopped (%s) after %d", why, cycles.done);
    if (recordIt) recordCycles(-1);
  }

  void finishCycles(const bool failed) {
    phase = Phase::Idle;
    if (failed) {
      cycles.stop = "remount_failed";
      recordCycles(-1);
      return;
    }
    cycles.stop = "done";
    if (!ask(QUESTION_SCOPE, SCOPE_HEADING, SCOPE_BODY, SCOPE_ANSWERS, 3)) recordCycles(-1);
  }

  // answer: the operator's (SCOPE_ANSWERS), or -1 when not asked.
  void recordCycles(const int answer) {
    char fails[48] = "";
    if (cycles.failed != 0) addFailure(fails, sizeof(fails), "remount");
    if (answer == 1) addFailure(fails, sizeof(fails), "3v3_dip");
    const bool complete = strcmp(cycles.stop, "done") == 0;
    const Verdict verdict = fails[0] != '\0' ? Verdict::Fail : complete && answer == 0 ? Verdict::Pass : Verdict::Info;
    record(verdict,
           "action=power_cycle cycles=%d/%d attempts=%s max_attempts=%u failed=%d last_err=0x%lx mounted=%d "
           "off_ms=%lu scope=%s stop=%s fails=%s",
           cycles.done, POWER_CYCLES, cycles.attempts[0] != '\0' ? cycles.attempts : "-", cycles.maxAttempts,
           cycles.failed, static_cast<unsigned long>(cycles.lastError), card.mounted ? 1 : 0, CYCLE_OFF_MS,
           answer >= 0 ? SCOPE_CODES[answer] : "none", cycles.stop, fails[0] != '\0' ? fails : "none");
  }

  // --- screen ----------------------------------------------------------------

  void drawCycles(TestPage& page) const {
    page.paragraph(CYCLE_NOTE, 4);
    page.rowf("Power cycles", "%d of %d%s", cycles.done, POWER_CYCLES,
              phase == Phase::CycleOff  ? ": SD_VDD off"
              : phase == Phase::CycleOn ? ": mounted"
                                        : "");
    if (cycles.attempts[0] != '\0') {
      page.rowf("Power-ons per mount / failed", "%s / %d", cycles.attempts, cycles.failed);
    }
  }

  void drawBody(TestPage& page) const override {
    page.paragraph(INTRO, 5);
    page.gap(4);
    page.setCompact(true);
    if (!card.mounted) {
      page.row("Card", "NOT MOUNTED");
      page.rowf("Power cycles / last error", "%u / 0x%lx", card.mountAttempts,
                static_cast<unsigned long>(card.lastMountError));
      if (cycles.done != 0) drawCycles(page);
      page.setCompact(false);
      return;
    }
    page.rowf("Card", "%s %lu MB, \"%s\"", cardTypeName(card.type),
              static_cast<unsigned long>(card.capacityBytes >> 20), card.name);
    page.rowf("CID MID / OID / rev / date", "0x%02x / 0x%04x / 0x%02x / %u-%02u", card.manufacturerId, card.oemId,
              card.revision, 2000U + (card.date >> 4), card.date & 0x0FU);
    page.rowf("Bus width", "%u bit%s", card.busWidth, widthOk() ? "" : " (profile: other)");
    page.rowf("Clock / host cap / card max", "%lu / %lu / %lu kHz%s", static_cast<unsigned long>(card.clockKhz),
              static_cast<unsigned long>(card.hostMaxKhz), static_cast<unsigned long>(card.cardMaxKhz),
              clockOk() ? "" : " (off)");
    page.rowf("Power cycles at mount", "%u%s", card.mountAttempts, card.lastMountError != 0 ? " (retried)" : "");
    page.rowf("Volume used / total", "%lu / %lu MB", static_cast<unsigned long>(usedBytes >> 20),
              static_cast<unsigned long>(totalBytes >> 20));
    if (powerCycleMode() || cycling()) {
      drawCycles(page);
      page.setCompact(false);
      return;
    }
    page.rowf("Run size / overlap", "%lu MB / %s", static_cast<unsigned long>(SIZES_MB[modeIndex]),
              overlap ? "Wi-Fi scans + refreshes" : "off");
    if (running()) {
      const unsigned pct = totalRun == 0 ? 0 : static_cast<unsigned>(doneBytes * 100 / totalRun);
      page.rowf(phase == Phase::Writing ? "Writing" : "Reading back", "%u %% of %lu MB", pct,
                static_cast<unsigned long>(totalRun >> 20));
    } else if (result.done) {
      const uint64_t bytes = static_cast<uint64_t>(result.sizeMb) << 20;
      page.rowf("Write / read", "%.2f / %.2f MB/s", mbPerSecond(bytes, result.writeMs),
                mbPerSecond(bytes, result.readMs));
      page.rowf("SHA-256 / compare", "%s / %s", result.shaMatch ? "match" : "MISMATCH",
                result.mismatchAt < 0 ? "equal" : "DIFFERS");
      page.rowf("I/O errors / file removed", "%d / %s", result.ioErrors, result.removed ? "yes" : "NO");
    }
    page.setCompact(false);
  }

  CardInfo card;
  uint64_t totalBytes = 0;
  uint64_t usedBytes = 0;
  int modeIndex = 0;  // a verify size (SIZES_MB), or MODE_POWER_CYCLE
  bool overlap = false;
  Phase phase = Phase::Idle;
  Result result;
  HalFile file;
  std::unique_ptr<uint8_t[]> buffer;
  std::unique_ptr<uint8_t[]> expected;
  uint64_t totalRun = 0;
  uint64_t doneBytes = 0;
  uint32_t seed = 0;
  uint32_t stream = 0;
  Sha256 sha;
  uint8_t writeDigest[Sha256::SIZE] = {};
  unsigned long phaseStart = 0;
  unsigned long drawnAt = 0;
  unsigned long supplyCheckedAt = 0;
  // The power cycles' result.
  struct CycleResult {
    int done = 0;  // mounts tried
    int failed = 0;
    uint8_t maxAttempts = 0;  // the most power-ons one mount needed
    int32_t lastError = 0;
    char attempts[POWER_CYCLES + 1] = {};  // power-ons per mount, one digit each; 'x' failed
    const char* stop = "done";
  };
  CycleResult cycles;
};

}  // namespace

std::unique_ptr<Activity> createSdCardTest(const TestInfo& info, GfxRenderer& renderer,
                                           MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<SdCardTest>(info, renderer, mappedInput);
}

}  // namespace testsuite
#endif

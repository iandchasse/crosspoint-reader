#if CROSSPOINT_TEST_SUITE
// U1 and U2: bench loads for a meter, a scope and a thermocouple, each bounded,
// with the radio off (WIFI_MODE_NULL) and the scratch file removed on every way
// out.
//
// U1, Wi-Fi TX bursts (BRING_UP section 3.3, the brown-out sweep): one second of
// TX, one second quiet, for 1, 2 or 5 minutes, as back-to-back active scans (no
// network needed) or UDP datagrams to the gateway of a network joined through the
// reader's Wi-Fi screen. The screen repaints only in a quiet second, so TX never
// overlaps a refresh (PWR-02). On USB it always runs; with no USB power the
// operator names what is on J5: a bench supply may be swept down through the
// brown-out region, a cell keeps the 3.4 V Wi-Fi gate (bat-wifi-gate). A
// brown-out resets the board: P1 then shows reset=BROWNOUT, and this screen shows
// the boot's reset reason.
//
// U2, sustained load (BRING_UP section 9, the LDO's temperature): continuous TX,
// a scratch file in /.silkscreen-test/ written and read back, and full-screen
// FAST refreshes back to back, for 5, 10 or 20 minutes. USB power only, checked
// again every second.
#include <Arduino.h>
#include <BoardConfig.h>
#include <FS.h>  // before SdFat (SDCardManager.h), as HalStorage.cpp includes them
#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalDisplay.h>
#include <HalFrontlight.h>
#include <HalGPIO.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <SDCardManager.h>
#include <WiFi.h>
#include <esp_random.h>
#include <esp_system.h>
#include <lwip/sockets.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <utility>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "PanelPatterns.h"
#include "RtcAccess.h"
#include "Supply.h"
#include "TestFactories.h"
#include "TestNames.h"
#include "TextBuffer.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/testsuite/TestCatalog.h"
#include "activities/testsuite/TestResultLog.h"
#include "activities/testsuite/TestScreen.h"
#include "activities/testsuite/TestSuiteAccess.h"
#include "fontIds.h"

namespace testsuite {
namespace {

enum class TxMode : uint8_t { Scan, Udp };

const char* txModeName(const TxMode mode) { return mode == TxMode::Scan ? "scan" : "udp"; }

// U1: one second of TX, one quiet; the screen repaints in a quiet second.
constexpr unsigned long BURST_ON_MS = 1000;
constexpr unsigned long BURST_OFF_MS = 1000;
constexpr unsigned long BURST_DURATIONS_S[] = {60, 120, 300};
constexpr unsigned long BURST_REDRAW_MS = 10000;
// U2
constexpr unsigned long LOAD_DURATIONS_S[] = {300, 600, 1200};
constexpr int DURATION_COUNT = 3;
static_assert(sizeof(BURST_DURATIONS_S) / sizeof(BURST_DURATIONS_S[0]) == DURATION_COUNT &&
                  sizeof(LOAD_DURATIONS_S) / sizeof(LOAD_DURATIONS_S[0]) == DURATION_COUNT,
              "three durations each");

constexpr unsigned long SUPPLY_CHECK_MS = 1000;
// Active scans with a short dwell: probe requests on every channel, back to back.
constexpr uint32_t SCAN_DWELL_MS = 40;
// UDP to the gateway's discard port, a few datagrams per loop pass.
constexpr uint16_t UDP_PORT = 9;
constexpr size_t UDP_PAYLOAD = 1024;
constexpr int UDP_PER_PASS = 8;

// U2's card load: a scratch file written and read back in full, over and over.
constexpr char LOAD_FILE[] = "/.silkscreen-test/load.tmp";
constexpr uint32_t LOAD_FILE_BYTES = 8UL * 1024UL * 1024UL;
constexpr size_t LOAD_CHUNK = 16 * 1024;
static_assert(LOAD_CHUNK % 4 == 0 && LOAD_FILE_BYTES % LOAD_CHUNK == 0, "whole chunks of 4-byte words");
constexpr unsigned long SD_SLICE_MS = 20;  // card work per loop pass, so the radio keeps sending
constexpr uint64_t SD_FREE_MARGIN_BYTES = 16ULL * 1024ULL * 1024ULL;

constexpr int QUESTION_SUPPLY = 1;
constexpr const char* SUPPLY_ANSWERS[] = {"Cancel", "A bench supply (brown-out sweep)", "A cell"};
constexpr char SUPPLY_HEADING[] = "What is on J5?";
constexpr char SUPPLY_BODY[] =
    "No USB power. A bench supply in place of the cell may be swept down through the brown-out region (BRING_UP "
    "3.3); a cell keeps the 3.4 V Wi-Fi gate.";

constexpr char BURST_INTRO[] =
    "Confirm: 1 s of Wi-Fi TX, 1 s quiet, for the time set (Left). Right: scans, or UDP to a network joined on the "
    "Wi-Fi screen. Scope 3V3 at U3.5 for dips below 3.0 V while the supply on J5 is swept down. Back stops.";
constexpr char BURST_NOTE[] =
    "A brown-out resets the board: open P1 (reset=BROWNOUT). Three in a row and it sleeps until POWER. Below about "
    "3.3 V on P+ with the radio on, the reader's cutoff sleeps it first (a cutoff, not a brown-out).";
constexpr char LOAD_INTRO[] =
    "Confirm, on USB only: Wi-Fi TX, a scratch file written and read back, and full-screen refreshes, back to back "
    "for the time set (Left). Right: scans, or UDP to a joined network. Thermocouple on U3, board in its case.";

// xorshift32: the same stream for the write and the compare.
void fill(uint8_t* buf, const size_t n, uint32_t& state) {
  for (size_t i = 0; i < n; i += 4) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    memcpy(buf + i, &state, 4);
  }
}

class LoadTest final : public TestScreen {
 public:
  // sustained: U2; otherwise U1.
  LoadTest(const TestInfo& info, GfxRenderer& renderer, MappedInputManager& mappedInput, const bool sustained)
      : TestScreen(info, renderer, mappedInput), sustained(sustained) {}

 private:
  enum class Phase : uint8_t { Idle, Connecting, Running };

  struct Run {
    TxMode mode = TxMode::Scan;
    const char* supply = "usb";  // usb, bench or cell
    unsigned long startedAt = 0;
    unsigned long durationMs = 0;
    unsigned long endedAt = 0;
    uint32_t bursts = 0;
    uint32_t scans = 0;
    uint32_t packets = 0;
    uint32_t sendErrors = 0;
    uint16_t pPlusStartMv = 0;
    uint16_t pPlusMinMv = 0;
    uint16_t pPlusLastMv = 0;
    char ssid[33] = "";
    int32_t rssi = 0;
    int txPowerQdbm = 0;  // in 0.25 dBm steps
    // U2
    uint32_t frames = 0;
    const char* sd = "none";  // none (no card), no_space, oom, on, failed
    uint64_t sdWritten = 0;
    uint64_t sdRead = 0;
    uint32_t sdErrors = 0;
    uint32_t sdMismatches = 0;
    bool sdRemoved = true;
    bool modeNull = false;
    const char* stop = "done";
    bool recorded = false;
  };

  unsigned long durationS() const {
    return sustained ? LOAD_DURATIONS_S[durationIndex] : BURST_DURATIONS_S[durationIndex];
  }
  bool running() const { return phase == Phase::Running; }
  // Bounded runs (5 and 20 min at most) with no input to keep the board awake.
  bool preventAutoSleep() override { return running(); }
  bool skipLoopDelay() override { return running() && (sustained || run.mode == TxMode::Udp); }

  void onStart() override { bootReset = esp_reset_reason(); }

  void onExit() override {
    if (running()) endRun("exit", /*recordIt=*/false);
    // The ActivityManager holds the RenderLock here. Whatever comes next paints
    // over a load frame: its first refresh is a clean one.
    if (glassDirty.load()) renderer.promoteNextRefresh(HalDisplay::HALF_REFRESH);
    TestScreen::onExit();  // the radio off, the light off
  }

  bool onBack() override {
    if (!running()) return false;
    endRun("back", /*recordIt=*/true);
    return true;
  }

  const char* slotLabel(const Slot slot) const override {
    if (phase != Phase::Idle) return nullptr;
    switch (slot) {
      case Slot::Confirm:
        return "Run";
      case Slot::Left:
        return "Time";
      case Slot::Right:
        return mode == TxMode::Scan ? "TX: scans" : "TX: UDP";
      default:
        return nullptr;
    }
  }

  void onSlot(const Slot slot) override {
    if (phase != Phase::Idle) return;
    switch (slot) {
      case Slot::Confirm:
        requestRun();
        return;
      case Slot::Left:
        durationIndex = (durationIndex + 1) % DURATION_COUNT;
        requestUpdate();
        return;
      case Slot::Right:
        mode = mode == TxMode::Scan ? TxMode::Udp : TxMode::Scan;
        requestUpdate();
        return;
      default:
        return;
    }
  }

  void onAnswer(const int question, const int answer) override {
    if (question != QUESTION_SUPPLY) return;
    if (answer == 1) {
      prepare("bench");
    } else if (answer == 2) {
      // bat-wifi-gate: a cell needs P+ at the cutoff before the radio comes up.
      const Supply supply = readSupply();
      const uint16_t gate = BoardConfig::powerPolicy().lowBatteryCutoffMv;
      if (gate != 0 && supply.pPlusMv < gate) {
        record(Verdict::Skip, "mode=%s supply=cell reason=battery_gate pplus=%u gate=%u", txModeName(mode),
               supply.pPlusMv, gate);
        return;
      }
      prepare("cell");
    }
  }

  // --- start -----------------------------------------------------------------

  void requestRun() {
    if (Frontlight.isOn()) Frontlight.setOn(false);  // no lit boost through a radio session
    const Supply supply = readSupply();
    if (supply.external) {
      prepare("usb");
      return;
    }
    if (sustained) {
      // U2 loads the LDO to its limit: USB power only (PWR-02, the USB_STAT ladder).
      record(Verdict::Skip, "mode=%s reason=on_battery pplus=%u (USB power only)", txModeName(mode), supply.pPlusMv);
      return;
    }
    ask(QUESTION_SUPPLY, SUPPLY_HEADING, SUPPLY_BODY, SUPPLY_ANSWERS, 3);
  }

  void prepare(const char* supplyKind) {
    run = Run{};
    run.mode = mode;
    run.supply = supplyKind;
    if (mode == TxMode::Scan) {
      beginRun();
      return;
    }
    // The Wi-Fi screen's first-connection hook syncs only a clock that has lost
    // its time (as T15): T14 then does not judge a span across that write.
    SETTINGS.clockHasBeenSynced = 1;
    if (!halClock.hasValidTime()) rtc::noteWritten();
    auto selection = makeUniqueNoThrow<WifiSelectionActivity>(renderer, mappedInput, /*autoConnect=*/true);
    if (!selection) {
      LOG_ERR("TEST", "OOM: Wi-Fi screen");
      record(Verdict::Fail, "mode=udp reason=oom");
      return;
    }
    phase = Phase::Connecting;
    startActivityForResult(std::move(selection), [this](const ActivityResult& result) {
      phase = Phase::Idle;
      if (result.isCancelled || WiFi.status() != WL_CONNECTED) {
        radioOff();
        record(Verdict::Skip, "mode=udp supply=%s reason=no_connection mode_null=%d", run.supply,
               WiFi.getMode() == WIFI_MODE_NULL ? 1 : 0);
        return;
      }
      snprintf(run.ssid, sizeof(run.ssid), "%s", WiFi.SSID().c_str());
      beginRun();
    });
  }

  void beginRun() {
    const Supply supply = readSupply();
    run.pPlusStartMv = supply.pPlusMv;
    run.pPlusMinMv = supply.pPlusMv;
    run.pPlusLastMv = supply.pPlusMv;
    if (run.mode == TxMode::Scan) {
      WiFi.mode(WIFI_STA);
    } else if (!openSocket()) {
      radioOff();
      record(Verdict::Fail, "mode=udp supply=%s reason=socket", run.supply);
      return;
    }
    run.rssi = run.mode == TxMode::Udp ? WiFi.RSSI() : 0;
    run.txPowerQdbm = static_cast<int>(WiFi.getTxPower());
    run.durationMs = durationS() * 1000UL;
    if (sustained) {
      beginCardLoad();
      closeRouting();  // the footer is not on the glass while the frames are
      framesDone.store(0);
      framesRequested = 1;  // the requestUpdate() below draws the first
      framesOn.store(true);
    }
    run.startedAt = millis();
    supplyCheckedAt = run.startedAt;
    inputArmed = false;
    burstOn = true;
    burstAt = run.startedAt;
    drawnAt = run.startedAt;
    phase = Phase::Running;
    LOG_INF("TEST", "%s: %s load for %lu s (%s), supply %s, P+ %u mV", info.id, sustained ? "sustained" : "TX burst",
            durationS(), txModeName(run.mode), run.supply, run.pPlusStartMv);
    requestUpdate();
  }

  // --- the run ---------------------------------------------------------------

  void onTick() override {
    if (!running()) return;
    const unsigned long now = millis();
    if (sustained) {
      // From the pass after the start: the key release that started the run is
      // still this pass's input.
      int x = 0;
      int y = 0;
      if (inputArmed && (gpio.wasAnyReleased() || mappedInput.wasScreenTapped(x, y))) {
        endRun("input", /*recordIt=*/true);
        return;
      }
      inputArmed = true;
    }
    if (now - supplyCheckedAt >= SUPPLY_CHECK_MS && !supplyStillOk()) return;
    if (now - run.startedAt >= run.durationMs) {
      endRun("done", /*recordIt=*/true);
      return;
    }
    if (sustained) {
      txTick(true);
      cardTick();
      // The next frame once the last one asked for is on the glass.
      const uint32_t done = framesDone.load();
      if (done >= framesRequested) {
        framesRequested = done + 1;
        requestUpdate();
      }
      return;
    }
    burstTick(now);
  }

  // U1: TX for BURST_ON_MS, quiet for BURST_OFF_MS; a repaint only when quiet.
  void burstTick(const unsigned long now) {
    if (burstOn) {
      if (now - burstAt >= BURST_ON_MS) {
        burstOn = false;
        burstAt = now;
        ++run.bursts;
      }
    } else if (now - burstAt >= BURST_OFF_MS) {
      burstOn = true;
      burstAt = millis();
    } else if (now - drawnAt >= BURST_REDRAW_MS && !scanRunning) {
      // Blocks for the refresh, which ends before the next burst starts.
      drawnAt = now;
      requestUpdateAndWait();
      burstAt = millis() - BURST_OFF_MS;  // the burst starts once the panel is done
    }
    txTick(burstOn);
  }

  // The supply, once a second. U2 and a USB run stop when USB power goes; a cell
  // stops at the cutoff less the radio allowance (the reader's own sleep level).
  bool supplyStillOk() {
    supplyCheckedAt = millis();
    const Supply supply = readSupply();
    run.pPlusLastMv = supply.pPlusMv;
    if (supply.pPlusMv < run.pPlusMinMv) run.pPlusMinMv = supply.pPlusMv;
    const char* why = nullptr;
    if ((sustained || strcmp(run.supply, "usb") == 0) && !supply.external) {
      why = "usb_lost";
    } else if (strcmp(run.supply, "cell") == 0) {
      const auto& policy = BoardConfig::powerPolicy();
      const uint16_t gate = policy.lowBatteryCutoffMv > policy.cutoffRadioAllowanceMv
                                ? policy.lowBatteryCutoffMv - policy.cutoffRadioAllowanceMv
                                : 0;
      if (supply.onBattery() && supply.pPlusMv < gate) why = "cell_gate";
    }
    if (why == nullptr) return true;
    LOG_INF("TEST", "%s: %s at P+ %u mV; stopping", info.id, why, supply.pPlusMv);
    endRun(why, /*recordIt=*/true);
    return false;
  }

  // --- radio -----------------------------------------------------------------

  void txTick(const bool transmit) {
    if (run.mode == TxMode::Scan) {
      scanTick(transmit);
    } else if (transmit) {
      udpTick();
    }
  }

  void scanTick(const bool transmit) {
    if (scanRunning) {
      const int16_t state = WiFi.scanComplete();
      if (state == WIFI_SCAN_RUNNING) return;
      if (state >= 0) ++run.scans;  // the networks found: a scan that ran
      WiFi.scanDelete();
      scanRunning = false;
    }
    if (!transmit) return;
    scanRunning =
        WiFi.scanNetworks(/*async=*/true, /*show_hidden=*/false, /*passive=*/false, SCAN_DWELL_MS) == WIFI_SCAN_RUNNING;
  }

  bool openSocket() {
    sock = lwip_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) return false;
    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_port = htons(UDP_PORT);
    dest.sin_addr.s_addr = static_cast<uint32_t>(WiFi.gatewayIP());
    return true;
  }

  void udpTick() {
    for (int i = 0; i < UDP_PER_PASS; ++i) {
      const int sent =
          lwip_sendto(sock, payload, sizeof(payload), MSG_DONTWAIT, reinterpret_cast<sockaddr*>(&dest), sizeof(dest));
      if (sent < 0) {
        ++run.sendErrors;  // the Wi-Fi queue is full: try again next pass
        return;
      }
      ++run.packets;
    }
  }

  void radioOff() {
    if (scanRunning) {
      WiFi.scanDelete();
      scanRunning = false;
    }
    if (sock >= 0) {
      lwip_close(sock);
      sock = -1;
    }
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    run.modeNull = WiFi.getMode() == WIFI_MODE_NULL;
  }

  // --- U2's card load ------------------------------------------------------------

  void beginCardLoad() {
    if (!Storage.ready()) return;  // run.sd stays "none"
    uint64_t totalBytes = 0;
    uint64_t usedBytes = 0;
    {
      // The SD manager has no lock of its own: HalStorage's, as its methods take it.
      SemaphoreHandle_t mutex = TestSuiteAccess::storageMutex();
      xSemaphoreTakeRecursive(mutex, portMAX_DELAY);
      auto& sd = SDCardManager::getInstance();
      totalBytes = sd.sdTotalBytes();
      usedBytes = sd.sdUsedBytes();
      xSemaphoreGiveRecursive(mutex);
    }
    if (totalBytes != 0 && totalBytes - usedBytes < LOAD_FILE_BYTES + SD_FREE_MARGIN_BYTES) {
      run.sd = "no_space";
      return;
    }
    cardBuf = makeUniqueNoThrow<uint8_t[]>(LOAD_CHUNK);
    cardExpected = makeUniqueNoThrow<uint8_t[]>(LOAD_CHUNK);
    if (!cardBuf || !cardExpected) {
      LOG_ERR("TEST", "OOM: U2 card buffers");
      run.sd = "oom";
      return;
    }
    run.sd = "failed";
    run.sdRemoved = false;
    if (Storage.exists(RESULTS_DIR) || Storage.mkdir(RESULTS_DIR)) {
      if (Storage.exists(LOAD_FILE)) Storage.remove(LOAD_FILE);
      cardFile = Storage.open(LOAD_FILE, O_RDWR | O_CREAT | O_TRUNC);
    }
    if (!cardFile) {
      ++run.sdErrors;
      LOG_ERR("TEST", "U2: could not open %s; the card load is off", LOAD_FILE);
      return;
    }
    run.sd = "on";
    cardWriting = true;
    cardPos = 0;
    cardSeed = esp_random() | 1U;  // xorshift never leaves 0
    cardStream = cardSeed;
  }

  void cardTick() {
    if (strcmp(run.sd, "on") != 0) return;
    const unsigned long start = millis();
    while (millis() - start < SD_SLICE_MS) {
      if (cardWriting) {
        fill(cardBuf.get(), LOAD_CHUNK, cardStream);
        if (cardFile.write(cardBuf.get(), LOAD_CHUNK) != LOAD_CHUNK) {
          cardFailed("write");
          return;
        }
        run.sdWritten += LOAD_CHUNK;
      } else {
        if (cardFile.read(cardBuf.get(), LOAD_CHUNK) != static_cast<int>(LOAD_CHUNK)) {
          cardFailed("read");
          return;
        }
        fill(cardExpected.get(), LOAD_CHUNK, cardStream);
        if (memcmp(cardBuf.get(), cardExpected.get(), LOAD_CHUNK) != 0) ++run.sdMismatches;
        run.sdRead += LOAD_CHUNK;
      }
      cardPos += LOAD_CHUNK;
      if (cardPos < LOAD_FILE_BYTES) continue;
      // A whole pass: back to the start, to read what was written, or to write anew.
      if (cardWriting) cardFile.flush();
      if (!cardFile.seekSet(0)) {
        cardFailed("seek");
        return;
      }
      cardPos = 0;
      if (!cardWriting) cardSeed = esp_random() | 1U;
      cardWriting = !cardWriting;
      cardStream = cardSeed;
    }
  }

  void cardFailed(const char* what) {
    ++run.sdErrors;
    run.sd = "failed";
    LOG_ERR("TEST", "U2: card %s failed after %lu MB written, %lu MB read; card load off", what,
            static_cast<unsigned long>(run.sdWritten >> 20), static_cast<unsigned long>(run.sdRead >> 20));
  }

  void endCardLoad() {
    if (cardFile) cardFile.close();
    if (strcmp(run.sd, "on") == 0 || strcmp(run.sd, "failed") == 0) {
      run.sdRemoved = !Storage.exists(LOAD_FILE) || Storage.remove(LOAD_FILE);
    }
    cardBuf.reset();
    cardExpected.reset();
  }

  // --- the end -----------------------------------------------------------------

  // Every way out: the radio off first, the card file removed, the frames ended.
  void endRun(const char* why, const bool recordIt) {
    phase = Phase::Idle;
    run.stop = why;
    run.endedAt = millis();
    radioOff();
    if (sustained) {
      endCardLoad();
      run.frames = framesDone.load();
      if (framesOn.exchange(false)) {
        requestCleanRepaint();  // the next repaint clears the last frame
        glassDirty.store(true);
      }
    }
    LOG_INF("TEST", "%s: run ended (%s) after %lu s", info.id, why,
            static_cast<unsigned long>((run.endedAt - run.startedAt) / 1000UL));
    if (recordIt) {
      recordRun();
    } else {
      requestUpdate();
    }
  }

  void recordRun() {
    run.recorded = true;
    char fails[64] = "";
    if (!run.modeNull) addFailure(fails, sizeof(fails), "radio_still_on");
    if (sustained) {
      if (run.sdErrors != 0) addFailure(fails, sizeof(fails), "sd_io");
      if (run.sdMismatches != 0) addFailure(fails, sizeof(fails), "sd_data");
      if (!run.sdRemoved) addFailure(fails, sizeof(fails), "sd_remove");
    }
    const Verdict verdict = fails[0] != '\0' ? Verdict::Fail : Verdict::Info;
    const unsigned long ranS = (run.endedAt - run.startedAt) / 1000UL;
    char ssid[24] = "-";
    if (run.ssid[0] != '\0') snprintf(ssid, sizeof(ssid), "%.16s", run.ssid);
    if (!sustained) {
      record(verdict,
             "mode=%s supply=%s dur_s=%lu ran_s=%lu bursts=%lu scans=%lu packets=%lu send_err=%lu pplus_start=%u "
             "pplus_min=%u ssid=%s rssi=%ld tx_qdbm=%d boot_reset=%s stop=%s mode_null=%d fails=%s",
             txModeName(run.mode), run.supply, run.durationMs / 1000UL, ranS, static_cast<unsigned long>(run.bursts),
             static_cast<unsigned long>(run.scans), static_cast<unsigned long>(run.packets),
             static_cast<unsigned long>(run.sendErrors), run.pPlusStartMv, run.pPlusMinMv, ssid,
             static_cast<long>(run.rssi), run.txPowerQdbm, resetReasonName(bootReset), run.stop, run.modeNull ? 1 : 0,
             fails[0] != '\0' ? fails : "none");
      return;
    }
    record(verdict,
           "mode=%s dur_s=%lu ran_s=%lu frames=%lu sd=%s sd_w_mb=%lu sd_r_mb=%lu sd_err=%lu sd_mismatch=%lu "
           "removed=%d scans=%lu packets=%lu send_err=%lu pplus=%u ssid=%s stop=%s mode_null=%d fails=%s",
           txModeName(run.mode), run.durationMs / 1000UL, ranS, static_cast<unsigned long>(run.frames), run.sd,
           static_cast<unsigned long>(run.sdWritten >> 20), static_cast<unsigned long>(run.sdRead >> 20),
           static_cast<unsigned long>(run.sdErrors), static_cast<unsigned long>(run.sdMismatches),
           run.sdRemoved ? 1 : 0, static_cast<unsigned long>(run.scans), static_cast<unsigned long>(run.packets),
           static_cast<unsigned long>(run.sendErrors), run.pPlusLastMv, ssid, run.stop, run.modeNull ? 1 : 0,
           fails[0] != '\0' ? fails : "none");
  }

  // --- screen ----------------------------------------------------------------

  void render(RenderLock&& lock) override {
    if (!framesOn.load()) {
      TestScreen::render(std::move(lock));
      glassDirty.store(false);
      return;
    }
    // U2's frames: the T4 patterns, alternating, with the timer in a box.
    const uint32_t frame = framesDone.load();
    renderer.clearScreen();
    patterns::drawBwFrame(renderer, (frame & 1U) != 0);
    drawRunBox(frame);
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    framesDone.store(frame + 1);
  }

  void drawRunBox(const uint32_t frame) const {
    const unsigned long elapsedS = (millis() - run.startedAt) / 1000UL;
    const unsigned long totalS = run.durationMs / 1000UL;
    char line1[48];
    char line2[64];
    snprintf(line1, sizeof(line1), "%s  sustained load  %lu:%02lu / %lu:%02lu", info.id, elapsedS / 60, elapsedS % 60,
             totalS / 60, totalS % 60);
    snprintf(line2, sizeof(line2), "frame %lu  SD %lu/%lu MB  TX %lu", static_cast<unsigned long>(frame + 1),
             static_cast<unsigned long>(run.sdWritten >> 20), static_cast<unsigned long>(run.sdRead >> 20),
             static_cast<unsigned long>(run.mode == TxMode::Scan ? run.scans : run.packets));
    constexpr char LINE3[] = "Any key or tap: stop";
    const int width = renderer.getScreenWidth();
    const int line12 = renderer.getLineHeight(UI_12_FONT_ID);
    const int line10 = renderer.getLineHeight(UI_10_FONT_ID);
    int textWidth = renderer.getTextWidth(UI_12_FONT_ID, line1, EpdFontFamily::BOLD);
    const int w2 = renderer.getTextWidth(UI_10_FONT_ID, line2);
    if (w2 > textWidth) textWidth = w2;
    const int boxWidth = textWidth + 32 < width - 16 ? textWidth + 32 : width - 16;
    const int boxHeight = line12 + 2 * line10 + 20;
    const int x = (width - boxWidth) / 2;
    const int y = (renderer.getScreenHeight() - boxHeight) / 2;
    renderer.fillRect(x, y, boxWidth, boxHeight, false);
    renderer.drawRect(x, y, boxWidth, boxHeight, 2, true);
    int top = y + 10;
    renderer.drawCenteredText(UI_12_FONT_ID, top, line1, true, EpdFontFamily::BOLD);
    top += line12;
    renderer.drawCenteredText(UI_10_FONT_ID, top, line2);
    top += line10;
    renderer.drawCenteredText(UI_10_FONT_ID, top, LINE3);
  }

  void drawBody(TestPage& page) const override {
    page.paragraph(sustained ? LOAD_INTRO : BURST_INTRO, 5);
    if (!sustained) page.paragraph(BURST_NOTE, 4);
    page.gap(4);
    page.setCompact(true);
    const unsigned long totalS = durationS();
    page.rowf("Time / TX", "%lu min / %s", totalS / 60,
              mode == TxMode::Scan ? "active scans" : "UDP to the gateway (joins a network)");
    if (!sustained) {
      page.rowf("This boot's reset", "%s%s", resetReasonName(bootReset),
                bootReset == ESP_RST_BROWNOUT ? ": a BROWN-OUT (open P1)" : "");
    }
    if (running()) {
      const unsigned long elapsedS = (millis() - run.startedAt) / 1000UL;
      page.rowf("Elapsed", "%lu:%02lu of %lu:%02lu (%s)", elapsedS / 60, elapsedS % 60, totalS / 60, totalS % 60,
                run.supply);
    } else if (run.recorded) {
      page.rowf("Last run", "%lu s of %lu, %s", (run.endedAt - run.startedAt) / 1000UL, run.durationMs / 1000UL,
                run.stop);
    }
    if (running() || run.recorded) {
      page.rowf("Bursts / scans / packets", "%lu / %lu / %lu", static_cast<unsigned long>(run.bursts),
                static_cast<unsigned long>(run.scans), static_cast<unsigned long>(run.packets));
      page.rowf("P+ start / lowest / last", "%u / %u / %u mV", run.pPlusStartMv, run.pPlusMinMv, run.pPlusLastMv);
      if (run.ssid[0] != '\0') page.rowf("Network / RSSI", "%.16s / %ld dBm", run.ssid, static_cast<long>(run.rssi));
      if (sustained) {
        page.rowf("Frames / card w, r MB", "%lu / %lu, %lu (%s)", static_cast<unsigned long>(run.frames),
                  static_cast<unsigned long>(run.sdWritten >> 20), static_cast<unsigned long>(run.sdRead >> 20),
                  run.sd);
      }
      page.row("Wi-Fi", running() ? "on" : run.modeNull ? "off (NULL mode)" : "STILL ON");
    }
    page.setCompact(false);
  }

  const bool sustained;
  Phase phase = Phase::Idle;
  TxMode mode = TxMode::Scan;
  int durationIndex = 1;
  Run run;
  esp_reset_reason_t bootReset = ESP_RST_UNKNOWN;
  unsigned long supplyCheckedAt = 0;
  unsigned long drawnAt = 0;
  bool inputArmed = false;  // U2: any key or tap stops the run
  // U1 bursts
  bool burstOn = false;
  unsigned long burstAt = 0;
  // radio
  bool scanRunning = false;
  int sock = -1;
  sockaddr_in dest = {};
  uint8_t payload[UDP_PAYLOAD] = {};
  // U2 card load
  HalFile cardFile;
  std::unique_ptr<uint8_t[]> cardBuf;
  std::unique_ptr<uint8_t[]> cardExpected;
  bool cardWriting = true;
  uint32_t cardPos = 0;
  uint32_t cardSeed = 0;
  uint32_t cardStream = 0;
  // U2 frames: the render task draws one per request while framesOn.
  std::atomic<bool> framesOn{false};
  std::atomic<uint32_t> framesDone{0};
  uint32_t framesRequested = 0;
  std::atomic<bool> glassDirty{false};  // a load frame is on the glass
};

}  // namespace

std::unique_ptr<Activity> createTxBurstTest(const TestInfo& info, GfxRenderer& renderer,
                                            MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<LoadTest>(info, renderer, mappedInput, /*sustained=*/false);
}

std::unique_ptr<Activity> createSustainedLoadTest(const TestInfo& info, GfxRenderer& renderer,
                                                  MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<LoadTest>(info, renderer, mappedInput, /*sustained=*/true);
}

}  // namespace testsuite
#endif

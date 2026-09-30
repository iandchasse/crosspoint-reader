#if CROSSPOINT_TEST_SUITE
// T15: Wi-Fi, checked by an NTP sync (bring-up B7; the radio's load on P+ for
// B6). A scan, then a connection through the reader's own Wi-Fi screen: a saved
// network from /.crosspoint/wifi.json, read only (the store's saves are no-ops
// in this firmware), or one typed on the keyboard, kept in RAM. Then RSSI,
// HalClock::syncFromNTP() timed, the RTC before and after, and BAT_MONIT with
// the radio on and off. The radio goes off (WIFI_MODE_NULL) before the result
// is written to the card or drawn (PWR-02), and on every way out.
#include <Arduino.h>
#include <BoardConfig.h>
#include <HalClock.h>
#include <HalFrontlight.h>
#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>
#include <sys/time.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>

#include "CrossPointSettings.h"
#include "RtcAccess.h"
#include "Supply.h"
#include "TestFactories.h"
#include "TextBuffer.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/testsuite/TestCatalog.h"
#include "activities/testsuite/TestScreen.h"

namespace testsuite {
namespace {

constexpr int RSSI_MIN_DBM = -80;              // proposal
constexpr unsigned long SYNC_LIMIT_MS = 5500;  // syncFromNTP() waits up to 5 s
constexpr long RTC_VS_NTP_MS = 1000;
constexpr unsigned long SCAN_TIMEOUT_MS = 15000;
constexpr unsigned long RADIO_OFF_SETTLE_MS = 1000;
constexpr int BEST = 3;
// Unix time before this is not NTP time (2023-11-14).
constexpr time_t NTP_VALID_AFTER = 1700000000;

constexpr char INTRO[] =
    "Confirm runs: a scan, then the reader's Wi-Fi screen (a saved network connects by itself; a typed password "
    "stays in RAM), then an NTP sync. The radio goes off before the result. On battery it needs P+ at 3.4 V.";

// No connection after the Wi-Fi screen: a failed attempt and backing out both
// return "cancelled", so the operator says which.
constexpr int QUESTION_NO_CONNECTION = 1;
constexpr const char* NO_CONNECTION_ANSWERS[] = {"An attempt failed", "Backed out"};
constexpr char NO_CONNECTION_HEADING[] = "No connection";
constexpr char NO_CONNECTION_BODY[] = "Did a connection attempt fail, or did you back out without trying?";

// Register and bit of the RTC's time-lost flag: DS3231 status OSF, PCF85063 seconds OS.
bool readTimeLostFlag(bool& lost) {
  if (!rtc::present()) return false;
  const uint8_t addr = rtc::driver().address();
  const bool ds3231 = rtc::driver().type() == BoardConfig::RtcType::Ds3231;
  uint8_t value = 0;
  if (!rtc::readRegisters(addr, ds3231 ? 0x0F : 0x04, &value, 1)) return false;
  lost = (value & 0x80) != 0;
  return true;
}

// System (NTP) time in ms at the moment millis() read `atMs`.
int64_t systemMsAt(const unsigned long atMs) {
  timeval tv{};
  gettimeofday(&tv, nullptr);
  const unsigned long nowMs = millis();
  return static_cast<int64_t>(tv.tv_sec) * 1000 + tv.tv_usec / 1000 - static_cast<int64_t>(nowMs - atMs);
}

class WifiTest final : public TestScreen {
 public:
  WifiTest(const TestInfo& info, GfxRenderer& renderer, MappedInputManager& mappedInput)
      : TestScreen(info, renderer, mappedInput) {}

 private:
  enum class Phase : uint8_t { Idle, Scanning, Connecting, Syncing, Settling, Done };

  struct Network {
    char ssid[33];
    int32_t rssi;
  };

  bool busy() const { return phase == Phase::Scanning || phase == Phase::Syncing || phase == Phase::Settling; }
  bool skipLoopDelay() override { return busy(); }
  bool preventAutoSleep() override { return busy(); }

  void onExit() override {
    if (phase == Phase::Scanning) WiFi.scanDelete();
    TestScreen::onExit();  // the radio off
  }

  const char* slotLabel(const Slot slot) const override {
    return slot == Slot::Confirm && (phase == Phase::Idle || phase == Phase::Done) ? "Run" : nullptr;
  }

  void onSlot(const Slot slot) override {
    if (slot == Slot::Confirm && (phase == Phase::Idle || phase == Phase::Done)) start();
  }

  void onTick() override {
    switch (phase) {
      case Phase::Scanning:
        pollScan();
        break;
      case Phase::Syncing:
        runSync();
        break;
      case Phase::Settling:
        if (millis() - radioOffAt >= RADIO_OFF_SETTLE_MS) finish();
        break;
      default:
        break;
    }
  }

  void start() {
    resetRun();
    // No lit boost through a radio session (and no replay of one after a restart).
    if (Frontlight.isOn()) Frontlight.setOn(false);
    supplyBefore = readSupply();
    const auto& policy = BoardConfig::powerPolicy();
    if (policy.lowBatteryCutoffMv != 0 && supplyBefore.onBattery() &&
        supplyBefore.pPlusMv < policy.lowBatteryCutoffMv) {
      // bat-wifi-gate: no Wi-Fi TX below about 3.4 V on the cell.
      phase = Phase::Done;
      record(Verdict::Skip, "reason=battery_gate pplus=%u gate=%u", supplyBefore.pPlusMv, policy.lowBatteryCutoffMv);
      return;
    }
    validBefore = halClock.hasValidTime();
    if (validBefore) tickedBefore = rtc::readAtTick(beforeEpoch, beforeAtMs);
    WiFi.mode(WIFI_STA);
    WiFi.scanNetworks(/*async=*/true);
    scanStartedAt = millis();
    phase = Phase::Scanning;
    LOG_INF("TEST", "T15: scanning");
    requestUpdate();
  }

  void resetRun() {
    networkCount = 0;
    bestCount = 0;
    connected = false;
    ssid[0] = '\0';
    rssi = 0;
    channel = 0;
    synced = false;
    syncMs = 0;
    validBefore = false;
    tickedBefore = false;
    validAfter = false;
    lostFlagRead = false;
    lostFlagAfter = false;
    rtcVsNtpKnown = false;
    rtcVsNtpMs = 0;
    offsetBeforeKnown = false;
    offsetBeforeMs = 0;
    radioOnMv = 0;
    radioOffMv = 0;
    modeNull = false;
  }

  void pollScan() {
    const int16_t result = WiFi.scanComplete();
    if (result == WIFI_SCAN_RUNNING && millis() - scanStartedAt < SCAN_TIMEOUT_MS) return;
    networkCount = result > 0 ? result : 0;
    for (int i = 0; i < networkCount; ++i) {
      Network candidate{};
      snprintf(candidate.ssid, sizeof(candidate.ssid), "%s", WiFi.SSID(i).c_str());
      candidate.rssi = WiFi.RSSI(i);
      // Keep the strongest BEST, strongest first.
      int at = bestCount;
      while (at > 0 && best[at - 1].rssi < candidate.rssi) --at;
      if (at >= BEST) continue;
      const int last = bestCount < BEST ? bestCount : BEST - 1;
      for (int j = last; j > at; --j) best[j] = best[j - 1];
      best[at] = candidate;
      if (bestCount < BEST) ++bestCount;
    }
    WiFi.scanDelete();
    LOG_INF("TEST", "T15: scan found %d networks", networkCount);
    if (networkCount == 0) {
      radioOff();
      finish();
      return;
    }
    // The Wi-Fi screen's first-connection hook syncs only a lost clock (as the
    // reader does after its first sync), so this test's own sync is the one timed.
    SETTINGS.clockHasBeenSynced = 1;
    if (!validBefore) rtc::noteWritten();
    auto selection = makeUniqueNoThrow<WifiSelectionActivity>(renderer, mappedInput, /*autoConnect=*/true);
    if (!selection) {
      LOG_ERR("TEST", "OOM: Wi-Fi screen");
      radioOff();
      finish();
      return;
    }
    phase = Phase::Connecting;
    startActivityForResult(std::move(selection),
                           [this](const ActivityResult& result) { onConnection(!result.isCancelled); });
  }

  void onConnection(const bool ok) {
    connected = ok && WiFi.status() == WL_CONNECTED;
    if (!connected) {
      LOG_INF("TEST", "T15: not connected");
      radioOff();
      phase = Phase::Done;
      if (!ask(QUESTION_NO_CONNECTION, NO_CONNECTION_HEADING, NO_CONNECTION_BODY, NO_CONNECTION_ANSWERS, 2)) finish();
      return;
    }
    snprintf(ssid, sizeof(ssid), "%s", WiFi.SSID().c_str());
    phase = Phase::Syncing;  // synced on the next pass, once "syncing" is on the glass
  }

  // The radio is already off. Back on the question counts as backing out.
  void onAnswer(const int question, const int answer) override {
    if (question != QUESTION_NO_CONNECTION) return;
    if (answer == 0) {
      finish();
      return;
    }
    record(Verdict::Skip, "reason=cancelled networks=%d mode_null=%d", networkCount, modeNull ? 1 : 0);
  }

  void runSync() {
    // As ClockSyncActivity does: the screen is drawn before the blocking sync.
    requestUpdateAndWait();
    rssi = WiFi.RSSI();
    channel = WiFi.channel();
    radioOnMv = readSupply().pPlusMv;
    // With no RTC (optional, rtc-devices) HalClock has nothing to sync.
    const unsigned long start = millis();
    synced = rtc::present() && halClock.syncFromNTP();
    syncMs = millis() - start;
    if (synced) rtc::noteWritten();
    timeval tv{};
    gettimeofday(&tv, nullptr);
    const bool ntpTime = tv.tv_sec > NTP_VALID_AFTER;
    if (ntpTime && tickedBefore) {
      offsetBeforeMs = static_cast<long>(static_cast<int64_t>(beforeEpoch) * 1000 - systemMsAt(beforeAtMs));
      offsetBeforeKnown = true;
    }
    validAfter = halClock.hasValidTime();
    lostFlagRead = readTimeLostFlag(lostFlagAfter);
    uint32_t afterEpoch = 0;
    unsigned long afterAtMs = 0;
    if (ntpTime && rtc::readAtTick(afterEpoch, afterAtMs)) {
      rtcVsNtpMs = static_cast<long>(static_cast<int64_t>(afterEpoch) * 1000 - systemMsAt(afterAtMs));
      rtcVsNtpKnown = true;
    }
    LOG_INF("TEST", "T15: NTP sync %s in %lu ms", synced ? "ok" : "failed", syncMs);
    radioOff();
    phase = Phase::Settling;
    requestUpdate();
  }

  void radioOff() {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    modeNull = WiFi.getMode() == WIFI_MODE_NULL;
    radioOffAt = millis();
  }

  void finish() {
    phase = Phase::Done;
    const Supply after = readSupply();
    radioOffMv = after.pPlusMv;
    const auto& policy = BoardConfig::powerPolicy();
    // The sag is judged only on the cell: on USB, P+ is the charger's output.
    const bool sagJudged = connected && after.onBattery() && radioOnMv != 0;
    const int sag = radioOnMv != 0 ? static_cast<int>(radioOffMv) - static_cast<int>(radioOnMv) : 0;
    char fails[96] = "";
    if (networkCount == 0) addFailure(fails, sizeof(fails), "no_networks");
    if (networkCount > 0 && !connected) addFailure(fails, sizeof(fails), "no_connection");
    // No RTC fitted: the scan, the connection, RSSI and the radio are judged.
    const bool rtcFitted = rtc::present();
    if (connected) {
      if (rssi <= RSSI_MIN_DBM) addFailure(fails, sizeof(fails), "rssi");
      if (rtcFitted) {
        if (!synced || syncMs > SYNC_LIMIT_MS) addFailure(fails, sizeof(fails), "ntp");
        if (!validAfter) addFailure(fails, sizeof(fails), "rtc_not_valid");
        if (lostFlagRead && lostFlagAfter) addFailure(fails, sizeof(fails), "osf");
        if (!rtcVsNtpKnown || labs(rtcVsNtpMs) > RTC_VS_NTP_MS) addFailure(fails, sizeof(fails), "rtc_vs_ntp");
      }
      if (sagJudged && sag > static_cast<int>(policy.cutoffRadioAllowanceMv))
        addFailure(fails, sizeof(fails), "radio_sag");
    }
    if (!modeNull) addFailure(fails, sizeof(fails), "radio_still_on");
    char offset[16] = "n/a";
    if (offsetBeforeKnown) snprintf(offset, sizeof(offset), "%ld", offsetBeforeMs);
    char best0[32] = "none";
    if (bestCount > 0) snprintf(best0, sizeof(best0), "%.16s/%ld", best[0].ssid, static_cast<long>(best[0].rssi));
    char ntp[8] = "skipped";
    if (rtcFitted) snprintf(ntp, sizeof(ntp), "%d", synced ? 1 : 0);
    record(fails[0] == '\0' ? Verdict::Pass : Verdict::Fail,
           "networks=%d strongest=%s ssid=%.16s rssi=%ld ch=%d rtc=%s ntp=%s sync_ms=%lu valid_before=%d "
           "valid_after=%d lost_flag=%d rtc_minus_ntp_ms=%ld offset_before_ms=%s pplus_on=%u pplus_off=%u sag_mv=%d "
           "sag_judged=%d mode_null=%d fails=%s",
           networkCount, best0, connected ? ssid : "none", static_cast<long>(rssi), channel,
           rtcFitted ? "fitted" : "none", ntp, syncMs, validBefore ? 1 : 0, validAfter ? 1 : 0,
           lostFlagRead ? (lostFlagAfter ? 1 : 0) : -1, rtcVsNtpKnown ? rtcVsNtpMs : 0L, offset, radioOnMv, radioOffMv,
           sag, sagJudged ? 1 : 0, modeNull ? 1 : 0, fails[0] == '\0' ? "none" : fails);
  }

  void drawBody(TestPage& page) const override {
    page.paragraph(INTRO, 5);
    page.gap(4);
    page.setCompact(true);
    page.rowf("P+ before / supply", "%u mV / %s", supplyBefore.pPlusMv,
              supplyBefore.external ? "USB"
              : supplyBefore.cell   ? "battery"
                                    : "no cell");
    switch (phase) {
      case Phase::Idle:
        page.setCompact(false);
        return;
      case Phase::Scanning:
        page.row("Scan", "running");
        page.setCompact(false);
        return;
      case Phase::Connecting:
        page.row("Wi-Fi", "connecting");
        break;
      case Phase::Syncing:
        page.rowf("Connected", "%s", ssid);
        page.row("NTP", "syncing (up to 5 s)");
        page.setCompact(false);
        return;
      default:
        break;
    }
    page.rowf("Networks", "%d", networkCount);
    for (int i = 0; i < bestCount; ++i) page.rowf(best[i].ssid, "%ld dBm", static_cast<long>(best[i].rssi));
    if (phase == Phase::Settling) {
      page.row("Radio", "off; reading P+");
      page.setCompact(false);
      return;
    }
    if (!connected) {
      page.row("Connection", networkCount > 0 ? "none" : "no network to join");
    } else {
      page.rowf("Connected / RSSI / channel", "%s / %ld dBm / %d", ssid, static_cast<long>(rssi), channel);
      if (rtc::present()) {
        page.rowf("NTP sync", "%s in %lu ms", synced ? "ok" : "FAILED", syncMs);
      } else {
        page.row("NTP sync", "skipped: no RTC");
      }
      page.rowf("RTC valid before / after", "%s / %s", validBefore ? "yes" : "no", validAfter ? "yes" : "no");
      if (offsetBeforeKnown) page.rowf("RTC - NTP before the sync", "%+ld ms", offsetBeforeMs);
      if (rtcVsNtpKnown) page.rowf("RTC - NTP after", "%+ld ms", rtcVsNtpMs);
      page.rowf("P+ radio on / off", "%u / %u mV (%+d)", radioOnMv, radioOffMv,
                static_cast<int>(radioOffMv) - static_cast<int>(radioOnMv));
    }
    page.rowf("Wi-Fi now", "%s", modeNull ? "off (NULL mode)" : "STILL ON");
    page.setCompact(false);
  }

  Phase phase = Phase::Idle;
  Supply supplyBefore;
  unsigned long scanStartedAt = 0;
  unsigned long radioOffAt = 0;
  int networkCount = 0;
  int bestCount = 0;
  Network best[BEST] = {};
  bool connected = false;
  char ssid[33] = "";
  int32_t rssi = 0;
  int channel = 0;
  bool synced = false;
  unsigned long syncMs = 0;
  bool validBefore = false;
  bool tickedBefore = false;
  uint32_t beforeEpoch = 0;
  unsigned long beforeAtMs = 0;
  bool validAfter = false;
  bool lostFlagRead = false;
  bool lostFlagAfter = false;
  bool rtcVsNtpKnown = false;
  long rtcVsNtpMs = 0;
  bool offsetBeforeKnown = false;
  long offsetBeforeMs = 0;
  uint16_t radioOnMv = 0;
  uint16_t radioOffMv = 0;
  bool modeNull = false;
};

}  // namespace

std::unique_ptr<Activity> createWifiTest(const TestInfo& info, GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<WifiTest>(info, renderer, mappedInput);
}

}  // namespace testsuite
#endif

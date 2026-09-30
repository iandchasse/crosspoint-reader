#if CROSSPOINT_TEST_SUITE
// P4: USB presence and USB Drive (bring-up B4, B6). The board has no VBUS sense
// (HW-11): a data host shows as USB Serial/JTAG start-of-frame traffic
// (HWCDC::isPlugged()), and any USB power only through the USB_STAT ladder
// (HalGPIO::isUsbConnected()). Each bench state is set up and checked in turn;
// the last one hands the card to the host through the reader's own USB Drive
// screen, which ends in a restart (the question about it comes at the next
// opening of P4).
#include <Arduino.h>
#include <BatteryMonitor.h>
#include <BoardConfig.h>
#include <HalGPIO.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <esp_attr.h>
#include <esp_system.h>
#if ARDUINO_USB_MODE && ARDUINO_USB_CDC_ON_BOOT
#include <HWCDC.h>
#endif

#include <cstdio>

#include "TestFactories.h"
#include "TestNames.h"
#include "TextBuffer.h"
#include "activities/ActivityManager.h"
#include "activities/testsuite/TestCatalog.h"
#include "activities/testsuite/TestScreen.h"

// Set when P4 starts a USB Drive session; the session ends in a restart, which
// keeps RTC_NOINIT memory. Any other value (power-on garbage) means none.
RTC_NOINIT_ATTR uint32_t testUsbDriveSession;

namespace testsuite {
namespace {

using State = BoardConfig::ChargeState;

constexpr uint32_t USB_DRIVE_MAGIC = 0x55534244;  // "USBD"
constexpr unsigned long SAMPLE_MS = 250;
constexpr unsigned long REDRAW_MS = 5000;
constexpr unsigned long MIN_REDRAW_MS = 1000;
// Both signals must agree this soon after the first one changed. Complete needs
// 8 stable ladder samples (about 8.5-9.5 s), the no-battery blink about 8 s.
constexpr unsigned long AGREE_MS = 3000;
constexpr unsigned long AGREE_SLOW_MS = 10000;
constexpr uint8_t HISTORY = 4;

constexpr int QUESTION_D2 = 1;
constexpr int QUESTION_START_DRIVE = 2;
constexpr int QUESTION_DRIVE_RESULT = 3;
constexpr const char* D2_ANSWERS[] = {"D2 dark", "D2 lit"};
constexpr char D2_HEADING[] = "D2 (USB power LED)";
constexpr char D2_BODY[] = "Is D2 lit right now?";
constexpr const char* START_ANSWERS[] = {"Cancel", "Start USB Drive"};
constexpr char START_HEADING[] = "Start USB Drive?";
constexpr char START_BODY[] =
    "The host gets the raw card and can write anything on it, the reader's /.crosspoint included (P8 will show it). "
    "Eject on the host to end the session, or pull the cable on battery: either restarts the board.";
constexpr const char* DRIVE_ANSWERS[] = {"Mounted; eject ended it", "Cable pulled; ended within 5 s",
                                         "Host never mounted it", "Did not end as expected"};
constexpr char DRIVE_HEADING[] = "USB Drive session";
constexpr char DRIVE_BODY[] = "A USB Drive session ended with this restart. What happened?";

constexpr char INTRO[] =
    "Left/Right pick the bench state; set it up, wait for the readings to settle, then Confirm (it asks about D2). "
    "The last state starts USB Drive.";

struct Stage {
  const char* title;
  const char* setup;
  bool host;  // HWCDC::isPlugged()
  bool usb;   // HalGPIO::isUsbConnected()
};

constexpr Stage STAGES[] = {
    {"1 Data cable", "Plug a USB data cable into a computer.", true, true},
    {"2 Charger only", "Swap to a charger (or a power-only cable).", false, true},
    {"3 Unplugged", "Unplug USB; the board runs on the cell.", false, false},
    {"4 USB Drive", "On a data cable. Confirm starts USB Drive; the question follows the restart.", true, true},
};
constexpr int STAGE_COUNT = static_cast<int>(sizeof(STAGES) / sizeof(STAGES[0]));
constexpr int DRIVE_STAGE = STAGE_COUNT - 1;

bool hostPlugged() {
#if ARDUINO_USB_MODE && ARDUINO_USB_CDC_ON_BOOT
  return HWCDC::isPlugged();
#else
  return false;
#endif
}

bool cdcOpen() {
#if ARDUINO_USB_MODE && ARDUINO_USB_CDC_ON_BOOT
  return HWCDC::isConnected();
#else
  return false;
#endif
}

class UsbTest final : public TestScreen {
 public:
  UsbTest(const TestInfo& info, GfxRenderer& renderer, MappedInputManager& mappedInput)
      : TestScreen(info, renderer, mappedInput) {}

 private:
  enum class Signal : uint8_t { Host, Usb };
  struct Change {
    unsigned long atMs;
    Signal signal;
    bool value;
  };

  void onStart() override {
    sample();
    startStage();
    const esp_reset_reason_t reset = esp_reset_reason();
    drivePending = testUsbDriveSession == USB_DRIVE_MAGIC && reset != ESP_RST_POWERON && reset != ESP_RST_DEEPSLEEP;
    if (testUsbDriveSession == USB_DRIVE_MAGIC && !drivePending) testUsbDriveSession = 0;
    if (drivePending) ask(QUESTION_DRIVE_RESULT, DRIVE_HEADING, DRIVE_BODY, DRIVE_ANSWERS, 4);
  }

  void onTick() override {
    const unsigned long now = millis();
    if (now - sampledAt < SAMPLE_MS) return;
    const bool before[2] = {host, usb};
    sample();
    if (host != before[0]) noteChange(Signal::Host, host, now);
    if (usb != before[1]) noteChange(Signal::Usb, usb, now);
    if ((dirty && now - drawnAt >= MIN_REDRAW_MS) || now - drawnAt >= REDRAW_MS) redraw();
  }

  const char* slotLabel(const Slot slot) const override {
    switch (slot) {
      case Slot::Confirm:
        return stage == DRIVE_STAGE ? "Start" : "Record";
      case Slot::Left:
        return "Prev";
      case Slot::Right:
        return "Next";
      default:
        return nullptr;
    }
  }

  void onSlot(const Slot slot) override {
    if (slot == Slot::Left) {
      stage = (stage + STAGE_COUNT - 1) % STAGE_COUNT;
      startStage();
    } else if (slot == Slot::Right) {
      stage = (stage + 1) % STAGE_COUNT;
      startStage();
    } else if (slot == Slot::Confirm) {
      if (stage == DRIVE_STAGE) {
        // USB Drive needs a mounted card: without one its screen fails and
        // restarts, and the next opening would ask how a session ended.
        if (!Storage.ready()) {
          record(Verdict::Skip, "stage=usb_drive reason=no_card");
          return;
        }
        ask(QUESTION_START_DRIVE, START_HEADING, START_BODY, START_ANSWERS, 2);
      } else {
        ask(QUESTION_D2, D2_HEADING, D2_BODY, D2_ANSWERS, 2);
      }
    }
  }

  void onAnswer(const int question, const int answer) override {
    if (question == QUESTION_D2 && answer >= 0) judge(answer == 1);
    if (question == QUESTION_START_DRIVE && answer == 1) startDrive();
    if (question == QUESTION_DRIVE_RESULT) {
      drivePending = false;
      testUsbDriveSession = 0;
      if (answer < 0) return;
      constexpr const char* CODES[] = {"eject", "cable_pull", "never_mounted", "unexpected"};
      record(answer <= 1 ? Verdict::Pass : Verdict::Fail, "stage=usb_drive ended_by=%s reset=%s", CODES[answer],
             resetReasonName(esp_reset_reason()));
    }
  }

  void sample() {
    sampledAt = millis();
    host = hostPlugged();
    cdc = cdcOpen();
    usb = gpio.isUsbConnected();
    charge = BatteryMonitor::chargeStatus();
  }

  void startStage() {
    historyCount = 0;
    lastChangeAt[0] = 0;
    lastChangeAt[1] = 0;
    hostSeen = false;
    redraw();
  }

  void noteChange(const Signal signal, const bool value, const unsigned long now) {
    if (historyCount == HISTORY) {
      for (uint8_t i = 1; i < HISTORY; ++i) history[i - 1] = history[i];
      --historyCount;
    }
    history[historyCount++] = {now, signal, value};
    lastChangeAt[static_cast<int>(signal)] = now;
    if (signal == Signal::Host && value) hostSeen = true;
    LOG_INF("TEST", "P4: %s %s", signal == Signal::Host ? "USB host" : "USB power (USB_STAT)",
            value ? "present" : "gone");
    dirty = true;
  }

  // The time between the two signals' last changes during this stage, when both changed.
  bool agreement(unsigned long& ms) const {
    if (lastChangeAt[0] == 0 || lastChangeAt[1] == 0) return false;
    ms = lastChangeAt[0] > lastChangeAt[1] ? lastChangeAt[0] - lastChangeAt[1] : lastChangeAt[1] - lastChangeAt[0];
    return true;
  }

  void judge(const bool d2Lit) {
    const Stage& s = STAGES[stage];
    const bool slowLadder = charge == State::Complete || charge == State::NoBattery;
    const unsigned long limit = slowLadder ? AGREE_SLOW_MS : AGREE_MS;
    unsigned long apart = 0;
    const bool both = agreement(apart);
    char fails[64] = "";
    if (host != s.host) addFailure(fails, sizeof(fails), "host");
    if (usb != s.usb) addFailure(fails, sizeof(fails), "usb_stat");
    if (d2Lit != s.usb) addFailure(fails, sizeof(fails), "d2");
    // A charger has no data lines: a host must never show during that stage.
    if (!s.host && s.usb && hostSeen) addFailure(fails, sizeof(fails), "host_on_charger");
    if (s.host == s.usb && both && apart > limit) addFailure(fails, sizeof(fails), "slow");
    char apartText[16] = "n/a";
    if (both) snprintf(apartText, sizeof(apartText), "%lu", apart);
    record(fails[0] == '\0' ? Verdict::Pass : Verdict::Fail,
           "stage=%d host=%d cdc=%d usb_stat=%d charge=%s d2=%d apart_ms=%s limit_ms=%lu changes=%u fails=%s",
           stage + 1, host ? 1 : 0, cdc ? 1 : 0, usb ? 1 : 0, chargeStateName(charge), d2Lit ? 1 : 0, apartText, limit,
           historyCount, fails[0] == '\0' ? "none" : fails);
  }

  void startDrive() {
    record(Verdict::Info, "stage=usb_drive started=1 host=%d usb_stat=%d", host ? 1 : 0, usb ? 1 : 0);
    testUsbDriveSession = USB_DRIVE_MAGIC;
    LOG_INF("TEST", "P4: starting USB Drive; the session ends in a restart");
    activityManager.goToUsbDrive();
  }

  void redraw() {
    drawnAt = millis();
    dirty = false;
    requestUpdate();
  }

  void drawBody(TestPage& page) const override {
    const Stage& s = STAGES[stage];
    page.paragraph(INTRO, 3);
    page.gap(4);
    page.heading(s.title);
    page.paragraph(s.setup, 2);
    page.gap(4);
    page.setCompact(true);
    page.rowf("USB host (isPlugged) / CDC open", "%s / %s", host ? "yes" : "no", cdc ? "yes" : "no");
    page.rowf("USB power (USB_STAT) / ladder", "%s / %s", usb ? "yes" : "no", chargeStateName(charge));
    page.rowf("Expected host / power / D2", "%s / %s / %s", s.host ? "yes" : "no", s.usb ? "yes" : "no",
              s.usb ? "lit" : "dark");
    unsigned long apart = 0;
    if (agreement(apart)) page.rowf("Last changes apart", "%lu ms", apart);
    const unsigned long now = millis();
    for (uint8_t i = 0; i < historyCount; ++i) {
      const Change& c = history[historyCount - 1 - i];
      char label[40];
      snprintf(label, sizeof(label), "%s %s", c.signal == Signal::Host ? "Host" : "USB_STAT",
               c.value ? "present" : "gone");
      page.rowf(label, "%lu s ago", (now - c.atMs) / 1000UL);
    }
    page.setCompact(false);
  }

  int stage = 0;
  bool host = false;
  bool cdc = false;
  bool usb = false;
  State charge = State::Unknown;
  bool hostSeen = false;  // a host showed during this stage
  bool drivePending = false;
  bool dirty = false;
  unsigned long sampledAt = 0;
  unsigned long drawnAt = 0;
  unsigned long lastChangeAt[2] = {};
  Change history[HISTORY] = {};
  uint8_t historyCount = 0;
};

}  // namespace

std::unique_ptr<Activity> createUsbTest(const TestInfo& info, GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<UsbTest>(info, renderer, mappedInput);
}

}  // namespace testsuite
#endif

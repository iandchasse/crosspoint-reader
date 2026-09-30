#if CROSSPOINT_TEST_SUITE
// T12: the touch controller's sleep command (TP-01, TP-02; bring-up B9). J4's
// 3V3 is not switched, so every sleep entry must hibernate a fitted FT6336U
// (0xA5 = 0x03). Three paths, each checked by its result and by the address no
// longer answering: the sleep entry this boot woke from (the SDK's RTC record
// of prepareTouchForSleep(), NACK then reset and retry included), the same
// call now, and the reset-then-hibernate of a reset-loop sleep. The controller
// stays asleep until a restart, whose boot probe resets it; TP_RST (IO11) is
// never held LOW here.
#include <Arduino.h>
#include <BoardConfig.h>
#include <HalGPIO.h>
#include <InputManager.h>
#include <Logging.h>
#include <Memory.h>
#include <SharedI2c.h>

#include "TestFactories.h"
#include "TestNames.h"
#include "TextBuffer.h"
#include "activities/testsuite/TestCatalog.h"
#include "activities/testsuite/TestScreen.h"
#include "activities/testsuite/TestSuiteAccess.h"

namespace testsuite {
namespace {

using Result = InputManager::TouchSleepResult;

constexpr int QUESTION_RESTART = 1;
constexpr const char* RESTART_ANSWERS[] = {"Restart now", "Cancel"};
constexpr char RESTART_HEADING[] = "Restart?";
constexpr char RESTART_BODY[] =
    "A software restart. The boot probe resets the touch controller, so touch works again in the menu.";

constexpr char INTRO[] =
    "Confirm: hibernate as each sleep entry does (a NACK: reset, retry). Left: reset, then hibernate (a reset-loop "
    "sleep). Each must be ACKed, then the address go quiet. Right restarts. The sleep entry: sleep, wake, open T12.";
constexpr char CURRENT_NOTE[] =
    "J4 pin 2 (a uA meter in place of R42, TEST-SUITE.md T12): about 220 uA awake and untouched for 10 s, 55 uA "
    "hibernated. About 1.1 mA: TP_RST (IO11) held LOW, a fault.";
constexpr char ASLEEP_NOTE[] = "The controller is asleep: touch is off until a restart. Use the keys; Right restarts.";
constexpr char NO_TOUCH[] =
    "No touch controller answered at boot: nothing to hibernate. A board without a tail still sends one best-effort "
    "command at each sleep entry.";

enum class Path : uint8_t { None, SleepEntry, Normal, Reset };

const char* pathCode(const Path path) {
  switch (path) {
    case Path::SleepEntry:
      return "sleep_entry";
    case Path::Normal:
      return "normal";
    case Path::Reset:
      return "reset";
    case Path::None:
      break;
  }
  return "none";
}

const char* ackText(const int ack) { return ack < 0 ? "n/a" : ack != 0 ? "ACK" : "NACK"; }

struct Attempt {
  Path path = Path::None;
  Result result = Result::NotApplicable;
  bool retried = false;  // the first command was not ACKed: reset and a second one
  int ackBefore = -1;    // -1 not probed, 0 NACK, 1 ACK
  int ackAfter = -1;
  unsigned long tookMs = 0;
};

// The sleep entry a boot woke from is logged once, by the first opening that sees it.
bool sleepEntryRecorded = false;

InputManager& touchInput() { return TestSuiteAccess::input(gpio); }

// Address-only probe of the touch controller on the sensor bus: -1 when it is not there.
int probe(const uint8_t addr) {
  const auto& t = BoardConfig::ACTIVE.touch;
  if (addr == 0 || !freeink::i2c::isSensorBus(t.sda, t.scl)) return -1;
  const auto scan = freeink::i2c::scanSensorBus(0, addr, addr);
  if (!scan.ok) return -1;
  return scan.acked(addr) ? 1 : 0;
}

class TouchSleepTest final : public TestScreen {
 public:
  TouchSleepTest(const TestInfo& info, GfxRenderer& renderer, MappedInputManager& mappedInput)
      : TestScreen(info, renderer, mappedInput) {}

 private:
  void onStart() override {
    diag = touchInput().touchDiagnostics();
    active = diag.active;
    address = diag.address != 0 ? diag.address : BoardConfig::ACTIVE.touch.i2cAddress;
    if (diag.previousSleepKnown && !sleepEntryRecorded) {
      sleepEntryRecorded = true;
      recordSleepEntry();
    }
  }

  const char* slotLabel(const Slot slot) const override {
    switch (slot) {
      case Slot::Confirm:
        return active ? "Hibernate" : nullptr;
      case Slot::Left:
        return active ? "Reset+hib" : nullptr;
      case Slot::Right:
        return "Restart";
      default:
        return nullptr;
    }
  }

  void onSlot(const Slot slot) override {
    if (slot == Slot::Confirm && active) hibernate(Path::Normal);
    if (slot == Slot::Left && active) hibernate(Path::Reset);
    if (slot == Slot::Right) ask(QUESTION_RESTART, RESTART_HEADING, RESTART_BODY, RESTART_ANSWERS, 2);
  }

  void onAnswer(const int question, const int answer) override {
    if (question != QUESTION_RESTART || answer != 0) return;
    LOG_INF("TEST", "T12: software restart; the boot probe resets the touch controller");
    delay(50);  // let the log line out
    ESP.restart();
  }

  void recordSleepEntry() {
    Attempt entry;
    entry.path = Path::SleepEntry;
    entry.result = diag.previousSleepResult;
    entry.retried = diag.previousSleepRetried;
    last = entry;
    Verdict verdict = Verdict::Info;
    const char* fails = "none";
    if (entry.result == Result::Asleep) {
      verdict = Verdict::Pass;
    } else if (entry.result == Result::NotApplicable) {
      verdict = Verdict::Skip;
    } else if (active) {
      verdict = Verdict::Fail;  // a controller that answers now did not take its sleep command
      fails = "no_ack";
    }
    record(verdict, "path=sleep_entry result=%s retried=%d ctrl=%s@0x%02x fails=%s", touchSleepResultName(entry.result),
           entry.retried ? 1 : 0, touchControllerName(BoardConfig::ACTIVE.touch.controller), address, fails);
  }

  void hibernate(const Path path) {
    Attempt attempt;
    attempt.path = path;
    const uint32_t resetsBefore = touchInput().touchDiagnostics().resetPulses;
    attempt.ackBefore = probe(address);
    const unsigned long start = millis();
    attempt.result = path == Path::Reset ? touchInput().resetAndHibernateTouch() : touchInput().prepareTouchForSleep();
    attempt.tookMs = millis() - start;
    attempt.ackAfter = probe(address);
    diag = touchInput().touchDiagnostics();
    attempt.retried = path == Path::Normal && diag.sleepRetried;
    last = attempt;
    if (attempt.result == Result::Asleep) asleep = true;
    LOG_INF("TEST", "T12: %s hibernate: %s, address %s after", pathCode(path), touchSleepResultName(attempt.result),
            ackText(attempt.ackAfter));

    char fails[48] = "";
    Verdict verdict = Verdict::Pass;
    if (attempt.result == Result::NotApplicable) {
      verdict = Verdict::Skip;
    } else {
      if (attempt.result != Result::Asleep) addFailure(fails, sizeof(fails), "no_ack");
      if (attempt.ackAfter == 1) addFailure(fails, sizeof(fails), "still_answers");
      if (fails[0] != '\0') {
        verdict = Verdict::Fail;
      } else if (attempt.ackAfter < 0) {
        verdict = Verdict::Info;  // asleep by its ACK, the NACK after could not be checked
      }
    }
    record(verdict,
           "path=%s result=%s retried=%d ack_before=%s ack_after=%s took=%lums ctrl=%s@0x%02x resets=%u fails=%s",
           pathCode(path), touchSleepResultName(attempt.result), attempt.retried ? 1 : 0, ackText(attempt.ackBefore),
           ackText(attempt.ackAfter), attempt.tookMs, touchControllerName(BoardConfig::ACTIVE.touch.controller),
           address, static_cast<unsigned>(diag.resetPulses - resetsBefore), fails[0] != '\0' ? fails : "none");
  }

  void drawBody(TestPage& page) const override {
    if (!active) {
      page.paragraph(NO_TOUCH, 3);
    } else {
      page.paragraph(INTRO, 5);
    }
    page.gap(4);
    page.setCompact(true);
    page.rowf("Controller", "%s at 0x%02x", active ? touchControllerName(BoardConfig::ACTIVE.touch.controller) : "none",
              address);
    if (diag.previousSleepKnown) {
      page.rowf("Sleep entry woken from", "%s%s", touchSleepResultName(diag.previousSleepResult),
                diag.previousSleepRetried ? " after a reset" : "");
    } else {
      page.row("Sleep entry woken from", "none: not a deep-sleep wake");
    }
    if (last.path == Path::Normal || last.path == Path::Reset) {
      page.rowf("Last hibernate", "%s: %s%s", pathCode(last.path), touchSleepResultName(last.result),
                last.retried ? " after a reset" : "");
      page.rowf("Address before / after", "%s / %s", ackText(last.ackBefore), ackText(last.ackAfter));
      page.rowf("Took", "%lu ms", last.tookMs);
    }
    page.rowf("Reset pulses this boot", "%u", static_cast<unsigned>(diag.resetPulses));
    page.setCompact(false);
    page.gap(4);
    page.paragraph(CURRENT_NOTE, 4);
    if (asleep) page.paragraph(ASLEEP_NOTE, 2);
  }

  InputManager::TouchDiagnostics diag{};
  bool active = false;
  bool asleep = false;
  uint8_t address = 0;
  Attempt last;
};

}  // namespace

std::unique_ptr<Activity> createTouchSleepTest(const TestInfo& info, GfxRenderer& renderer,
                                               MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<TouchSleepTest>(info, renderer, mappedInput);
}

}  // namespace testsuite
#endif

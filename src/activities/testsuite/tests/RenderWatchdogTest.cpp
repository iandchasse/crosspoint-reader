#if CROSSPOINT_TEST_SUITE
// P9: the render watchdog (docs/silkscreen/POWER.md, AW-04). Confirm hangs the render task for good, the way the 3.68"
// panel's freeze did, to see the watchdog end the session: a bound of 30 s in RAM in place of the reader's 5 min, a
// restart into deep sleep with this screen left on the panel, and, at the boot after the power key, a P9 row
// (action=watchdog) with how long the render had run, which screen it was and how much of the watchdog task's stack
// was free. The reader's bound (renderwatchdog::BOUND_MS) is not touched; the test's lives in RAM and dies with the
// restart it causes.
//
// What the hung render() holds is what a stuck display driver can hold, because the claim being tested is that the
// watchdog waits for none of it: the render lock (TestScreen::render() leaves it with the caller), the ADC lock
// (InputManager's ladder poll waits for it without a bound, which should stop the loop task passing too, so that the
// watchdog has to read the charge pin itself, with the lock held), and the SPI bus twice over (the second
// SPI.beginTransaction() waits for the class's own non-recursive lock, which the first holds, with the HAL's bus
// lock: the freeze).
#include "RenderWatchdogTest.h"

#include <Arduino.h>
#include <BoardConfig.h>
#include <HalGPIO.h>
#include <Logging.h>
#include <Memory.h>
#include <SPI.h>
#include <SharedAdc.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "TestFactories.h"
#include "activities/testsuite/TestCatalog.h"
#include "activities/testsuite/TestResultLog.h"
#include "activities/testsuite/TestScreen.h"
#include "util/RenderWatchdog.h"

#if !FREEINK_CAP_POWER_POLICY
#error "P9 tests the Silkscreen render watchdog: only the Silkscreen test envs build it"
#endif

namespace testsuite {
namespace {

// The bound once the render is hung, in RAM (renderwatchdog::setBoundForTest). The reader's is BOUND_MS.
constexpr uint32_t TEST_BOUND_MS = 30000;
// The watchdog looks every TICK_MS, so the restart comes between the bound and the bound plus one tick after the
// render began; a second more for the scheduler.
constexpr uint32_t WINDOW_MS = renderwatchdog::TICK_MS + 1000;
// The watchdog task's stack is 4096 bytes. Less than this free at the restart fails, less than WARN warns.
constexpr uint32_t STACK_FAIL_BYTES = 512;
constexpr uint32_t STACK_WARN_BYTES = 1024;
// A name for a list in a row's fails= or warns=, joined with '+'.
void addToList(char* list, const size_t size, const char* name) {
  const size_t used = strlen(list);
  snprintf(list + used, size - used, "%s%s", used != 0 ? "+" : "", name);
}

constexpr int QUESTION_HANG = 1;
constexpr const char* HANG_ANSWERS[] = {"Cancel", "Hang render()"};
constexpr char HANG_HEADING[] = "Hang render()?";
constexpr char HANG_BODY[] =
    "The render task will never return. Keys stop. About 30-35 s on battery the watchdog restarts the chip into deep "
    "sleep; press POWER to wake. On USB nothing happens until the cable is out. Reset the board to cancel.";

constexpr char INTRO[] =
    "Hangs render() for good, as the 3.68\" panel's freeze did, to watch the render watchdog end the session on "
    "battery: restart, deep sleep with this screen on the panel, wake on POWER, then a P9 row from the boot. Unplug "
    "USB first (it acts on battery only), measure the sleep current as P5 does, then wake it.";
constexpr char HUNG_TEXT[] =
    "Hung. The render lock, the ADC lock and the SPI bus are held. Within about 35 s on battery the chip restarts "
    "into deep sleep and the panel keeps this screen. Press POWER to wake: the menu opens and P9 shows its result.";
constexpr char NOT_RUNNING_TEXT[] = "The watchdog task is not running: nothing to test.";

// What a stuck display driver can hold. The ADC lock is taken for good, and so is the SPI bus: the second
// beginTransaction() blocks in the class's own lock, with the first one's HAL lock held.
[[noreturn]] void hangForGood() {
  const freeink::adc::Lock adc;
  SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
  SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
  for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
}

class RenderWatchdogTest final : public TestScreen {
 public:
  RenderWatchdogTest(const TestInfo& info, GfxRenderer& renderer, MappedInputManager& mappedInput)
      : TestScreen(info, renderer, mappedInput) {}

 private:
  void onStart() override { taskRunning = renderwatchdog::running(); }

  const char* slotLabel(const Slot slot) const override {
    return slot == Slot::Confirm && taskRunning && !hangPending.load() ? "Hang" : nullptr;
  }

  void onSlot(const Slot) override { ask(QUESTION_HANG, HANG_HEADING, HANG_BODY, HANG_ANSWERS, 2); }

  void onAnswer(const int question, const int answer) override {
    if (question != QUESTION_HANG || answer != 1 || !taskRunning) return;
    const bool usb = gpio.isUsbConnected();
    renderwatchdog::setBoundForTest(TEST_BOUND_MS);
    LOG_INF("TEST", "P9: render() hangs for good; bound %lu ms, supply %s", static_cast<unsigned long>(TEST_BOUND_MS),
            usb ? "usb" : "battery");
    record(Verdict::Info, "action=arm bound_ms=%lu supply=%s expect_ms=%lu..%lu",
           static_cast<unsigned long>(TEST_BOUND_MS), usb ? "usb" : "battery",
           static_cast<unsigned long>(TEST_BOUND_MS), static_cast<unsigned long>(TEST_BOUND_MS + WINDOW_MS));
    hangPending.store(true);
    requestUpdate();
  }

  // The base draws the page (the supply, the arm row, the hung text) and leaves the render lock with the caller;
  // the hang comes after the frame is on the panel.
  void render(RenderLock&& lock) override {
    TestScreen::render(std::move(lock));
    if (hangPending.load()) hangForGood();
  }

  void drawBody(TestPage& page) const override {
    if (!hangPending.load()) {
      page.paragraph(INTRO, 8);
      page.gap(6);
    }
    page.setCompact(true);
    page.row("Watchdog task", taskRunning ? "running" : "NOT RUNNING");
    page.row("Supply", gpio.isUsbConnected() ? "USB (it waits)" : "battery");
    page.rowf("The reader's bound", "%lu s", static_cast<unsigned long>(renderwatchdog::BOUND_MS / 1000UL));
    page.rowf("Bound once hung", "%lu s, RAM only", static_cast<unsigned long>(TEST_BOUND_MS / 1000UL));
    page.setCompact(false);
    page.gap(6);
    if (hangPending.load()) {
      page.heading("Hung");
      page.paragraph(HUNG_TEXT, 7);
    } else if (!taskRunning) {
      page.paragraph(NOT_RUNNING_TEXT, 3);
    }
  }

  bool taskRunning = false;
  std::atomic<bool> hangPending{false};  // set by the loop task, read by the render task
};

// "Test P9" for a row: no spaces in a value.
void underscored(const char* name, char* out, const size_t size) {
  snprintf(out, size, "%s", name);
  for (char* c = out; *c != '\0'; ++c) {
    if (*c == ' ') *c = '_';
  }
}

}  // namespace

std::unique_ptr<Activity> createRenderWatchdogTest(const TestInfo& info, GfxRenderer& renderer,
                                                   MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<RenderWatchdogTest>(info, renderer, mappedInput);
}

namespace watchdog {

void reportAtBoot() {
  renderwatchdog::Event event;
  if (!renderwatchdog::reportedThisBoot(event)) return;
  const TestInfo* test = findTest("P9");
  if (test == nullptr) return;
  uint32_t stackFree = 0;
  const bool haveStack = renderwatchdog::reportedStackFree(stackFree);
  char screen[24];
  snprintf(screen, sizeof(screen), "Test %s", test->id);
  char activity[sizeof(event.activity)];
  underscored(event.activity, activity, sizeof(activity));
  char stack[16] = "n/a";
  if (haveStack) snprintf(stack, sizeof(stack), "%lu", static_cast<unsigned long>(stackFree));

  char details[220];
  Verdict verdict = Verdict::Info;
  if (strcmp(event.activity, screen) != 0) {
    // Not P9's hang: the watchdog ended a session of the test firmware by itself, on battery, after the reader's bound.
    snprintf(details, sizeof(details),
             "action=watchdog activity=%s stuck_ms=%lu uptime_ms=%lu stack_free=%s caused_by_p9=0", activity,
             static_cast<unsigned long>(event.stuckMs), static_cast<unsigned long>(event.uptimeMs), stack);
    LOG_ERR("TEST", "The render watchdog ended the last session in %s after %lu s, not in P9", event.activity,
            static_cast<unsigned long>(event.stuckMs / 1000UL));
  } else {
    // Early is a defect. Late is a warning: with USB in when P9 was armed the watchdog waits for the cell, and the
    // restart then comes within one tick of the cable coming out, however long after the bound that was.
    char fails[24] = "";
    char warns[24] = "";
    if (event.stuckMs < TEST_BOUND_MS) addToList(fails, sizeof(fails), "early");
    if (event.stuckMs > TEST_BOUND_MS + WINDOW_MS) addToList(warns, sizeof(warns), "late");
    if (haveStack && stackFree < STACK_FAIL_BYTES) addToList(fails, sizeof(fails), "stack");
    if (haveStack && stackFree >= STACK_FAIL_BYTES && stackFree < STACK_WARN_BYTES) {
      addToList(warns, sizeof(warns), "stack");
    }
    verdict = fails[0] != '\0' ? Verdict::Fail : warns[0] != '\0' ? Verdict::Warn : Verdict::Pass;
    snprintf(
        details, sizeof(details),
        "action=watchdog activity=%s stuck_ms=%lu uptime_ms=%lu expect_ms=%lu..%lu stack_free=%s fails=%s warns=%s",
        activity, static_cast<unsigned long>(event.stuckMs), static_cast<unsigned long>(event.uptimeMs),
        static_cast<unsigned long>(TEST_BOUND_MS), static_cast<unsigned long>(TEST_BOUND_MS + WINDOW_MS), stack,
        fails[0] != '\0' ? fails : "none", warns[0] != '\0' ? warns : "none");
  }
  recordResult(*test, verdict, details);
}

}  // namespace watchdog
}  // namespace testsuite
#endif

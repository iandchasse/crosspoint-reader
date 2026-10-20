#if CROSSPOINT_TEST_SUITE
// P9: the render watchdog (docs/silkscreen/POWER.md, AW-04). Confirm hangs the render task for good, the way the 3.68"
// panel's freeze did, to see the watchdog end the session: a bound of 30 s in RAM in place of the reader's 5 min, a
// restart into deep sleep with this screen left on the panel, and, at the boot after the power key, a P9 row
// (action=watchdog) with how long the render had run, which screen it was and how much of the watchdog task's stack
// was free. The reader's bound (renderwatchdog::BOUND_MS) is not touched; the test's lives in RAM and dies with the
// restart it causes.
//
// What the hung render() holds is what a stuck display driver can hold, because the claim being tested is that the
// watchdog waits for none of it: the render lock (TestScreen::render() leaves it with the caller) and the SPI bus
// twice over (the second SPI.beginTransaction() waits for the class's own non-recursive lock, which the first holds,
// with the HAL's bus lock: the freeze). Confirm hangs with those. Right adds the ADC lock, which InputManager's
// ladder poll waits for without a bound: the loop task then stops passing, and the watchdog has to read the charge
// pin itself, which it cannot with that lock held, so it acts as on battery once the loop has been stale for 15 s,
// USB or not (the bias POWER.md AW-04 documents). Without the ADC lock the pin can be read and the USB rule holds.
//
// Left arms the other thing the watchdog covers: a sleep entry that never finishes. The next time the board is put to
// sleep (hold POWER, or a timeout), enterDeepSleep() hangs for good right after its mark is set, before the sleep
// screen is drawn, with the sleep-entry bound shortened to 30 s in RAM. The restart, the sleep and the row are the
// same, with kind=sleep and activity=sleep_entry. Back disarms it.
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

// The bound once the render is hung, in RAM (renderwatchdog::setBoundForTest), and the sleep entry's
// (setSleepEntryBoundForTest). The reader's are BOUND_MS and SLEEP_ENTRY_BOUND_MS.
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

// Armed by the Left slot, read by enterDeepSleep() on the loop task (hangSleepEntryIfArmed()).
std::atomic<bool> g_sleepHangArmed{false};
// Written just before the sleep entry hangs; RTC_NOINIT, so it survives the restart and the sleep and tells the report
// of the boot after that this hang caused the restart (the watchdog's own record says "sleep entry", whoever caused
// it).
constexpr uint32_t SLEEP_HANG_MAGIC = 0x50395348;  // "P9SH"
RTC_NOINIT_ATTR uint32_t g_sleepHangNote;

constexpr int QUESTION_HANG = 1;
constexpr int QUESTION_HANG_ADC = 2;
constexpr int QUESTION_HANG_SLEEP = 3;
constexpr const char* HANG_ANSWERS[] = {"Cancel", "Hang render()"};
constexpr const char* HANG_SLEEP_ANSWERS[] = {"Cancel", "Arm"};
constexpr char HANG_SLEEP_HEADING[] = "Hang the sleep entry?";
constexpr char HANG_SLEEP_BODY[] =
    "The next sleep (hold POWER, or a timeout) hangs before the sleep screen is drawn. Keys stop. About 30-35 s on "
    "battery the watchdog restarts the chip into deep sleep; press POWER to wake. On USB nothing happens until the "
    "cable is out. Back disarms it.";
constexpr char HANG_HEADING[] = "Hang render()?";
constexpr char HANG_BODY[] =
    "The render task will never return. Keys stop. About 30-35 s on battery the watchdog restarts the chip into deep "
    "sleep; press POWER to wake. On USB nothing happens until the cable is out. Reset the board to cancel.";
constexpr char HANG_ADC_BODY[] =
    "As Hang, and the ADC lock is held too, so the charge pin cannot be read: the watchdog acts as on battery about "
    "30-35 s later, USB or not. Press POWER to wake. Reset the board to cancel.";

constexpr char INTRO[] =
    "Hangs render() for good, as the 3.68\" panel's freeze did, to watch the render watchdog end the session on "
    "battery: restart, deep sleep with this screen on the panel, wake on POWER, then a P9 row from the boot. Unplug "
    "USB first (it acts on battery only), measure the sleep current as P5 does, then wake it. Right also holds the "
    "ADC lock. Left arms a hang in the sleep entry instead.";
constexpr char ARMED_SLEEP_TEXT[] =
    "Armed. The next sleep (hold POWER, or a timeout) hangs before the sleep screen is drawn. Within about 35 s on "
    "battery the chip restarts into deep sleep. Press POWER to wake: the menu opens and P9 shows its result. Back "
    "disarms it.";
constexpr char HUNG_TEXT[] =
    "Hung. The render lock and the SPI bus are held. Within about 35 s on battery the chip restarts into deep sleep "
    "and the panel keeps this screen. Press POWER to wake: the menu opens and P9 shows its result.";
constexpr char HUNG_ADC_TEXT[] =
    "Hung. The render lock, the SPI bus and the ADC lock are held. Within about 35 s, USB or not, the chip restarts "
    "into deep sleep and the panel keeps this screen. Press POWER to wake: the menu opens and P9 shows its result.";
constexpr char NOT_RUNNING_TEXT[] = "The watchdog task is not running: nothing to test.";

enum class Hang : uint8_t { None, Spi, SpiAndAdc };

// What a stuck display driver can hold: the SPI bus is taken for good, and the second beginTransaction() blocks in
// the class's own lock with the first one's HAL lock held. Never returns.
[[noreturn]] void hangOnSpi() {
  SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
  SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
  for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
}

[[noreturn]] void hangForGood(const Hang hang) {
  if (hang == Hang::SpiAndAdc) {
    const freeink::adc::Lock adc;  // never released: hangOnSpi() does not return
    hangOnSpi();
  }
  hangOnSpi();
}

class RenderWatchdogTest final : public TestScreen {
 public:
  RenderWatchdogTest(const TestInfo& info, GfxRenderer& renderer, MappedInputManager& mappedInput)
      : TestScreen(info, renderer, mappedInput) {}

 public:
  // Leaving the screen disarms the sleep-entry hang and puts the sleep-entry bound back.
  void onExit() override {
    disarmSleepHang();
    TestScreen::onExit();
  }

 private:
  void onStart() override { taskRunning = renderwatchdog::running(); }

  const char* slotLabel(const Slot slot) const override {
    if (!taskRunning || hang.load() != Hang::None || g_sleepHangArmed.load()) return nullptr;
    return slot == Slot::Confirm ? "Hang"
           : slot == Slot::Right ? "Hang+ADC"
           : slot == Slot::Left  ? "Hang sleep"
                                 : nullptr;
  }

  void onSlot(const Slot slot) override {
    if (slot == Slot::Confirm) ask(QUESTION_HANG, HANG_HEADING, HANG_BODY, HANG_ANSWERS, 2);
    if (slot == Slot::Right) ask(QUESTION_HANG_ADC, HANG_HEADING, HANG_ADC_BODY, HANG_ANSWERS, 2);
    if (slot == Slot::Left) ask(QUESTION_HANG_SLEEP, HANG_SLEEP_HEADING, HANG_SLEEP_BODY, HANG_SLEEP_ANSWERS, 2);
  }

  static void disarmSleepHang() {
    g_sleepHangArmed.store(false);
    renderwatchdog::setSleepEntryBoundForTest(0);
  }

  // Back leaves the test, which disarms; while armed, the first Back only disarms and stays.
  bool onBack() override {
    if (!g_sleepHangArmed.load()) return false;
    disarmSleepHang();
    requestUpdate();
    return true;
  }

  void onAnswer(const int question, const int answer) override {
    if (question == QUESTION_HANG_SLEEP) {
      if (answer != 1 || !taskRunning) return;
      const bool usb = gpio.isUsbConnected();
      renderwatchdog::setSleepEntryBoundForTest(TEST_BOUND_MS);
      g_sleepHangArmed.store(true);
      LOG_INF("TEST", "P9: the next sleep entry hangs for good; bound %lu ms, supply %s",
              static_cast<unsigned long>(TEST_BOUND_MS), usb ? "usb" : "battery");
      record(Verdict::Info, "action=arm mode=sleep bound_ms=%lu supply=%s expect_ms=%lu..%lu",
             static_cast<unsigned long>(TEST_BOUND_MS), usb ? "usb" : "battery",
             static_cast<unsigned long>(TEST_BOUND_MS), static_cast<unsigned long>(TEST_BOUND_MS + WINDOW_MS));
      requestUpdate();
      return;
    }
    if ((question != QUESTION_HANG && question != QUESTION_HANG_ADC) || answer != 1 || !taskRunning) return;
    const Hang mode = question == QUESTION_HANG_ADC ? Hang::SpiAndAdc : Hang::Spi;
    const bool usb = gpio.isUsbConnected();
    const char* modeName = mode == Hang::SpiAndAdc ? "spi_adc" : "spi";
    renderwatchdog::setBoundForTest(TEST_BOUND_MS);
    LOG_INF("TEST", "P9: render() hangs for good (%s); bound %lu ms, supply %s", modeName,
            static_cast<unsigned long>(TEST_BOUND_MS), usb ? "usb" : "battery");
    record(Verdict::Info, "action=arm mode=%s bound_ms=%lu supply=%s expect_ms=%lu..%lu", modeName,
           static_cast<unsigned long>(TEST_BOUND_MS), usb ? "usb" : "battery",
           static_cast<unsigned long>(TEST_BOUND_MS), static_cast<unsigned long>(TEST_BOUND_MS + WINDOW_MS));
    hang.store(mode);
    requestUpdate();
  }

  // The base draws the page (the supply, the arm row, the hung text) and leaves the render lock with the caller;
  // the hang comes after the frame is on the panel.
  void render(RenderLock&& lock) override {
    TestScreen::render(std::move(lock));
    const Hang mode = hang.load();
    if (mode != Hang::None) hangForGood(mode);
  }

  void drawBody(TestPage& page) const override {
    const Hang mode = hang.load();
    const bool armed = g_sleepHangArmed.load();
    if (mode == Hang::None && !armed) {
      page.paragraph(INTRO, 9);
      page.gap(6);
    }
    page.setCompact(true);
    page.row("Watchdog task", taskRunning ? "running" : "NOT RUNNING");
    page.row("Supply", gpio.isUsbConnected() ? "USB (it waits)" : "battery");
    page.rowf("The reader's bounds", "%lu s render, %lu s sleep",
              static_cast<unsigned long>(renderwatchdog::BOUND_MS / 1000UL),
              static_cast<unsigned long>(renderwatchdog::SLEEP_ENTRY_BOUND_MS / 1000UL));
    page.rowf("Bound once hung", "%lu s, RAM only", static_cast<unsigned long>(TEST_BOUND_MS / 1000UL));
    page.setCompact(false);
    page.gap(6);
    if (mode != Hang::None) {
      page.heading("Hung");
      page.paragraph(mode == Hang::SpiAndAdc ? HUNG_ADC_TEXT : HUNG_TEXT, 7);
    } else if (armed) {
      page.heading("Armed");
      page.paragraph(ARMED_SLEEP_TEXT, 7);
    } else if (!taskRunning) {
      page.paragraph(NOT_RUNNING_TEXT, 3);
    }
  }

  bool taskRunning = false;
  std::atomic<Hang> hang{Hang::None};  // set by the loop task, read by the render task
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

void hangSleepEntryIfArmed() {
  if (!g_sleepHangArmed.load()) return;
  g_sleepHangNote = SLEEP_HANG_MAGIC;
  LOG_INF("TEST", "P9: the sleep entry hangs for good");
  for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
}

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
  // Whose hang it was: a render is P9's when it is P9's own screen; a sleep entry is, when the hook left its note.
  const bool sleepEntry = event.kind == renderwatchdog::Kind::SleepEntry;
  const bool hungByP9 = sleepEntry ? g_sleepHangNote == SLEEP_HANG_MAGIC : strcmp(event.activity, screen) == 0;
  g_sleepHangNote = 0;
  const char* kind = sleepEntry ? "sleep" : "render";

  char details[240];
  Verdict verdict = Verdict::Info;
  if (!hungByP9) {
    // Not P9's hang: the watchdog ended a session of the test firmware by itself, on battery, after the reader's bound.
    snprintf(details, sizeof(details),
             "action=watchdog kind=%s activity=%s stuck_ms=%lu uptime_ms=%lu stack_free=%s caused_by_p9=0", kind,
             activity, static_cast<unsigned long>(event.stuckMs), static_cast<unsigned long>(event.uptimeMs), stack);
    char what[96];
    renderwatchdog::describe(event, what, sizeof(what));
    LOG_ERR("TEST", "The render watchdog ended the last session, not in P9: %s", what);
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
    snprintf(details, sizeof(details),
             "action=watchdog kind=%s activity=%s stuck_ms=%lu uptime_ms=%lu expect_ms=%lu..%lu stack_free=%s fails=%s "
             "warns=%s",
             kind, activity, static_cast<unsigned long>(event.stuckMs), static_cast<unsigned long>(event.uptimeMs),
             static_cast<unsigned long>(TEST_BOUND_MS), static_cast<unsigned long>(TEST_BOUND_MS + WINDOW_MS), stack,
             fails[0] != '\0' ? fails : "none", warns[0] != '\0' ? warns : "none");
  }
  recordResult(*test, verdict, details);
}

}  // namespace watchdog
}  // namespace testsuite
#endif

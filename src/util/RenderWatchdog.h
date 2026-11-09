#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

// The Silkscreen render watchdog (docs/silkscreen/POWER.md, AW-04). Firmware hooks sit behind FREEINK_CAP_POWER_POLICY
// (including RenderWatchdogProgress.h's shared helper), and RenderWatchdog.cpp is empty elsewhere.
//
// Silkscreen has no hardware off, and every way to sleep it (the power key, the auto-sleep timer, the awake ceiling,
// the low-battery cutoff) ends in enterDeepSleep() and ActivityManager::goToSleep(), which takes the render lock and
// draws the sleep screen on the loop task (SleepActivity::onEnter()). A render task that never returns from
// Activity::render() keeps that lock: no sleep path can run, the e-paper keeps the last page so the device looks off,
// and the board draws 30-45 mA until the cell is flat. A sleep entry that hangs on its own, in the sleep screen's
// bitmap decode or its panel refresh, leaves the same picture. The ESP32's task watchdog does not see either (it
// watches CPU0's idle task only), and a check inside loop() is no answer: loop() is the task that is stuck, or it
// waits for the render lock.
//
// So two marks are kept, each a Stamp, and a task of its own, independent of the loop task and of the render task,
// looks at both every TICK_MS:
//   - the render task stamps the time just before it calls render() (once it holds the lock) and clears it just
//     after; a render that is still making progress re-stamps it (progress()). A render() that has run for BOUND_MS
//     on the cell without progress is hung.
//   - enterDeepSleep() stamps the time at its top and never clears it, because startDeepSleep() does not return. A
//     sleep entry that has not slept after SLEEP_ENTRY_BOUND_MS on the cell is hung.
// Either restarts the chip, and the boot that follows sleeps at once through the reset loop's driver-free sleep
// (main.cpp). The restart is the reason this works from any state: it needs no lock, so a task that is stuck holding
// the SPI, I2C, ADC, storage, log or front-light lock cannot stop it.
namespace renderwatchdog {

// How long one render() may run without progress before it counts as hung. Above the longest legitimate stretch: the
// cold layout of a huge chapter between two chunks (progress() runs per chunk), a large image decode, and three e-ink
// refresh sequences of 30-90 s each.
constexpr uint32_t BOUND_MS = 5UL * 60UL * 1000UL;
// How long a sleep entry may take before it counts as hung. A sleep entry draws the sleep screen (the page rebuilt from
// the book's cache, an image decoded, one refresh), may deliver plugin events over the network (a slow server costs
// about 30 s a request), and ends in deep sleep; a cut here costs little, because the end state is asleep.
constexpr uint32_t SLEEP_ENTRY_BOUND_MS = 3UL * 60UL * 1000UL;
// How often the task looks. The cell pays for the bound plus up to this much.
constexpr uint32_t TICK_MS = 5000;
// The loop task passes noteLoopPass() every few tens of ms, and the USB answer it keeps is only as fresh as that. Past
// this many ms without a pass, the answer is not trusted (see loopIsStale()).
constexpr uint32_t LOOP_STALE_MS = 15000;
// A stamp up to this many ms "in the future" is one the render task wrote after the clock was read, not a render that
// began 49.7 days ago.
constexpr uint32_t FUTURE_GUARD_MS = 0x10000;

static_assert(BOUND_MS >= 3UL * 90UL * 1000UL, "the bound must clear three 90 s e-ink refresh sequences");
static_assert(SLEEP_ENTRY_BOUND_MS >= 2UL * 60UL * 1000UL && SLEEP_ENTRY_BOUND_MS < BOUND_MS,
              "the sleep entry's bound must clear a slow plugin delivery and a refresh, and sit under the render's");
static_assert(TICK_MS * 2 < SLEEP_ENTRY_BOUND_MS && BOUND_MS < UINT32_MAX - FUTURE_GUARD_MS,
              "tick, bounds and the guard must nest");

// What was hung: a render(), or a sleep entry.
enum class Kind : uint8_t { Render = 0, SleepEntry = 1 };

// A mark that has run for at least boundMs on the cell. `stampMs` is the Stamp's value: 0 while nothing runs, otherwise
// millis() at the start. The difference is unsigned, so it stays right when millis() wraps (every 49.7 days). The task
// reads the stamp before the clock, so the stamp is never later than `nowMs`; a caller that reads them the other way
// round can see one that is, a few microseconds, and that comes out as a huge difference, which FUTURE_GUARD_MS keeps
// from reading as a run of 49 days. On USB the device is powered and a hang is left as it is.
constexpr bool renderStuck(const uint32_t nowMs, const uint32_t stampMs, const uint32_t boundMs, const bool onBattery) {
  if (!onBattery || stampMs == 0) return false;
  const uint32_t runningMs = nowMs - stampMs;
  return runningMs >= boundMs && runningMs <= UINT32_MAX - FUTURE_GUARD_MS;
}

// The verdict over both marks: whether either is hung, which one, and for how long. When both are, the one that has run
// longer is the cause (a sleep entry that waits for the lock of a render that hung before it began).
struct Hung {
  bool hung;
  Kind kind;
  uint32_t stuckMs;
};

constexpr Hung decide(const uint32_t nowMs, const uint32_t renderStampMs, const uint32_t renderBoundMs,
                      const uint32_t sleepStampMs, const uint32_t sleepBoundMs, const bool onBattery) {
  const bool render = renderStuck(nowMs, renderStampMs, renderBoundMs, onBattery);
  const bool sleep = renderStuck(nowMs, sleepStampMs, sleepBoundMs, onBattery);
  const uint32_t renderMs = render ? nowMs - renderStampMs : 0;
  const uint32_t sleepMs = sleep ? nowMs - sleepStampMs : 0;
  if (sleep && (!render || sleepMs > renderMs)) return {true, Kind::SleepEntry, sleepMs};
  if (render) return {true, Kind::Render, renderMs};
  return {false, Kind::Render, 0};
}

// Whether to treat the device as running from the cell. HalGPIO::isUsbConnected() answers from the charge ladder that
// loop() polls, so while the loop task is stuck too (a key press that waits for the render lock stops it, and so does
// a sleep entry that never returns to loop()) the answer stays what it was, and a cable pulled afterwards would never
// be seen. A loop that has not passed for staleMs hands the question to a fresh reading of the ladder pin instead
// (usbPresentNow() in RenderWatchdog.cpp).
constexpr bool loopIsStale(const uint32_t loopAgeMs, const uint32_t staleMs) { return loopAgeMs > staleMs; }

// A task's mark: 0 while nothing runs, otherwise millis() at the start, never 0.
class Stamp {
 public:
  void begin(const uint32_t nowMs) { value_.store(nowMs != 0 ? nowMs : 1, std::memory_order_release); }
  void end() { value_.store(0, std::memory_order_release); }
  uint32_t get() const { return value_.load(std::memory_order_acquire); }
  // Moves a running mark forward and leaves an idle one alone: a refresh that meets the end of the render cannot
  // leave a stale mark behind. Compare-and-swap, because the end may come between the read and the write.
  void refresh(const uint32_t nowMs) {
    uint32_t current = value_.load(std::memory_order_acquire);
    const uint32_t next = nowMs != 0 ? nowMs : 1;
    while (current != 0 &&
           !value_.compare_exchange_weak(current, next, std::memory_order_release, std::memory_order_acquire)) {
    }
  }

 private:
  std::atomic<uint32_t> value_{0};
};

// What the RTC record keeps across the restart and the sleep, for the log line and the report of the next boot.
struct Event {
  uint32_t stuckMs = 0;      // how long it had run (since the last progress) when the watchdog acted
  uint32_t uptimeMs = 0;     // millis() then
  char activity[24] = {};    // the activity whose render() it was (truncated); "sleep entry" for a sleep entry
  Kind kind = Kind::Render;  // what was hung
};

// The firmware side (RenderWatchdog.cpp; Silkscreen only).

// Starts the task: once, after the render task exists. A second call does nothing; if the task could not be created,
// noteLoopPass() tries again (once a minute, five times).
void start();
// From the render task with the render lock held, immediately before / after Activity::render(). The name is the
// activity's, copied here.
void beginRender(const char* activityName);
void endRender();
// From inside a long render(), where it has just made progress (one chunk of a section build, one cover): the render
// task's mark moves forward. Does nothing from any other task, and nothing when no render is in flight.
void progress();
// From the top of enterDeepSleep(), on the loop task: a sleep entry has begun. It is never cleared: startDeepSleep()
// does not return.
void beginSleepEntry();
// From loop(), once per pass: the loop task is alive.
void noteLoopPass();
// setup(), early (beside the reset loop's sleep): the previous session ended in the watchdog's restart. True once;
// the caller sleeps at once, without starting anything.
bool takeSleepRequest(Event& out);
// Any later boot that stays up: the session before the last sleep ended that way. True once, for the log line.
bool takeReport(Event& out);
// sleepIfResetLoop(): the reset loop sleeps instead of this boot, so the pending sleep request is spent; the next boot
// reports the event and does not sleep again.
void resetLoopSlept();
// setup(), before anything logs: a watchdog restart is pending or has just slept, so the log ring in RTC memory still
// holds the hung session's last lines. The caller keeps the ring (Logging's holdLastLogs()) until takeReport()
// has printed it.
bool holdsLogs();
// A line for the log: "render() of Settings had run for 301 s" or "the sleep entry had run for 181 s".
void describe(const Event& event, char* out, size_t size);

#if CROSSPOINT_TEST_SUITE
// The hardware test firmware only (P9, docs/silkscreen/TEST-SUITE.md): the reader images carry none of these.
// A shorter bound for render() and one for the sleep entry, in RAM: they die with the restart that they cause. 0 puts
// the constant back.
void setBoundForTest(uint32_t boundMs);
void setSleepEntryBoundForTest(uint32_t boundMs);
// The task exists.
bool running();
// takeReport() found a record at this boot: the record, for the test that causes one to read.
bool reportedThisBoot(Event& out);
// The same, and the task's free stack at the restart in bytes (the lowest it had been); false if there is no note.
bool reportedStackFree(uint32_t& freeBytes);
#endif

}  // namespace renderwatchdog

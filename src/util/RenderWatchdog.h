#pragma once

#include <atomic>
#include <cstdint>

// The Silkscreen render watchdog (docs/silkscreen/POWER.md, AW-04). Compiled into Silkscreen builds only: every hook
// that uses it sits behind FREEINK_CAP_POWER_POLICY, and RenderWatchdog.cpp is empty elsewhere.
//
// Silkscreen has no hardware off, and every way to sleep it (the power key, the auto-sleep timer, the awake ceiling,
// the low-battery cutoff) ends in ActivityManager::goToSleep(), which takes the render lock and has the render task
// draw the sleep screen. A render task that never returns from Activity::render() keeps that lock: no sleep path can
// run, the e-paper keeps the last page so the device looks off, and the board draws 30-45 mA until the cell is flat.
// The ESP32's task watchdog does not see it (it watches CPU0's idle task only), and a check inside loop() is no
// answer: ActivityManager::loop() itself waits for the render lock.
//
// So the render task stamps the time into a Stamp just before it calls render() (once it holds the lock) and clears it
// just after, and a task of its own, independent of the loop task and of the render task, looks at the stamp every
// TICK_MS. A render that has run for BOUND_MS on the cell restarts the chip, and the boot that follows sleeps at once
// through the reset loop's driver-free sleep (main.cpp). The restart is the reason this works from any state: it
// needs no lock, so a task that is stuck holding the SPI, I2C, ADC, storage, log or front-light lock cannot stop it.
namespace renderwatchdog {

// How long one render() may run before it counts as hung. Above the longest legitimate one: the cold layout of a huge
// chapter, a large image decode, and three e-ink refresh sequences of 30-90 s each.
constexpr uint32_t BOUND_MS = 5UL * 60UL * 1000UL;
// How often the task looks. The cell pays for BOUND_MS plus up to this much.
constexpr uint32_t TICK_MS = 5000;
// The loop task passes noteLoopPass() every few tens of ms, and the USB answer it keeps is only as fresh as that. Past
// this many ms without a pass, the answer is not trusted (see loopIsStale()).
constexpr uint32_t LOOP_STALE_MS = 15000;
// A stamp up to this many ms "in the future" is one the render task wrote after the clock was read, not a render that
// began 49.7 days ago.
constexpr uint32_t FUTURE_GUARD_MS = 0x10000;

static_assert(BOUND_MS >= 3UL * 90UL * 1000UL, "the bound must clear three 90 s e-ink refresh sequences");
static_assert(TICK_MS * 2 < BOUND_MS && BOUND_MS < UINT32_MAX - FUTURE_GUARD_MS, "tick, bound and the guard must nest");

// A render that has run for at least boundMs on the cell. `stampMs` is the Stamp's value: 0 while no render runs,
// otherwise millis() at the start of the current one. The difference is unsigned, so it stays right when millis()
// wraps (every 49.7 days). The task reads the stamp before the clock, so the stamp is never later than `nowMs`; a
// caller that reads them the other way round can see one that is, a few microseconds, and that comes out as a huge
// difference, which FUTURE_GUARD_MS keeps from reading as a render of 49 days. On USB the device is powered and a
// hung render is left as it is.
constexpr bool renderStuck(const uint32_t nowMs, const uint32_t stampMs, const uint32_t boundMs, const bool onBattery) {
  if (!onBattery || stampMs == 0) return false;
  const uint32_t runningMs = nowMs - stampMs;
  return runningMs >= boundMs && runningMs <= UINT32_MAX - FUTURE_GUARD_MS;
}

// Whether to treat the device as running from the cell. HalGPIO::isUsbConnected() answers from the charge ladder that
// loop() polls, so while the loop task is stuck too (a key press that waits for the render lock stops it) the answer
// stays what it was, and a cable pulled afterwards would never be seen. A loop that has not passed for staleMs hands
// the question to a fresh reading of the ladder pin instead (usbPresentNow() in RenderWatchdog.cpp).
constexpr bool loopIsStale(const uint32_t loopAgeMs, const uint32_t staleMs) { return loopAgeMs > staleMs; }

// The render task's mark: 0 while no render runs, otherwise millis() at the start of the current render, never 0.
class Stamp {
 public:
  void begin(const uint32_t nowMs) { value_.store(nowMs != 0 ? nowMs : 1, std::memory_order_release); }
  void end() { value_.store(0, std::memory_order_release); }
  uint32_t get() const { return value_.load(std::memory_order_acquire); }

 private:
  std::atomic<uint32_t> value_{0};
};

// What the RTC record keeps across the restart and the sleep, for the log line and the report of the next boot.
struct Event {
  uint32_t stuckMs = 0;    // how long the render had run when the watchdog acted
  uint32_t uptimeMs = 0;   // millis() then
  char activity[24] = {};  // the activity whose render() it was (truncated)
};

// The firmware side (RenderWatchdog.cpp; Silkscreen only).

// Starts the task: once, after the render task exists. A second call does nothing.
void start();
// From the render task with the render lock held, immediately before / after Activity::render(). The name is the
// activity's, copied here.
void beginRender(const char* activityName);
void endRender();
// From loop(), once per pass: the loop task is alive.
void noteLoopPass();
// setup(), early (beside the reset loop's sleep): the previous session ended in the watchdog's restart. True once;
// the caller sleeps at once, without starting anything.
bool takeSleepRequest(Event& out);
// Any later boot that stays up: the session before the last sleep ended that way. True once, for the log line.
bool takeReport(Event& out);

#if CROSSPOINT_TEST_SUITE
// The hardware test firmware only (P9, docs/silkscreen/TEST-SUITE.md): the reader images carry none of these.
// A shorter bound, in RAM: it dies with the restart that it causes. 0 puts BOUND_MS back.
void setBoundForTest(uint32_t boundMs);
// The task exists.
bool running();
// takeReport() found a record at this boot: the record, for the test that causes one to read.
bool reportedThisBoot(Event& out);
// The same, and the task's free stack at the restart in bytes (the lowest it had been); false if there is no note.
bool reportedStackFree(uint32_t& freeBytes);
#endif

}  // namespace renderwatchdog

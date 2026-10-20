// Host test of the render watchdog's decision (src/util/RenderWatchdog.h): when a render, or a sleep entry, counts as
// hung. It runs the header the firmware compiles; test_render_watchdog.py builds and runs it (and its negative
// controls). Every check is a CHECK() that counts and prints, not assert(): a build with NDEBUG must not turn this into
// a pass that ran nothing, and main() fails a run in which no check ran.
#include <cstdint>
#include <cstdio>

#include "RenderWatchdog.h"

namespace {
int failures = 0;
int checks = 0;
}  // namespace

#define CHECK(cond)                                       \
  do {                                                    \
    ++checks;                                             \
    if (!(cond)) {                                        \
      ++failures;                                         \
      std::printf("FAIL line %d: %s\n", __LINE__, #cond); \
    }                                                     \
  } while (0)

using renderwatchdog::BOUND_MS;
using renderwatchdog::decide;
using renderwatchdog::Kind;
using renderwatchdog::renderStuck;
using renderwatchdog::SLEEP_ENTRY_BOUND_MS;
using renderwatchdog::Stamp;

namespace {

// No render runs: the stamp is 0, whatever the clock says and however long ago the last render ended.
void idleIsNeverStuck() {
  CHECK(!renderStuck(0, 0, BOUND_MS, true));
  CHECK(!renderStuck(1000, 0, BOUND_MS, true));
  CHECK(!renderStuck(BOUND_MS * 10, 0, BOUND_MS, true));
  CHECK(!renderStuck(0xFFFFFFFFu, 0, BOUND_MS, true));
}

// The bound's edge: a render that has run for exactly the bound is hung, one millisecond short of it is not.
void boundEdge() {
  const uint32_t start = 123456;
  CHECK(!renderStuck(start, start, BOUND_MS, true));
  CHECK(!renderStuck(start + 1, start, BOUND_MS, true));
  CHECK(!renderStuck(start + BOUND_MS - 1, start, BOUND_MS, true));
  CHECK(renderStuck(start + BOUND_MS, start, BOUND_MS, true));
  CHECK(renderStuck(start + BOUND_MS + 1, start, BOUND_MS, true));
  CHECK(renderStuck(start + BOUND_MS * 3, start, BOUND_MS, true));
  // The bound is the caller's: a shorter one moves the edge with it.
  CHECK(!renderStuck(start + 29999, start, 30000, true));
  CHECK(renderStuck(start + 30000, start, 30000, true));
}

// millis() wraps every 49.7 days. The difference is unsigned, so a render that began just before the wrap and has run
// past the bound is hung, and one that has not is not.
void wrapAround() {
  const uint32_t start = 0xFFFFFF00u;  // 256 ms before the wrap
  CHECK(!renderStuck(0xFFFFFFFFu, start, BOUND_MS, true));
  CHECK(!renderStuck(start + BOUND_MS - 1, start, BOUND_MS, true));  // wraps to a small value
  CHECK(start + BOUND_MS < start);                                   // the test really crosses the wrap
  CHECK(renderStuck(start + BOUND_MS, start, BOUND_MS, true));
  CHECK(renderStuck(start + BOUND_MS + 5000, start, BOUND_MS, true));
  // A render that began at millis() == 1 (the lowest stamp) and one that ends at the top of the range.
  CHECK(!renderStuck(BOUND_MS, 1, BOUND_MS, true));
  CHECK(renderStuck(BOUND_MS + 1, 1, BOUND_MS, true));
  CHECK(!renderStuck(0xFFFFFFFFu, 0xFFFFFFFFu - (BOUND_MS - 1), BOUND_MS, true));
  CHECK(renderStuck(0xFFFFFFFFu, 0xFFFFFFFFu - BOUND_MS, BOUND_MS, true));
}

// On USB the device is powered: a hung render is left alone, however long it has run.
void usbSuppresses() {
  const uint32_t start = 5000;
  CHECK(!renderStuck(start + BOUND_MS, start, BOUND_MS, false));
  CHECK(!renderStuck(start + BOUND_MS * 100, start, BOUND_MS, false));
  CHECK(!renderStuck(0xFFFFFFFFu, 1, BOUND_MS, false));
  // ... and it counts again the moment the cable is out (the stamp is old by then).
  CHECK(renderStuck(start + BOUND_MS * 100, start, BOUND_MS, true));
}

// A stamp written by the render task after the clock was read is later than `now`, so the difference is huge: that is
// a render that has not run at all, not one that has run for 49 days.
void stampFromTheFuture() {
  using renderwatchdog::FUTURE_GUARD_MS;
  CHECK(!renderStuck(1000, 1001, BOUND_MS, true));
  CHECK(!renderStuck(1000, 5000, BOUND_MS, true));
  CHECK(!renderStuck(0, 1, BOUND_MS, true));
  CHECK(!renderStuck(1000, 1000 + FUTURE_GUARD_MS, BOUND_MS, true));     // the edge of the guard: still the future
  CHECK(renderStuck(1000, 1000 + FUTURE_GUARD_MS + 1, BOUND_MS, true));  // one past it: a render of 49.7 days
  // A real old render is stuck: one on USB for 25 days that is unplugged now.
  const uint32_t days25 = 25u * 24u * 60u * 60u * 1000u;
  CHECK(renderStuck(days25 + 7, 7, BOUND_MS, true));
  CHECK(!renderStuck(days25 + 7, 7, BOUND_MS, false));
}

// The Stamp, in the order ActivityManager::renderTaskLoop() uses it. Waiting for the render lock is not rendering: the
// stamp is taken once the lock is held, so a long wait never counts.
void waitingForTheLockDoesNotCount() {
  Stamp stamp;
  CHECK(stamp.get() == 0);
  uint32_t now = 1000;
  // The render task is blocked on the lock for twice the bound (a long render by the main task, say).
  now += BOUND_MS * 2;
  CHECK(!renderStuck(now, stamp.get(), BOUND_MS, true));
  // It gets the lock and starts rendering: the clock starts here.
  stamp.begin(now);
  CHECK(stamp.get() == now);
  CHECK(!renderStuck(now + BOUND_MS - 1, stamp.get(), BOUND_MS, true));
  CHECK(renderStuck(now + BOUND_MS, stamp.get(), BOUND_MS, true));
  // render() returns: idle again, and the next wait does not inherit anything.
  stamp.end();
  CHECK(stamp.get() == 0);
  CHECK(!renderStuck(now + BOUND_MS * 5, stamp.get(), BOUND_MS, true));
  // A new render restarts the clock from its own start.
  const uint32_t second = now + BOUND_MS * 5;
  stamp.begin(second);
  CHECK(!renderStuck(second + 1000, stamp.get(), BOUND_MS, true));
  CHECK(renderStuck(second + BOUND_MS, stamp.get(), BOUND_MS, true));
}

// millis() == 0 at the start of a render must not read as "idle" (it would blind the watchdog for that render).
void stampIsNeverZeroWhileRendering() {
  Stamp stamp;
  stamp.begin(0);
  CHECK(stamp.get() == 1);  // the render began at "millis() == 0": stored as 1, one ms later than it was
  CHECK(!renderStuck(BOUND_MS, stamp.get(), BOUND_MS, true));
  CHECK(renderStuck(BOUND_MS + 1, stamp.get(), BOUND_MS, true));
  stamp.end();
  CHECK(stamp.get() == 0);
}

// The constants the design stands on.
void constants() {
  CHECK(BOUND_MS == 300000);              // five minutes without progress
  CHECK(SLEEP_ENTRY_BOUND_MS == 180000);  // three minutes
  CHECK(SLEEP_ENTRY_BOUND_MS < BOUND_MS);
  CHECK(renderwatchdog::TICK_MS == 5000);
  CHECK(renderwatchdog::TICK_MS * 2 < BOUND_MS);
  CHECK(renderwatchdog::TICK_MS * 2 < SLEEP_ENTRY_BOUND_MS);
  CHECK(BOUND_MS >= 3 * 90 * 1000);  // three 90 s e-ink refresh sequences
  CHECK(renderwatchdog::LOOP_STALE_MS >= 1000);
}

// progress(): a render that is still making progress moves its mark forward. The bound is "no progress for BOUND_MS",
// and a hang inside a chunk still trips it.
void progressMovesTheMark() {
  Stamp stamp;
  // Idle: a refresh leaves the mark at 0, so one that meets the end of the render cannot leave a stale mark behind.
  stamp.refresh(1000);
  CHECK(stamp.get() == 0);
  stamp.begin(1000);
  stamp.end();
  stamp.refresh(2000);
  CHECK(stamp.get() == 0);

  // A long build: progress every 60 s for 20 minutes, looked at on every tick. It is never hung.
  const uint32_t start = 50000;
  const uint32_t length = 20u * 60u * 1000u;
  stamp.begin(start);
  bool everStuck = false;
  for (uint32_t t = 0; t < length; t += renderwatchdog::TICK_MS) {
    if (t % 60000u == 0 && t != 0) stamp.refresh(start + t);
    everStuck = everStuck || renderStuck(start + t, stamp.get(), BOUND_MS, true);
  }
  CHECK(!everStuck);
  // Without a refresh the same render is hung at the bound: the refresh is what keeps this one alive.
  CHECK(renderStuck(start + BOUND_MS, start, BOUND_MS, true));

  // The hang: the last progress is at `last`, then nothing. It trips BOUND_MS after that, not before.
  const uint32_t last = start + length;
  stamp.refresh(last);
  CHECK(stamp.get() == last);
  CHECK(!renderStuck(last + BOUND_MS - 1, stamp.get(), BOUND_MS, true));
  CHECK(renderStuck(last + BOUND_MS, stamp.get(), BOUND_MS, true));
  stamp.end();
  CHECK(stamp.get() == 0);

  // millis() == 0 stores 1, as begin() does; across the wrap the mark still moves and the bound still holds.
  stamp.begin(0xFFFFFF00u);
  stamp.refresh(0);
  CHECK(stamp.get() == 1);
  CHECK(!renderStuck(BOUND_MS, stamp.get(), BOUND_MS, true));
  CHECK(renderStuck(BOUND_MS + 1, stamp.get(), BOUND_MS, true));
  stamp.refresh(0xFFFFFFF0u);
  CHECK(stamp.get() == 0xFFFFFFF0u);
  CHECK(renderStuck(0xFFFFFFF0u + BOUND_MS, stamp.get(), BOUND_MS, true));  // wraps
}

// Both marks: the sleep entry's is shorter, the render's longer, and the one that has run longer is the cause.
void decideOverBothMarks() {
  const uint32_t t0 = 777000;
  // Nothing runs.
  CHECK(!decide(t0, 0, BOUND_MS, 0, SLEEP_ENTRY_BOUND_MS, true).hung);
  CHECK(!decide(0xFFFFFFFFu, 0, BOUND_MS, 0, SLEEP_ENTRY_BOUND_MS, true).hung);

  // A render alone: hung at BOUND_MS, reported as a render.
  CHECK(!decide(t0 + BOUND_MS - 1, t0, BOUND_MS, 0, SLEEP_ENTRY_BOUND_MS, true).hung);
  const auto render = decide(t0 + BOUND_MS, t0, BOUND_MS, 0, SLEEP_ENTRY_BOUND_MS, true);
  CHECK(render.hung && render.kind == Kind::Render && render.stuckMs == BOUND_MS);

  // A sleep entry alone: hung at SLEEP_ENTRY_BOUND_MS, three minutes, well before a render would be.
  CHECK(!decide(t0 + SLEEP_ENTRY_BOUND_MS - 1, 0, BOUND_MS, t0, SLEEP_ENTRY_BOUND_MS, true).hung);
  const auto sleep = decide(t0 + SLEEP_ENTRY_BOUND_MS, 0, BOUND_MS, t0, SLEEP_ENTRY_BOUND_MS, true);
  CHECK(sleep.hung && sleep.kind == Kind::SleepEntry && sleep.stuckMs == SLEEP_ENTRY_BOUND_MS);
  // The same age as a render mark is not hung yet: each mark has its own bound.
  CHECK(!decide(t0 + SLEEP_ENTRY_BOUND_MS, t0, BOUND_MS, 0, SLEEP_ENTRY_BOUND_MS, true).hung);

  // Both hung: the one that has run longer is the cause. A render that hung before the sleep entry began ...
  const auto both = decide(t0 + BOUND_MS + 90000, t0, BOUND_MS, t0 + 60000, SLEEP_ENTRY_BOUND_MS, true);
  CHECK(both.hung && both.kind == Kind::Render && both.stuckMs == BOUND_MS + 90000);
  // ... and a sleep entry that began first, with a render that began after it and is also past its bound.
  const auto both2 = decide(t0 + BOUND_MS + 20000, t0 + 10000, BOUND_MS, t0, SLEEP_ENTRY_BOUND_MS, true);
  CHECK(both2.hung && both2.kind == Kind::SleepEntry && both2.stuckMs == BOUND_MS + 20000);
  // A sleep entry past its bound is the cause even while a render that began later is still inside its own.
  const auto later = decide(t0 + BOUND_MS - 1, t0 + 60000, BOUND_MS, t0, SLEEP_ENTRY_BOUND_MS, true);
  CHECK(later.hung && later.kind == Kind::SleepEntry && later.stuckMs == BOUND_MS - 1);
  // A tie goes to the render.
  const auto tie = decide(t0 + BOUND_MS, t0, BOUND_MS, t0, SLEEP_ENTRY_BOUND_MS, true);
  CHECK(tie.hung && tie.kind == Kind::Render);

  // On USB neither counts.
  CHECK(!decide(t0 + BOUND_MS * 10, t0, BOUND_MS, t0, SLEEP_ENTRY_BOUND_MS, false).hung);
  CHECK(!decide(t0 + SLEEP_ENTRY_BOUND_MS * 10, 0, BOUND_MS, t0, SLEEP_ENTRY_BOUND_MS, false).hung);

  // Across the wrap of millis().
  const uint32_t before = 0xFFFFFF00u;
  CHECK(!decide(before + SLEEP_ENTRY_BOUND_MS - 1, 0, BOUND_MS, before, SLEEP_ENTRY_BOUND_MS, true).hung);
  CHECK(decide(before + SLEEP_ENTRY_BOUND_MS, 0, BOUND_MS, before, SLEEP_ENTRY_BOUND_MS, true).kind ==
        Kind::SleepEntry);
  // A sleep stamp from the future (written after the clock was read) is not a sleep entry of 49 days.
  CHECK(!decide(1000, 0, BOUND_MS, 1000 + 5000, SLEEP_ENTRY_BOUND_MS, true).hung);
}

// The loop's heartbeat: stale only past the limit.
void loopStaleness() {
  using renderwatchdog::loopIsStale;
  CHECK(!loopIsStale(0, renderwatchdog::LOOP_STALE_MS));
  CHECK(!loopIsStale(renderwatchdog::LOOP_STALE_MS, renderwatchdog::LOOP_STALE_MS));
  CHECK(loopIsStale(renderwatchdog::LOOP_STALE_MS + 1, renderwatchdog::LOOP_STALE_MS));
  CHECK(loopIsStale(0xFFFFFFFFu, renderwatchdog::LOOP_STALE_MS));
}

}  // namespace

int main() {
  idleIsNeverStuck();
  boundEdge();
  wrapAround();
  usbSuppresses();
  stampFromTheFuture();
  waitingForTheLockDoesNotCount();
  stampIsNeverZeroWhileRendering();
  constants();
  loopStaleness();
  progressMovesTheMark();
  decideOverBothMarks();
  std::printf("render_watchdog host test: %d checks, %d failed\n", checks, failures);
  if (checks == 0) {
    std::printf("no check ran\n");
    return 1;
  }
  return failures == 0 ? 0 : 1;
}

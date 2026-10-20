// Host test of the render watchdog's decision (src/util/RenderWatchdog.h): when a render counts as hung. It runs the
// header the firmware compiles; test_render_watchdog.py builds and runs it (and its negative controls). Every check
// is a CHECK() that counts and prints, not assert(): a build with NDEBUG must not turn this into a pass that ran
// nothing.
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
using renderwatchdog::renderStuck;
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
  CHECK(BOUND_MS == 300000);  // five minutes
  CHECK(renderwatchdog::TICK_MS == 5000);
  CHECK(renderwatchdog::TICK_MS * 2 < BOUND_MS);
  CHECK(BOUND_MS >= 3 * 90 * 1000);  // three 90 s e-ink refresh sequences
  CHECK(renderwatchdog::LOOP_STALE_MS >= 1000);
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
  std::printf("render_watchdog host test: %d checks, %d failed\n", checks, failures);
  return failures == 0 ? 0 : 1;
}

#pragma once

// P9: the render watchdog (docs/silkscreen/POWER.md, AW-04; CROSSPOINT_TEST_SUITE builds only).
#if CROSSPOINT_TEST_SUITE
namespace testsuite::watchdog {

// Once per boot, when the menu first opens: if the render watchdog ended the session before the last sleep, records
// the result of that restart as a P9 row (action=watchdog), whoever caused it. Loop task only.
void reportAtBoot();

// From enterDeepSleep(), on the loop task, right after the sleep entry's mark is set
// (renderwatchdog::beginSleepEntry()): if the operator armed P9's third mode, the sleep entry hangs here for good,
// before the sleep screen is drawn, and the watchdog (its sleep-entry bound shortened to 30 s in RAM) restarts the chip
// into deep sleep. Does nothing otherwise.
void hangSleepEntryIfArmed();

}  // namespace testsuite::watchdog
#endif

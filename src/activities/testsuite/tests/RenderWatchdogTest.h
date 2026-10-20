#pragma once

// P9: the render watchdog (docs/silkscreen/POWER.md, AW-04; CROSSPOINT_TEST_SUITE builds only).
#if CROSSPOINT_TEST_SUITE
namespace testsuite::watchdog {

// Once per boot, when the menu first opens: if the render watchdog ended the session before the last sleep, records
// the result of that restart as a P9 row (action=watchdog), whoever caused it. Loop task only.
void reportAtBoot();

}  // namespace testsuite::watchdog
#endif

#pragma once

// Which panel family a test build drives, for the display tests (CROSSPOINT_TEST_SUITE builds only).
//
// The SSD1677 and UC8279 X4 drivers report every refresh they fire to the EpdBus diagnostics, the gray
// and settle passes included, and the references of the 4.26" board are theirs. The Silkscreen 3.68"
// variant (FREEINK_SILKSCREEN_368) runs the SDK's X3 UC8279d driver, which reports only the B/W
// refreshes it starts from displayStart(). On that build:
//   - a gray pass or a settle pass leaves no record: its rows read "not reported", and the checks that
//     need a record are not made (the operator's look at the glass is the verdict);
//   - a refresh may run in another mode than the one asked, by the driver's design: FULL and HALF both
//     run its one GC bank (it has no half bank) and are reported as asked, FAST runs the DU bank only
//     with a previous frame in the controller, so a FAST after begin() or after a resync runs GC and
//     reports FULL (the first two content paints after begin() are GC whatever was asked, and the app
//     turns a HALF into a resync on the X3 panel): recorded, not failed;
//   - the 4.26" panel's reference times and its FULL >= HALF > FAST order do not apply: "no reference
//     yet, record it". FULL and HALF are one bank here, so their times should be about equal.
#if CROSSPOINT_TEST_SUITE
#include <BoardConfig.h>

namespace testsuite {

inline constexpr bool X3_PANEL_BUILD = FREEINK_SILKSCREEN_368 != 0;

}  // namespace testsuite
#endif

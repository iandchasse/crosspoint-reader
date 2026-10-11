#pragma once

// Which panel family a test build drives, for the display tests (CROSSPOINT_TEST_SUITE builds only).
//
// The SSD1677 and UC8279 X4 drivers report every refresh they fire to the EpdBus diagnostics, the gray
// and settle passes included, and the references of the 4.26" board are theirs. The Silkscreen 3.68"
// variant (FREEINK_SILKSCREEN_368) runs the SDK's X3 drivers, UC8253 or UC8279d, which report only the
// B/W refreshes they start from displayStart(). On that build:
//   - a gray pass or a settle pass leaves no record: its rows read "not reported", and the checks that
//     need a record are not made (the operator's look at the glass is the verdict);
//   - a refresh may run in another mode than the one asked, by the driver's design (the first paints
//     after begin() are full syncs, and the app turns a HALF into a resync on the X3 panel): it is
//     recorded, not failed;
//   - the 4.26" panel's reference times and its FULL >= HALF > FAST order do not apply: "no reference
//     yet, record it".
#if CROSSPOINT_TEST_SUITE
#include <BoardConfig.h>

namespace testsuite {

inline constexpr bool X3_PANEL_BUILD = FREEINK_SILKSCREEN_368 != 0;

}  // namespace testsuite
#endif

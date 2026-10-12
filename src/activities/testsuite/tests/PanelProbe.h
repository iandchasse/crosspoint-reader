#pragma once

// The panel controller this boot resolved (the SDK's XteinkDetect display probe
// under the profile's DisplayProbeConfig), shown and logged by P1 and T4
// (CROSSPOINT_TEST_SUITE builds only). On the Silkscreen 3.68" variant nothing is
// probed: the controller is the X3's UC8279d, fixed in the profile, and the build
// reads no VER. The rows say that and add the driver that ran and the geometry.
#if CROSSPOINT_TEST_SUITE
#include <cstddef>

#include "activities/testsuite/TestResultLog.h"

namespace testsuite {

class TestPage;

// "controller=UC8279 source=probed verdict=ultrachip ver=00-00-02-FF-FF flg=13
// lut_ver=02 variant=02 mtp=A5..." (the MTP header only when the probe read it).
// On the 3.68" variant, which probes nothing (no ver=, no verdict=, no flg=):
// "controller=UC8279 source=fixed probe=none variant=00 driver=Uc8279Driver x3_mode=1 geom=792x528 seq=97".
void formatPanelProbe(char* buf, size_t size);
// "UC8279:probed:000002FFFF:13" for a run's result; "-" for IDs no probe read
// ("UC8279:fixed:-:-" on the 3.68" variant).
void formatPanelToken(char* buf, size_t size);
// PASS: the probe, this boot or kept from the probe before a deep sleep, resolved
// a controller. INFO: the build forced it, so nothing was probed. FAIL: an
// inconclusive probe (the panel then runs as the profile's default controller,
// SSD1677) or no resolution at all. The 3.68" variant probes nothing, so it has
// no `probe_inconclusive` and no BUSY_N warning: INFO, or FAIL on `x3_mode`,
// `geometry`, `controller` or `driver` (the facade, the profile and the last
// refresh's report disagree: consistency checks, nothing reads the glass). An INFO
// or a PASS means the controller is settled, not that a panel is there. `fails`
// gets the failed check, or "none".
Verdict panelProbeVerdict(char* fails, size_t size);
// Body rows: the controller and how it was chosen, the probe's verdict and IDs.
// `brief`: two rows; otherwise also LUT_VER, the variant and the MTP header. The
// 3.68" variant has two rows always: the controller (fixed in this build, no VER
// probe) and the driver / geometry.
void drawPanelProbe(TestPage& page, bool brief);

}  // namespace testsuite
#endif

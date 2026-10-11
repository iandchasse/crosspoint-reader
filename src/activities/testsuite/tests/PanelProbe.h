#pragma once

// The panel controller this boot resolved (the SDK's XteinkDetect display probe
// under the profile's DisplayProbeConfig), shown and logged by P1 and T4
// (CROSSPOINT_TEST_SUITE builds only). On the Silkscreen 3.68" variant the probe
// is the X3's stock one: three VER bytes, no FLG and no MTP, byte 2 decides
// (0x66 a UC8279d, 0xFF a UC8253), and the rows add the driver that ran and the
// geometry, so the first bench step (read VER) is one row of P1.
#if CROSSPOINT_TEST_SUITE
#include <cstddef>

#include "activities/testsuite/TestResultLog.h"

namespace testsuite {

class TestPage;

// "controller=UC8279 source=probed verdict=ultrachip ver=00-00-02-FF-FF flg=13
// lut_ver=02 variant=02 mtp=A5..." (the MTP header only when the probe read it).
void formatPanelProbe(char* buf, size_t size);
// "UC8279:probed:000002FFFF:13" for a run's result; "-" for IDs no probe read.
void formatPanelToken(char* buf, size_t size);
// PASS: the probe, this boot or kept from the probe before a deep sleep, resolved
// a controller. INFO: the build forced it, so nothing was probed. FAIL: an
// inconclusive probe (the panel then runs as SSD1677) or no resolution at all.
// `fails` gets the failed check, or "none".
Verdict panelProbeVerdict(char* fails, size_t size);
// Body rows: the controller and how it was chosen, the probe's verdict and IDs.
// `brief`: two rows; otherwise also LUT_VER, the variant and the MTP header.
void drawPanelProbe(TestPage& page, bool brief);

}  // namespace testsuite
#endif

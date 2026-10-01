#pragma once

// The SDK's boot-time hardware checks (ProbeChecks.h) as the tests show and log
// them: key-ladder presence, the power pin's idle level, and the touch and RTC
// identity checks (CROSSPOINT_TEST_SUITE builds only). P1 shows all four; P2,
// T3, T11 and T13 use the ones they test.
#if CROSSPOINT_TEST_SUITE
#include <cstddef>

namespace testsuite {

class TestPage;

// Key ladders (PowerConfig::ladderPresenceProbe): "IO1:fitted,IO4:off@probed",
// "@cached" after a deep-sleep wake, "not_probed" when the profile does not ask.
void formatLadderToken(char* buf, size_t size);
// Ladder `index` was found with no pull-up and is off for this session.
bool ladderOff(int index);
// The power pin at boot (PowerConfig::powerPinIdleCheck): "low:3mV",
// "high:2990mV", "undefined:1755mV", or "not_checked" (a deep-sleep wake).
void formatPowerIdleToken(char* buf, size_t size);
// The power pin idled between the logic levels at this boot's check.
bool powerIdleFault();
// The touch identity check (TouchConfig::requireFocalTechId): "ok:64" (the chip
// id accepted), "ok" (found without an id read), "rejected:1c", "rejected:cached",
// "none" (nothing answered) or "off".
void formatTouchIdToken(char* buf, size_t size);
// The touch probe met a device whose chip id is not FocalTech's and left it alone.
bool touchIdRejected();
// The RTC identity check (SensorsConfig::rtcIdentityCheck): "ok", "rejected@0x68",
// "none" (no DS3231 found) or "off". Loop task only (it asks the RTC driver).
void formatRtcIdToken(char* buf, size_t size);
// The address the RTC's begin() refused (0 = none).
unsigned rtcRejectedAddress();
// P1's body rows for all four.
void drawBootProbes(TestPage& page);

}  // namespace testsuite
#endif

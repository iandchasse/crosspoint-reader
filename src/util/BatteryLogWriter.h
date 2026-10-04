#if SILKSCREEN_BATLOG
#pragma once

#include <BoardConfig.h>

#if !FREEINK_DEVICE_SILKSCREEN || !FREEINK_CAP_POWER_POLICY
#error "SILKSCREEN_BATLOG is for the Silkscreen device only (FREEINK_DEVICE_SILKSCREEN, FREEINK_CAP_POWER_POLICY)"
#endif
#if CROSSPOINT_TEST_SUITE
#error "SILKSCREEN_BATLOG never goes into the hardware test firmware: it writes only under /.silkscreen-test/"
#endif

#include <cstdint>

// The SD side of the Silkscreen battery log (the silkscreen-batlog image). The SDK formats the pending row
// (freeink::batterylog, BatteryLog.h) and never opens a file; this appends it to /silkscreen-battery/battery.csv, at
// most once per verified wake: one open, one write per line, one close, timed, with the back-off the SDK keeps in its
// RTC record. From the loop task only.
namespace BatteryLogWriter {

// The loop's flush, on a pass after the first paint: writes the pending row once the card is ready, the first 2.5 s of
// the boot have passed and the back-off is clear. A few RAM loads when nothing waits.
void flushIfDue();

// The fallback in the sleep entry, with the card still mounted, for a row the loop never flushed because the session
// ended first. Only with the radio off and no earlier bad attempt: an SD stall with the radio on costs far more.
void flushAtEntry();

// USB presence (0 or 1) and the charge state ('?' unknown, 'b' on battery, 'c' charging, 'f' complete, 'x' external
// idle, 'n' no cell) as the rows record them. USB reads as unknown (0xFF) until the charge ladder has decoded.
uint8_t usbPresent();
char chargeState();

}  // namespace BatteryLogWriter
#endif  // SILKSCREEN_BATLOG

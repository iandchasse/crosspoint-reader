#pragma once

// The RTC as the tests see it (CROSSPOINT_TEST_SUITE builds only): HalClock's
// own driver (TestSuiteAccess), raw register reads on the sensor bus, and UTC
// epoch arithmetic. Loop task only: the render task never touches the bus.
#if CROSSPOINT_TEST_SUITE
#include <Rtc.h>

#include <cstdint>

namespace testsuite::rtc {

// HalClock found an RTC at boot.
bool present();
// HalClock's driver: address(), type(), now(), set().
Rtc& driver();

// A UTC calendar time to Unix seconds and back (no time zone involved).
int64_t toEpoch(const Rtc::DateTime& utc);
void fromEpoch(int64_t epoch, Rtc::DateTime& utc);

// Waits for the RTC's next seconds tick (at most TICK_WAIT_MS), then returns
// the time just after it (Unix seconds, UTC) and millis() when it was seen, to
// within a few ms. False when the RTC is absent, reports its time lost, or did
// not tick.
inline constexpr unsigned long TICK_WAIT_MS = 1100;
bool readAtTick(uint32_t& epoch, unsigned long& atMs);

// `len` registers from `reg` at `addr` on the sensor bus. Counts nothing.
bool readRegisters(uint8_t addr, uint8_t reg, uint8_t* dst, uint8_t len);

// After a test writes the RTC: HalClock reads the chip again, and T14 does not
// judge an elapsed time that spans the write.
void noteWritten();
bool writtenThisBoot();

}  // namespace testsuite::rtc
#endif

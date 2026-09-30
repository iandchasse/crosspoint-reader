#pragma once

// Short names for SDK enums on test screens and in results.csv
// (CROSSPOINT_TEST_SUITE builds only).
#if CROSSPOINT_TEST_SUITE
#include <BoardConfig.h>

namespace testsuite {

const char* chargeStateName(BoardConfig::ChargeState state);               // "OnBattery", "Charging", ...
const char* touchControllerName(BoardConfig::TouchController controller);  // "FT5x06", "GT911", ...
const char* rtcTypeName(BoardConfig::RtcType type);                        // "DS3231", "PCF85063", ...

}  // namespace testsuite
#endif

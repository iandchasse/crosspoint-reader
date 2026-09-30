#pragma once

// Short names for SDK enums on test screens and in results.csv
// (CROSSPOINT_TEST_SUITE builds only).
#if CROSSPOINT_TEST_SUITE
#include <BoardConfig.h>
#include <InputManager.h>
#include <esp_sleep.h>
#include <esp_system.h>

#include <cstddef>
#include <cstdint>

namespace testsuite {

const char* chargeStateName(BoardConfig::ChargeState state);               // "OnBattery", "Charging", ...
const char* touchControllerName(BoardConfig::TouchController controller);  // "FT5x06", "GT911", ...
const char* rtcTypeName(BoardConfig::RtcType type);                        // "DS3231", "PCF85063", ...
const char* touchSleepResultName(InputManager::TouchSleepResult result);   // "Asleep", "NoAnswer", ...
const char* resetReasonName(esp_reset_reason_t reason);                    // "POWERON", "DEEPSLEEP", ...
const char* wakeCauseName(esp_sleep_wakeup_cause_t cause);                 // "none", "EXT1", ...
// "0x" + hex of an ext1 wake mask (printf's %llx is not relied on).
void formatWakeMask(char* buf, size_t size, uint64_t mask);
// This boot is a deep-sleep wake by ext1 with the power button's bit set (the
// board's only wake source).
bool powerButtonWake();

}  // namespace testsuite
#endif

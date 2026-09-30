#if CROSSPOINT_TEST_SUITE
#include "TestNames.h"

namespace testsuite {

const char* chargeStateName(const BoardConfig::ChargeState state) {
  using BoardConfig::ChargeState;
  switch (state) {
    case ChargeState::Unknown:
      return "Unknown";
    case ChargeState::OnBattery:
      return "OnBattery";
    case ChargeState::Charging:
      return "Charging";
    case ChargeState::Complete:
      return "Complete";
    case ChargeState::ExternalIdle:
      return "ExternalIdle";
    case ChargeState::NoBattery:
      return "NoBattery";
  }
  return "?";
}

const char* touchControllerName(const BoardConfig::TouchController controller) {
  using BoardConfig::TouchController;
  switch (controller) {
    case TouchController::None:
      return "none";
    case TouchController::Chsc6x:
      return "CHSC6x";
    case TouchController::Gt911:
      return "GT911";
    case TouchController::Ft5x06:
      return "FT5x06";
    case TouchController::Ft6336u:
      return "FT6336U";
    case TouchController::Gslx680:
      return "GSLx680";
  }
  return "?";
}

const char* rtcTypeName(const BoardConfig::RtcType type) {
  using BoardConfig::RtcType;
  switch (type) {
    case RtcType::None:
      return "none";
    case RtcType::Pcf8563:
      return "PCF8563";
    case RtcType::Ds3231:
      return "DS3231";
    case RtcType::Rx8130:
      return "RX8130";
    case RtcType::Pcf85063:
      return "PCF85063";
  }
  return "?";
}

const char* touchSleepResultName(const InputManager::TouchSleepResult result) {
  using Result = InputManager::TouchSleepResult;
  switch (result) {
    case Result::NotApplicable:
      return "NotApplicable";
    case Result::Asleep:
      return "Asleep";
    case Result::NoAnswer:
      return "NoAnswer";
  }
  return "?";
}

}  // namespace testsuite
#endif

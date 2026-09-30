#if CROSSPOINT_TEST_SUITE
#include "TestNames.h"

#include <cstdio>

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

const char* resetReasonName(const esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON:
      return "POWERON";
    case ESP_RST_EXT:
      return "EXT";
    case ESP_RST_SW:
      return "SW";
    case ESP_RST_PANIC:
      return "PANIC";
    case ESP_RST_INT_WDT:
      return "INT_WDT";
    case ESP_RST_TASK_WDT:
      return "TASK_WDT";
    case ESP_RST_WDT:
      return "WDT";
    case ESP_RST_DEEPSLEEP:
      return "DEEPSLEEP";
    case ESP_RST_BROWNOUT:
      return "BROWNOUT";
    case ESP_RST_SDIO:
      return "SDIO";
    case ESP_RST_USB:
      return "USB";
    case ESP_RST_JTAG:
      return "JTAG";
    case ESP_RST_EFUSE:
      return "EFUSE";
    case ESP_RST_PWR_GLITCH:
      return "PWR_GLITCH";
    case ESP_RST_CPU_LOCKUP:
      return "CPU_LOCKUP";
    default:
      return "UNKNOWN";
  }
}

const char* wakeCauseName(const esp_sleep_wakeup_cause_t cause) {
  switch (cause) {
    case ESP_SLEEP_WAKEUP_UNDEFINED:
      return "none";
    case ESP_SLEEP_WAKEUP_EXT0:
      return "EXT0";
    case ESP_SLEEP_WAKEUP_EXT1:
      return "EXT1";
    case ESP_SLEEP_WAKEUP_TIMER:
      return "TIMER";
    case ESP_SLEEP_WAKEUP_GPIO:
      return "GPIO";
    default:
      return "other";
  }
}

void formatWakeMask(char* buf, const size_t size, const uint64_t mask) {
  const auto hi = static_cast<unsigned long>(mask >> 32);
  const auto lo = static_cast<unsigned long>(mask & 0xFFFFFFFFUL);
  if (hi != 0) {
    snprintf(buf, size, "0x%lx%08lx", hi, lo);
  } else {
    snprintf(buf, size, "0x%lx", lo);
  }
}

bool powerButtonWake() {
  const int8_t pin = BoardConfig::ACTIVE.input.power;
  if (pin < 0 || esp_reset_reason() != ESP_RST_DEEPSLEEP) return false;
  if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_EXT1) return false;
  return (esp_sleep_get_ext1_wakeup_status() & (1ULL << pin)) != 0;
}

}  // namespace testsuite
#endif

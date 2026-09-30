#if CROSSPOINT_TEST_SUITE
#include "LightCheck.h"

#include <Arduino.h>
#include <BoardConfig.h>
#include <HalFrontlight.h>
#include <Logging.h>
#include <SharedAdc.h>
#include <driver/gpio.h>
#include <soc/gpio_reg.h>
#include <soc/gpio_sig_map.h>
#include <soc/soc.h>

#include "activities/testsuite/TestSuiteAccess.h"

namespace testsuite::light {

bool monitorConfigured() {
  const auto& fl = BoardConfig::ACTIVE.frontlight;
  return Frontlight.present() && fl.monitorAdc != BoardConfig::PIN_UNASSIGNED && fl.monitorScaleX1000 != 0 &&
         profileTripMv() != 0;
}

uint32_t ledFromPin(const uint32_t pinMv) { return pinMv * BoardConfig::ACTIVE.frontlight.monitorScaleX1000 / 1000U; }

uint32_t pinFromLed(const uint32_t ledMv) {
  const uint32_t scale = BoardConfig::ACTIVE.frontlight.monitorScaleX1000;
  return scale != 0 ? ledMv * 1000U / scale : 0;
}

Reading readMonitor(const uint8_t samples) {
  Reading reading;
  if (!monitorConfigured()) return reading;
  const uint8_t pin = static_cast<uint8_t>(BoardConfig::ACTIVE.frontlight.monitorAdc);
  const uint8_t n = samples == 0 ? 1 : samples;
  uint32_t sum = 0;
  {
    const freeink::adc::Lock lock;
    for (uint8_t i = 0; i < n; ++i) sum += analogReadMilliVolts(pin);
  }
  reading.pinMv = sum / n;
  reading.ledMv = ledFromPin(reading.pinMv);
  return reading;
}

FrontlightManager::MonitorDiagnostics monitor() { return TestSuiteAccess::frontlight(Frontlight).monitorDiagnostics(); }

PadState readPad(const int8_t pin) {
  PadState pad;
  if (pin < 0 || !GPIO_IS_VALID_GPIO(pin)) return pad;
  gpio_io_config_t config = {};
  if (gpio_get_io_config(static_cast<gpio_num_t>(pin), &config) != ESP_OK) return pad;
  pad.valid = true;
  pad.outputEnabled = config.oe;
  pad.fromGpioOut = config.sig_out == SIG_GPIO_OUT_IDX;
  pad.pullUp = config.pu;
  pad.pullDown = config.pd;
#ifdef GPIO_OUT1_REG
  const uint32_t out = pin < 32 ? REG_READ(GPIO_OUT_REG) : REG_READ(GPIO_OUT1_REG);
#else
  const uint32_t out = REG_READ(GPIO_OUT_REG);
#endif
  pad.level = ((out >> (static_cast<uint32_t>(pin) & 31U)) & 1U) != 0;
  return pad;
}

uint16_t profileTripMv() {
  static const uint16_t level = BoardConfig::ACTIVE.frontlight.monitorTripMv;
  return level;
}

void setTripMv(const uint16_t mv) {
  const uint16_t profile = profileTripMv();
  const uint16_t level = mv < profile ? mv : profile;
  BoardConfig::ACTIVE.frontlight.monitorTripMv = level;
  LOG_INF("TEST", "Front-light trip level %u mV (profile %u mV), in RAM", level, profile);
}

void restoreTripMv() {
  const uint16_t profile = profileTripMv();
  if (BoardConfig::ACTIVE.frontlight.monitorTripMv == profile) return;
  BoardConfig::ACTIVE.frontlight.monitorTripMv = profile;
  LOG_INF("TEST", "Front-light trip level back to the profile's %u mV", profile);
}

}  // namespace testsuite::light
#endif

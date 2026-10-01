#include "HalFrontlight.h"

#include <BoardConfig.h>
#include <Logging.h>
#include <esp_system.h>

HalFrontlight HalFrontlight::instance;

namespace {
void darkBeforeRestart() { Frontlight.prepareForSleep(); }
}  // namespace

bool HalFrontlight::warmthIsSelection() const {
  return present() && BoardConfig::ACTIVE.frontlight.colorSelectGpio != BoardConfig::PIN_UNASSIGNED;
}

void HalFrontlight::begin(const uint8_t brightness, const uint8_t warmth, const bool on) {
  if (!manager.present()) return;

  // An enable-gated light (colour select / enable pulse / LED monitor) must go
  // dark before COLOR_SEL changes. A software restart releases both pads at once,
  // so take it dark first, in esp_restart()'s shutdown handlers (every restart
  // path, once per boot).
  const auto& fl = BoardConfig::ACTIVE.frontlight;
  static bool shutdownHandlerRegistered = false;
  if (!shutdownHandlerRegistered && (fl.colorSelectGpio != BoardConfig::PIN_UNASSIGNED || fl.enablePulseUs != 0 ||
                                     fl.monitorAdc != BoardConfig::PIN_UNASSIGNED)) {
    shutdownHandlerRegistered = esp_register_shutdown_handler(&darkBeforeRestart) == ESP_OK;
    if (!shutdownHandlerRegistered) LOG_ERR("LIGHT", "No shutdown handler slot; the light may stay lit into a restart");
  }

  manager.begin();
  lastBrightness = brightness > 100 ? 100 : brightness;
  manager.setColorTemperature(warmth > 100 ? 100 : warmth);
  lit = on;
#if FREEINK_CAP_FRONTLIGHT_PROBE
  if (lit && !manager.restoreAllowed()) {
    LOG_INF("LIGHT", "Frontlight not restored: no light answered its last switch-on");
    lit = false;
  }
#endif
  manager.setBrightness(lit ? lastBrightness : 0);
#if FREEINK_CAP_FRONTLIGHT_PROBE
  settleSwitchOn(false);
#endif
  LOG_INF("LIGHT", "Frontlight up: %u%% warm=%u%% %s", lastBrightness, manager.colorTemperature(), lit ? "on" : "off");
}

void HalFrontlight::setBrightness(const uint8_t percent) {
  lastBrightness = percent > 100 ? 100 : percent;
  if (lit) manager.setBrightness(lastBrightness);
}

void HalFrontlight::setWarmth(const uint8_t warmPercent) {
  manager.setColorTemperature(warmPercent > 100 ? 100 : warmPercent);
#if FREEINK_CAP_FRONTLIGHT_PROBE
  settleSwitchOn(true);  // a colour change while lit probes the other string
#endif
}

void HalFrontlight::setOn(const bool on) {
  // Off is judged on the user's state and the fault, not isOn(): a tripped light
  // still has lit set, and switching it off must clear both (re-arming the monitor).
  if (on ? isOn() : !lit && !manager.fault()) return;
  if (on) manager.clearFault();
  lit = on;
  manager.setBrightness(lit ? lastBrightness : 0);
#if FREEINK_CAP_FRONTLIGHT_PROBE
  settleSwitchOn(true);
#endif
}

#if FREEINK_CAP_FRONTLIGHT_PROBE
void HalFrontlight::settleSwitchOn(const bool userAsked) {
  if (!lit || !manager.fault() || manager.load() != freeink::frontlight::Load::Absent) return;
  lit = false;
  manager.setBrightness(0);  // dark and re-armed; the SDK's record keeps "no light"
  if (userAsked) {
    noLightNotice = true;
  } else {
    restoreNoLightNotice = true;
  }
  LOG_ERR("LIGHT", "No front light detected%s", userAsked ? "" : " (Restore Light on Wake)");
}
#endif

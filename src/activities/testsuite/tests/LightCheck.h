#pragma once

// Shared by the front-light tests T9 and T10 (CROSSPOINT_TEST_SUITE builds only):
// the LED monitor (LED_MONIT) read through freeink::adc, the SDK's monitor
// record, the pads as the GPIO matrix drives them, and the trip level a test
// may lower in RAM. Nothing here sets a pin mode or a pull.
#if CROSSPOINT_TEST_SUITE
#include <FrontlightManager.h>

#include <cstdint>

namespace testsuite::light {

// LED_MONIT limits (REQUIREMENTS fl-*), at the ADC pin unless named Led.
// Dark, LED_SW sits one diode below LDO_IN: about 0.25-0.55 V at the pin. Below
// the floor the monitor sees nothing and its trip is blind; above the ceiling
// LED_SW is higher than any LDO_IN, so the boost is running.
inline constexpr uint32_t DARK_MIN_PIN_MV = 100;
inline constexpr uint32_t DARK_MAX_PIN_MV = 700;
// Lit: 22 V caps J6 strings (and stays under the 23 V trip and U10's 24.25 V
// OVP); the bonded J3 strings run at about 15 V at full current, less at the
// lowest step (proposal: 11-18 V).
inline constexpr uint32_t LIT_MAX_LED_MV = 22000;
inline constexpr uint32_t J3_MIN_LED_MV = 11000;
inline constexpr uint32_t J3_MAX_LED_MV = 18000;
// LED_MONIT settles in about 50 ms (107 kOhm, 10.7 ms tau); readings wait this long after a change.
inline constexpr unsigned long SETTLE_MS = 300;

struct Reading {
  uint32_t pinMv = 0;
  uint32_t ledMv = 0;  // pin x FrontlightConfig::monitorScaleX1000 / 1000
};

// The board has the LED monitor (pin, scale, trip level).
bool monitorConfigured();
uint32_t ledFromPin(uint32_t pinMv);
uint32_t pinFromLed(uint32_t ledMv);
// `samples` conversions of the monitor pin, averaged, under the shared ADC lock.
Reading readMonitor(uint8_t samples = 16);
// The SDK's monitor record (trips, last reading), kept after a trip is handled.
// Its trips are the in-use ones; a switch-on probe's are in probe().
FrontlightManager::MonitorDiagnostics monitor();
// The SDK's load-probe record (FrontlightConfig::loadProbeWindowMs): what the last
// switch-on found, each string's state, and the probe's own readings. All zero
// without FREEINK_CAP_FRONTLIGHT_PROBE.
FrontlightManager::ProbeDiagnostics probe();
// The probe decides on the SDK's monitor timer, after the call that switched the
// light on: this blocks until that switch-on has decided (its window, a rail wait
// and the other string's window) and the HAL has settled it, as the main loop
// would. False if it had not decided within switchOnBoundMs().
bool awaitSwitchOn();
// The longest a switch-on may take to decide: three probe windows (a blend's
// first switch-on lights each string, and a missing second one sends it back to
// the first) and the longest rail wait (4 s), with a margin.
unsigned long switchOnBoundMs();
// The string the SDK's colour select picks now. It differs from the warmth asked
// for when the probe found that string missing. While blending: the dominant one.
bool selectsWarm();
// A warmth between the ends blends the strings (FrontlightConfig::colorBlendHz,
// neither string missing), and the colour select runs its blend carrier now.
bool blends();
bool blending();
// Record tokens.
const char* loadToken(freeink::frontlight::Load load);            // unknown, present, absent
const char* stringToken(freeink::frontlight::StringState state);  // unknown, present, missing

// A pad as the GPIO matrix drives it, from the registers: no pin change.
struct PadState {
  bool valid = false;
  bool outputEnabled = false;
  bool fromGpioOut = false;  // routed to the plain GPIO output register (not a peripheral such as LEDC)
  bool level = false;        // that register's bit (meaningful with fromGpioOut)
  bool pullUp = false;
  bool pullDown = false;
};
PadState readPad(int8_t pin);
// The pad is held at `level` by the GPIO output register.
inline bool padHeldAt(const PadState& pad, const bool level) {
  return pad.valid && pad.outputEnabled && pad.fromGpioOut && pad.level == level;
}

// FrontlightConfig::monitorTripMv as the profile has it, captured before any
// test changes it.
uint16_t profileTripMv();
// Sets the trip level in RAM; never above the profile's.
void setTripMv(uint16_t mv);
// Back to the profile's level. Safe to call at any time (exits, the menu).
void restoreTripMv();

}  // namespace testsuite::light
#endif

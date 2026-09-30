#pragma once

// What powers the board, for tests that gate a load on the battery (T15's
// Wi-Fi, P3's card writes) or judge a reading only on the cell (CROSSPOINT_TEST_SUITE
// builds only). One averaged BAT_MONIT reading; never the cutoff's own reading.
#if CROSSPOINT_TEST_SUITE
#include <BatteryMonitor.h>
#include <BoardConfig.h>

#include <cstdint>

namespace testsuite {

struct Supply {
  uint16_t pPlusMv = 0;   // BAT_MONIT x the divider, 16 samples
  bool external = false;  // USB_STAT reports USB power (charging, complete, idle or no cell)
  bool cell = false;      // P+ at least BatteryMonitor::CELL_PRESENT_MIN_MV

  // Running from the cell alone.
  bool onBattery() const { return !external && cell; }
};

inline Supply readSupply() {
  const BatteryMonitor battery;
  Supply supply;
  bool known = false;
  const bool external = battery.isExternalPowerPresent(&known);
  supply.external = (known && external) || BatteryMonitor::chargeStatus() == BoardConfig::ChargeState::NoBattery;
  supply.pPlusMv = battery.readMillivoltsAveraged(16);
  supply.cell = supply.pPlusMv >= BatteryMonitor::CELL_PRESENT_MIN_MV;
  return supply;
}

}  // namespace testsuite
#endif

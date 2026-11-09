#include "HalPowerManager.h"

#include <BoardConfig.h>
#if FREEINK_DEVICE_SILKSCREEN
#include <BlePageTurner.h>
#endif
#include <Logging.h>
#include <PowerManager.h>
#include <SharedI2c.h>
#include <WiFi.h>
#include <esp_sleep.h>
#include <esp_system.h>
#include <soc/soc_caps.h>

#include <algorithm>
#include <cassert>

#include "HalFrontlight.h"
#if FREEINK_DEVICE_SILKSCREEN
#include <Preferences.h>
#include <esp_rtc_time.h>

#include "HalDisplay.h"
#endif
#include "HalGPIO.h"

#if FREEINK_DEVICE_EEGO_A4 || FREEINK_DEVICE_PICCO
#include <HalFrontlight.h>
#endif

#if FREEINK_DEVICE_PICCO
#include <driver/gpio.h>
#endif

#if SILKSCREEN_BATLOG
#include <BatteryLog.h>

#include "HalStorage.h"
#endif

#if FREEINK_DEVICE_PAPERMONO
#include <M5Pm1.h>
#endif

namespace {
#if FREEINK_DEVICE_SILKSCREEN
// RTC RAM, not flash: retain the last trusted display estimate through normal
// sleep and maintenance restarts, including wakes while a cable is attached.
struct RetainedBatteryDisplay {
  uint32_t magic;
  freeink::battery::BatteryChargeEstimator::Saved state;
};
RTC_NOINIT_ATTR volatile RetainedBatteryDisplay retainedBatteryDisplay;
constexpr uint32_t BATTERY_DISPLAY_MAGIC = 0x42445032;  // BDP2
#endif
}  // namespace

HalPowerManager powerManager;  // Singleton instance

// GPIO13 controls the X4 battery latch and the X3 SD power rail on the C3
// Xteink boards. Other boards use it for unrelated signals, including the
// X4 Pro display chip select.
static constexpr gpio_num_t XTEINK_C3_GPIO13 = GPIO_NUM_13;

void HalPowerManager::begin() {
  if (BoardConfig::ACTIVE.batteryAdc >= 0) {
    pinMode(BoardConfig::ACTIVE.batteryAdc, INPUT);
  }
  // Board charger setup (Picco SGM41562: stock charge voltage and limits); a
  // no-op on boards without a configurable charger.
  if (BatteryMonitor::configureCharger()) LOG_INF("PWR", "Charger configured");
#if FREEINK_DEVICE_SILKSCREEN
  if (BoardConfig::isSilkscreen()) {
    Preferences preferences;
    if (preferences.begin("silkbat", true)) {
      const uint32_t packed = preferences.getUInt("cal", 0);
      if ((packed >> 24) == 1)
        batteryDisplay.setCalibration({static_cast<uint16_t>(packed), static_cast<uint8_t>(packed >> 16)});
      preferences.end();
    }
    const auto reason = esp_reset_reason();
    if ((reason == ESP_RST_DEEPSLEEP || reason == ESP_RST_SW) &&
        retainedBatteryDisplay.magic == BATTERY_DISPLAY_MAGIC) {
      freeink::battery::BatteryChargeEstimator::Saved saved{};
      const auto* source = reinterpret_cast<const volatile uint8_t*>(&retainedBatteryDisplay.state);
      auto* target = reinterpret_cast<uint8_t*>(&saved);
      for (size_t i = 0; i < sizeof(saved); ++i) target[i] = source[i];
      batteryDisplay.restore(saved);
      batteryDisplayPercent.store(batteryDisplay.shown(), std::memory_order_release);
#if SILKSCREEN_BATLOG
      batteryDisplayTenths.store(batteryDisplay.save().tenths, std::memory_order_release);
#endif
    } else {
      retainedBatteryDisplay.magic = 0;
    }
  }
#endif
  normalFreq = getCpuFrequencyMhz();
  modeMutex = xSemaphoreCreateMutex();
  assert(modeMutex != nullptr);
}

void HalPowerManager::setPowerSaving(bool enabled) {
  if (normalFreq <= 0) {
    return;  // invalid state
  }

  auto wifiMode = WiFi.getMode();
  if (wifiMode != WIFI_MODE_NULL) {
    // Wifi is active, force disabling power saving
    enabled = false;
  }

  // Held across the check and the switch: a Lock taken in between would find full speed, do
  // nothing, and then run at LOW_POWER_FREQ until the next key press.
  xSemaphoreTake(modeMutex, portMAX_DELAY);
  const LockMode mode = currentLockMode;

  if (mode == None && enabled && !isLowPower) {
    LOG_DBG("PWR", "Going to low-power mode");
    if (!setCpuFrequencyMhz(LOW_POWER_FREQ)) {
      LOG_DBG("PWR", "Failed to set CPU frequency = %d MHz", LOW_POWER_FREQ);
      xSemaphoreGive(modeMutex);
      return;
    }
    isLowPower = true;

  } else if ((!enabled || mode != None) && isLowPower) {
    LOG_DBG("PWR", "Restoring normal CPU frequency");
    if (!setCpuFrequencyMhz(normalFreq)) {
      LOG_DBG("PWR", "Failed to set CPU frequency = %d MHz", normalFreq);
      xSemaphoreGive(modeMutex);
      return;
    }
    isLowPower = false;
  }

  // Otherwise, no change needed
  xSemaphoreGive(modeMutex);
}

void HalPowerManager::startDeepSleep(HalGPIO& gpio, const bool wakeOnHeldButton) const {
  sleepImpl(gpio, /*sleepCommands=*/true, wakeOnHeldButton);
}

void HalPowerManager::startDeepSleepMinimal(HalGPIO& gpio) const {
  sleepImpl(gpio, /*sleepCommands=*/false, /*wakeOnHeldButton=*/false);
}

void HalPowerManager::sleepImpl(HalGPIO& gpio, const bool sleepCommands, const bool wakeOnHeldButton) const {
#if FREEINK_DEVICE_EEGO_A4 || FREEINK_DEVICE_PICCO
  // The frontlight driver and touch share I2C; turn the light off before touch
  // releases the bus. On Picco this also puts the TMA525C to sleep.
  Frontlight.setOn(false);
  gpio.prepareForDeepSleep();
#endif
  // A touch controller on an always-on rail goes to sleep, then the shared I2C
  // bus is released with nothing in flight (no-ops on boards without them). Both
  // before the serial teardown below, so their logs still reach the host. The
  // bus release is GPIO-level only (it frees a line a slave holds LOW), so even
  // a minimal sleep entry runs it, and a minimal entry still re-holds the pins a
  // sleeping touch controller needs.
  if (sleepCommands) {
    gpio.prepareForSleep();
  } else {
    gpio.prepareForMinimalSleep();
  }
  if (!freeink::i2c::releaseSensorBusForSleep()) {
    LOG_ERR("PWR", "I2C SDA still held LOW after bus recovery");
#if SILKSCREEN_BATLOG
    freeink::batterylog::noteFlag(freeink::batterylog::Flag::SdaStuck);
#endif
  }

#ifdef ENABLE_SERIAL_LOG
  // Tear down HWCDC so the host sees a clean disconnect and the peripheral
  // doesn't hold power domains that interfere with USB-powered GPIO wake.
  // logSerial is the raw HWCDC reference; Serial is the MySerialImpl proxy
  // (which doesn't expose end()).
  logSerial.end();
#endif

#if !SOC_PM_SUPPORT_EXT1_WAKEUP
  if (gpio.isXteinkDevice()) {
    // GPIO13 gates the battery MOSFET on both Xteink C3 boards; driving it low
    // is the battery power-off (the SDK wake source still handles USB power).
    // Release any surviving pad hold first: hold_en survives deep sleep via
    // the SDK's deepSleep() (esp_sleep_config_gpio_isolate +
    // gpio_deep_sleep_hold_en), and a held pad silently ignores the drive.
    gpio_hold_dis(XTEINK_C3_GPIO13);
    gpio_set_direction(XTEINK_C3_GPIO13, GPIO_MODE_OUTPUT);
    gpio_set_level(XTEINK_C3_GPIO13, 0);
    gpio_hold_en(XTEINK_C3_GPIO13);
  }
#endif

  // Hold every configured power-latch pin HIGH through deep sleep. These are
  // keep-alive enables (the X4 Pro's master peripheral rail on GPIO1, the
  // Sticky's PWR_HOLD/PWR_LOCK): deepSleep() isolates all pads
  // (esp_sleep_config_gpio_isolate), so a latch without an armed hold loses its
  // output driver and floats — on the X4 Pro the latch drops as soon as
  // external power leaves (serial/pogo adapter unplugged), and the next power-
  // button press cold-boots instead of fast-waking. holdPowerRails() asserted
  // the latches at boot but arms no sleep hold; arm it here instead. Skips
  // XTEINK_C3_GPIO13: it IS power.latch0 on the C3 Xteink boards, where the
  // block above drives it LOW on purpose (battery power-off).
  for (const int8_t pin : {BoardConfig::ACTIVE.power.latch0, BoardConfig::ACTIVE.power.latch1}) {
    if (pin < 0 || static_cast<gpio_num_t>(pin) == XTEINK_C3_GPIO13) continue;
    const auto g = static_cast<gpio_num_t>(pin);
    // Release any surviving pad hold first: a held pad silently ignores the
    // drive below (same trap as the GPIO13 block above).
    gpio_hold_dis(g);
    pinMode(pin, OUTPUT);
    digitalWrite(pin, HIGH);
    gpio_hold_en(g);
  }

  // An enable-gated front light goes dark and releases its LEDC channel first, so
  // the parked-pin holds below take pads no peripheral is driving. No-op on
  // other frontlights and on a light that was never begun (early re-sleep).
  Frontlight.prepareForSleep();

  // Cut the gated peripheral rails (touch/SD/EPD on boards like the Sticky) and
  // hold the enables off through deep sleep — otherwise the GT911 and SD card
  // stay powered all through "off" and drain the battery. No-op on boards with
  // no switched rails (X4/X3). Trade-off: no touch-to-wake; wake is the power
  // button. Must run after display.deepSleep() so the panel controller gets its
  // deep-sleep command while its rail is still up (enterDeepSleep() in main.cpp
  // guarantees that ordering).
  freeink::PowerManager::powerDownRailsForSleep();

#if FREEINK_DEVICE_PAPERMONO
  // Its power button is behind the M5PM1 PMIC rather than an ESP GPIO, so
  // normal GPIO deep sleep would have no wake source. Ask the PMIC to shut the
  // device down; a button click then restarts it through a cold boot.
  if (freeink::m5pm1::requestShutdown()) {
    delay(1000);  // allow the PMIC firmware time to drop power
  }
#endif
#if FREEINK_DEVICE_METALIO_EINK4
  // Deep sleep leaves the board, 4G modem and audio module powered, so cut
  // power instead; the button cold-boots it. Returns only on USB power.
  freeink::metalio::powerOff();
#endif

  if (wakeOnHeldButton) {
    freeink::PowerManager::armPowerButtonWakeup();
    freeink::PowerManager::deepSleep();
  }
  // PowerConfig::releaseWaitMaxMs: the bounded release wait below may last
  // seconds, and nothing from here on needs full speed.
  if (BoardConfig::powerPolicy().releaseWaitMaxMs != 0) setCpuFrequencyMhz(LOW_POWER_FREQ);

  // Waits for the power button to be physically released (so holding it doesn't
  // immediately wake the device again), then arms the wake source and sleeps.
  freeink::PowerManager::deepSleepUntilPowerButton();
}

bool HalPowerManager::lowBatteryCutoffTripped() {
  if (cutoffTripped) return true;
  const uint16_t cutoffMv = BoardConfig::powerPolicy().lowBatteryCutoffMv;
  if (cutoffMv == 0) return false;
  static const BatteryMonitor cutoffBattery;
  const bool radioOn = WiFi.getMode() != WIFI_MODE_NULL;
  uint16_t mv = 0;
  if (!cutoffBattery.isBelowCutoff(nullptr, &mv, radioOn)) return false;
  cutoffTripped = true;
  LOG_ERR("PWR", "Battery at %u mV%s, below the %u mV cutoff; sleeping", mv, radioOn ? " with Wi-Fi on" : "", cutoffMv);
  return true;
}

unsigned long HalPowerManager::awakeCeilingMs(const unsigned long sleepTimeoutMs) {
  const unsigned long profileMs = BoardConfig::powerPolicy().awakeCeilingMin * 60UL * 1000UL;
  if (profileMs == 0) return 0;
  return std::max(profileMs, sleepTimeoutMs);
}

bool HalPowerManager::awakeCeilingPassed(const unsigned long sleepTimeoutMs) const {
  const unsigned long ceilingMs = awakeCeilingMs(sleepTimeoutMs);
  return ceilingMs > 0 && msSinceInput() >= ceilingMs && msSinceClientTraffic() >= ceilingMs && !gpio.isUsbConnected();
}

bool HalPowerManager::awakeInputCapPassed() const {
  const unsigned long capMs = BoardConfig::powerPolicy().awakeInputCapMin * 60UL * 1000UL;
  return capMs > 0 && msSinceInput() >= capMs && !gpio.isUsbConnected();
}

bool HalPowerManager::backgroundNetworkMustStop(const unsigned long sleepTimeoutMs) {
  return lowBatteryCutoffTripped() || awakeCeilingPassed(sleepTimeoutMs) || awakeInputCapPassed();
}

bool HalPowerManager::radioOffWhenIdle() { return BoardConfig::powerPolicy().radioOffWhenIdle; }

bool HalPowerManager::releaseIdleRadio(const char* tag) {
  if (!radioOffWhenIdle() || WiFi.getMode() == WIFI_MODE_NULL) return false;
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  LOG_DBG(tag, "Work done; Wi-Fi off");
  return true;
}

bool HalPowerManager::firmwareUpdateBlockedByBattery() const {
  const uint16_t minMv = BoardConfig::powerPolicy().firmwareUpdateMinMv;
  if (minMv == 0) return false;
  const BatteryMonitor battery;
  bool known = false;
  if (battery.isExternalPowerPresent(&known) && known) return false;  // USB carries the write
  const uint16_t mv = battery.readMillivoltsAveraged(16);
  if (mv < BatteryMonitor::CELL_PRESENT_MIN_MV) return false;  // no cell: USB powers the board
  const uint16_t allowanceMv = WiFi.getMode() != WIFI_MODE_NULL ? BoardConfig::powerPolicy().cutoffRadioAllowanceMv : 0;
  if (mv + allowanceMv >= minMv) return false;
  LOG_ERR("PWR", "Battery at %u mV (+%u mV radio allowance), below the %u mV a firmware write needs", mv, allowanceMv,
          minMv);
  return true;
}

bool HalPowerManager::noChargeableCell() const {
  if (!BoardConfig::powerPolicy().noChargeableCellNotice) return false;
  const unsigned long now = millis();
  if (_noCellPolledMs != 0 && now - _noCellPolledMs < BATTERY_POLL_MS) return _noCellCached;
  _noCellPolledMs = now != 0 ? now : 1;
  // The charger's no-battery blink decides by itself: the charger then holds P+
  // between its recharge threshold and 4.2 V, so P+ cannot show the missing cell.
  bool noCell = BatteryMonitor::chargeStatus() == BoardConfig::ChargeState::NoBattery;
  if (noCell) {
    if (!_noCellCached) LOG_INF("PWR", "Charger's no-battery blink: no chargeable cell");
  } else if (gpio.isUsbConnected()) {
    // Set below CELL_PRESENT_MIN_MV, cleared only at NO_CELL_CLEAR_MV, so a flat
    // cell charging up through the threshold does not flip the label.
    static const BatteryMonitor battery;
    const uint16_t mv = battery.readMillivoltsAveraged(4);
    noCell = mv < (_noCellCached ? NO_CELL_CLEAR_MV : BatteryMonitor::CELL_PRESENT_MIN_MV);
    if (noCell && !_noCellCached) LOG_INF("PWR", "External power, battery %u mV: no chargeable cell", mv);
  }
  _noCellCached = noCell;
  return _noCellCached;
}

#if FREEINK_DEVICE_SILKSCREEN && !SILKSCREEN_BATLOG
namespace {

// What Silkscreen shows for a whole-percent reading: truncated to a multiple of 5, and 100 from 98 up so a full
// cell resting off the charger (about 4.18 V, 98-99%) reads 100 and not 95. Only the shown value; the average, the
// cutoff and the gates keep the raw reading.
constexpr int silkscreenShownPercent(const int percent) {
  return freeink::battery::BatteryDisplayFilter::quantize(percent);
}

constexpr bool silkscreenShownPercentHolds() {
  int previous = 0;
  for (int p = 0; p <= 100; p++) {
    const int shown = silkscreenShownPercent(p);
    const bool pinned = p >= 98;
    if (shown % 5 != 0) return false;
    if (pinned ? shown != 100 : (shown > p || p - shown >= 5)) return false;
    if (shown < previous) return false;
    previous = shown;
  }
  return true;
}

static_assert(silkscreenShownPercent(0) == 0 && silkscreenShownPercent(1) == 0 && silkscreenShownPercent(4) == 0,
              "0-4% shows 0");
static_assert(silkscreenShownPercent(5) == 5 && silkscreenShownPercent(9) == 5, "5-9% shows 5");
static_assert(silkscreenShownPercent(83) == 80 && silkscreenShownPercent(94) == 90, "truncates down");
static_assert(silkscreenShownPercent(95) == 95 && silkscreenShownPercent(97) == 95, "95-97% shows 95");
static_assert(silkscreenShownPercent(98) == 100 && silkscreenShownPercent(99) == 100 &&
                  silkscreenShownPercent(100) == 100,
              "98% and up shows 100");
static_assert(silkscreenShownPercentHolds(), "multiples of 5, truncating, 100 from 98, monotonic");
}  // namespace
#endif

#if FREEINK_DEVICE_PICCO
// ponytail: 30 s poll; the SGM41562 status is I2C-only, so USB unplug and
// charge progress are only seen when the timer fires.
static constexpr uint64_t USB_HOLD_POLL_US = 30ULL * 1000 * 1000;

bool HalPowerManager::holdWhileUsbPowered(HalGPIO& gpio, void (*onStatus)(bool charging, uint16_t percent)) const {
  if (!gpio.isUsbConnected()) return false;
  LOG_DBG("PWR", "USB power present, holding in light sleep");
  const auto powerPin = static_cast<gpio_num_t>(BoardConfig::ACTIVE.input.power);
  // The press that requested sleep may still be down; it must not end the hold.
  while (digitalRead(powerPin) == LOW) delay(10);  // Picco power button is active-low
  const BatteryMonitor battery;
  int lastCharging = -1;
  int lastPercent = -1;
  while (gpio.isUsbConnected()) {
    const bool charging = battery.isCharging();
    const uint16_t percent = getBatteryPercentage();
    if (static_cast<int>(charging) != lastCharging || percent != lastPercent) {
      lastCharging = charging;
      lastPercent = percent;
      onStatus(charging, percent);
    }
    esp_sleep_enable_timer_wakeup(USB_HOLD_POLL_US);
    gpio_wakeup_enable(powerPin, GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();
    esp_light_sleep_start();
    gpio_wakeup_disable(powerPin);
    // gpio_wakeup_disable() keeps the LOW_LEVEL type, and a later pinMode() would
    // enable it as an interrupt that storms while the button is held.
    gpio_set_intr_type(powerPin, GPIO_INTR_DISABLE);
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    if (digitalRead(powerPin) == LOW) return true;
  }
  return false;
}
#endif

#if FREEINK_DEVICE_SILKSCREEN
bool HalPowerManager::updateBatteryDisplay() {
  if (!BoardConfig::isSilkscreen()) return false;
  using Estimator = freeink::battery::BatteryChargeEstimator;
  using Supply = Estimator::Supply;
  const uint16_t previous = batteryDisplay.shown();
  const auto context = [&] {
    Supply supply = Supply::Unknown;
    switch (BatteryMonitor::chargeStatus()) {
      case BoardConfig::ChargeState::Unknown:
        break;
      case BoardConfig::ChargeState::OnBattery:
        supply = Supply::Battery;
        break;
      case BoardConfig::ChargeState::Charging:
        supply = Supply::Charging;
        break;
      case BoardConfig::ChargeState::Complete:
        supply = Supply::Complete;
        break;
      case BoardConfig::ChargeState::ExternalIdle:
        supply = Supply::ExternalIdle;
        break;
      case BoardConfig::ChargeState::NoBattery:
        supply = Supply::NoBattery;
        break;
    }
    bool radioOn = WiFi.getMode() != WIFI_MODE_NULL;
    const auto ble = bleturner::status();
    radioOn = radioOn || ble.running || ble.starting || ble.stopping || ble.scanning || ble.connecting;
    const bool lit = Frontlight.isOn() && Frontlight.brightness() > 0;
    const uint16_t light = lit ? 1 + Frontlight.brightness() + (static_cast<uint16_t>(Frontlight.warmth()) << 7) : 0;
    const bool ready = !lit || (!Frontlight.switchOnPending() && !Frontlight.fault() && !Frontlight.startupBlocked());
    return Estimator::Context{supply, light, radioOn || display.batteryRefreshActive(),
                              display.batteryRefreshRecent(millis(), Estimator::QUIET_MS), ready};
  };
  // RTC time survives normal sleep, so the unplug relaxation is not restarted
  // at every wake. Boot divider settling still uses the new boot's uptime.
  const auto now = [] { return static_cast<uint32_t>(esp_rtc_get_time_us() / 1000); };
  batteryDisplay.observe(now(), context());
#if SILKSCREEN_BATLOG
  if (batteryDisplay.takeCompleteReference()) appendBatteryChargeRow(now(), "complete_ref");
#endif
  if (millis() >= 2500 && batteryDisplay.sampleDue(now())) {
    if (batteryDisplay.canSample()) {
      static const BatteryMonitor battery;
      const auto before = context();
      const uint16_t mv = battery.readMillivoltsAveraged(16);
      const auto after = context();
      // A render or light change can start on the other core during conversion.
      batteryDisplay.observe(now(), after);
      if (before.light == after.light && before.supply == after.supply && batteryDisplay.canSample()) {
        if (batteryDisplay.sample(now(), mv, after)) {
#if SILKSCREEN_BATLOG
          appendBatteryChargeRow(now(), "sample");
          if (previous == Estimator::UNKNOWN && batteryDisplay.shown() != Estimator::UNKNOWN)
            freeink::batterylog::noteUiSeed(batteryDisplay.tenths());
#endif
        }
      } else
        batteryDisplay.skip();
    } else
      batteryDisplay.skip();
  }
  Estimator::Calibration calibration{};
  if (batteryDisplay.takeCalibration(calibration)) {
    Preferences preferences;
    const uint32_t packed = (1UL << 24) | (static_cast<uint32_t>(calibration.count) << 16) | calibration.stepMv;
    const bool opened = preferences.begin("silkbat", false);
    const bool stored = opened && preferences.putUInt("cal", packed) == sizeof(packed);
    if (opened) preferences.end();
    LOG_INF("PWR", "Charge step %u mV (%u edges), NVS %s", calibration.stepMv, calibration.count,
            stored ? "saved" : "failed");
  }
  const auto saved = batteryDisplay.save();
  retainedBatteryDisplay.magic = 0;
  const auto* source = reinterpret_cast<const uint8_t*>(&saved);
  auto* target = reinterpret_cast<volatile uint8_t*>(&retainedBatteryDisplay.state);
  for (size_t i = 0; i < sizeof(saved); ++i) target[i] = source[i];
  std::atomic_thread_fence(std::memory_order_release);
  retainedBatteryDisplay.magic = BATTERY_DISPLAY_MAGIC;
#if SILKSCREEN_BATLOG
  batteryDisplayTenths.store(saved.tenths, std::memory_order_release);
#endif
  batteryDisplayPercent.store(batteryDisplay.shown(), std::memory_order_release);
  return previous != batteryDisplay.shown();
}
#endif

uint16_t HalPowerManager::getBatteryPercentage() const {
  static const BatteryMonitor battery;
  if (BoardConfig::ACTIVE.batteryGauge.gaugeAddr != 0) {
    const unsigned long now = millis();
    if (_batteryLastPollMs != 0 && (now - _batteryLastPollMs) < BATTERY_POLL_MS) {
      return _batteryCachedPercent;
    }

    _batteryLastPollMs = now;
    uint16_t percent = 0;
    if (!battery.readPercentageChecked(percent)) {
      return _batteryCachedPercent;
    }
    _batteryCachedPercent = percent;
    return _batteryCachedPercent;
  }

#if FREEINK_DEVICE_SILKSCREEN
  if (BoardConfig::isSilkscreen()) return batteryDisplayPercent.load(std::memory_order_acquire);
#endif
  // Preserve the other ADC boards' existing per-call smoothing and rounding.
  const int sampleTenths = battery.readPercentage() * 10;
  if (_batteryCachedPercent == 0) {
    _batteryCachedPercent = sampleTenths;
  } else {
    _batteryCachedPercent = (_batteryCachedPercent * 9 + sampleTenths) / 10;
  }
  return _batteryCachedPercent / 10;
}

HalPowerManager::Lock::Lock() {
  xSemaphoreTake(powerManager.modeMutex, portMAX_DELAY);
  // Current limitation: only one lock at a time
  if (powerManager.currentLockMode != None) {
    LOG_ERR("PWR", "Lock already held, ignore");
    valid = false;
  } else {
    powerManager.currentLockMode = NormalSpeed;
    valid = true;
  }
  xSemaphoreGive(powerManager.modeMutex);
  if (valid) {
    // Immediately restore normal CPU frequency if currently in low-power mode
    powerManager.setPowerSaving(false);
  }
}

HalPowerManager::Lock::~Lock() {
  xSemaphoreTake(powerManager.modeMutex, portMAX_DELAY);
  if (valid) {
    powerManager.currentLockMode = None;
  }
  xSemaphoreGive(powerManager.modeMutex);
}

#if FREEINK_DEVICE_SILKSCREEN && SILKSCREEN_BATLOG
namespace {
// Main-loop only. One bounded batch, no file kept open between samples.
char chargeLogBuffer[1024];
size_t chargeLogUsed = 0;
constexpr char CHARGE_LOG_PATH[] = "/silkscreen-battery/charge.csv";
constexpr char CHARGE_LOG_HEADER[] =
    "rtc_ms,uptime_ms,event,raw_mv,ladder,phase,step_mv,light_mv,comp_mv,tenths,shown,flags,cal_count,reference_ms,"
    "reference_mv\n";
}  // namespace

void HalPowerManager::flushBatteryChargeLog() {
  if (!BoardConfig::isSilkscreen() || !chargeLogUsed) return;
  if (Storage.ready() && Storage.ensureDirectoryExists("/silkscreen-battery")) {
    HalFile file = Storage.open(CHARGE_LOG_PATH, O_WRONLY | O_CREAT | O_APPEND);
    if (file) {
      // Bound diagnostic storage to 32 MiB; a full charge trace is much smaller.
      bool ok = file.fileSize() < 32UL * 1024 * 1024;
      if (ok && file.fileSize() == 0)
        ok = file.write(CHARGE_LOG_HEADER, sizeof(CHARGE_LOG_HEADER) - 1) == sizeof(CHARGE_LOG_HEADER) - 1;
      if (ok) ok = file.write(chargeLogBuffer, chargeLogUsed) == chargeLogUsed;
      const bool closed = file.close();
      if (!ok || !closed) LOG_ERR("BATLOG", "Charge sample log write failed or limit reached");
    } else
      LOG_ERR("BATLOG", "Charge sample log open failed");
  }
  // Best effort: never retain stale batches indefinitely or retry in a loop.
  chargeLogUsed = 0;
}

void HalPowerManager::appendBatteryChargeRow(uint32_t now, const char* event) {
  char row[192];
  const auto saved = batteryDisplay.save();
  const int length =
      snprintf(row, sizeof(row), "%lu,%lu,%s,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%lu,%u\n", static_cast<unsigned long>(now),
               static_cast<unsigned long>(millis()), event, batteryDisplay.rawMv(),
               static_cast<unsigned>(BatteryMonitor::chargeStatus()), static_cast<unsigned>(batteryDisplay.phase()),
               batteryDisplay.stepMv(), batteryDisplay.lightCorrectionMv(), batteryDisplay.compensatedMv(),
               saved.tenths, saved.shown, batteryDisplay.flags(), batteryDisplay.calibration().count,
               static_cast<unsigned long>(batteryDisplay.completeReferenceAt()), batteryDisplay.completeReferenceMv());
  if (length <= 0 || static_cast<size_t>(length) >= sizeof(row)) return;
  if (chargeLogUsed + length > sizeof(chargeLogBuffer)) flushBatteryChargeLog();
  memcpy(chargeLogBuffer + chargeLogUsed, row, length);
  chargeLogUsed += length;
}
#endif

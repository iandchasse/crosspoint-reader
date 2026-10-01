#if CROSSPOINT_TEST_SUITE
#include "TestSuite.h"

#include <Arduino.h>
#include <BoardConfig.h>
#include <HalGPIO.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <esp_system.h>
#if ARDUINO_USB_MODE && ARDUINO_USB_CDC_ON_BOOT
#include <HWCDC.h>
#endif

#include "BoardOptions.h"
#include "CrossPointSettings.h"
#include "SleepRecord.h"
#include "TestResultLog.h"
#include "network/FirmwareBoardTag.h"

// The tests read the SDK's bring-up records (freeink-sdk README, "Bring-up
// diagnostics"), which exist only with the flag.
static_assert(FREEINK_DIAGNOSTICS, "[env:silkscreen-test] must build the SDK with -DFREEINK_DIAGNOSTICS=1");

namespace testsuite {
namespace {

// In RAM only; the reader's settings.json keeps what the reader saved.
void pinSettings() {
  auto& s = SETTINGS;
  // Display state the loop and the render task re-apply every pass: tests that
  // exercise it change these fields, never the renderer or the HAL directly.
  s.screenInverted = 0;
  s.fadingFix = 0;
  s.orientation = CrossPointSettings::PORTRAIT;
  // A power-button click does nothing and a hold longer than 400 ms sleeps, so
  // every sleep entry leaves the hold-to-wake rule.
  s.shortPwrBtn = CrossPointSettings::IGNORE;
  // The light stays off until a front-light test has the load confirmed.
  s.frontlightOn = 0;
  s.frontlightRestoreOnWake = 0;
  // The keys in their default roles, so the screens and the manual agree.
  s.frontButtonLayout = CrossPointSettings::BACK_CONFIRM_LEFT_RIGHT;
  s.frontButtonBack = CrossPointSettings::FRONT_HW_BACK;
  s.frontButtonConfirm = CrossPointSettings::FRONT_HW_CONFIRM;
  s.frontButtonLeft = CrossPointSettings::FRONT_HW_LEFT;
  s.frontButtonRight = CrossPointSettings::FRONT_HW_RIGHT;
  s.frontButtonFollowOrientation = 0;
  s.sideButtonLayout = CrossPointSettings::PREV_NEXT;
  s.language = static_cast<uint8_t>(Language::EN);
  s.uiTheme = CrossPointSettings::LYRA;
  // No reader font from the card: nothing here renders a book.
  s.sdFontFamilyName[0] = '\0';
  if (s.sleepTimeoutMinutes >= CrossPointSettings::SLEEP_TIMEOUT_NEVER_MINUTES) s.sleepTimeoutMinutes = 10;
}

}  // namespace

void beginAfterSettings() {
  pinSettings();
  // P5's "no input after a wake": this boot's 180 s sleep is the one it checks,
  // so a shorter saved timeout must not fire first (in RAM, this boot only).
  constexpr uint8_t WAKE_NO_INPUT_MIN_TIMEOUT_MIN = 5;
  SleepRecord woke{};
  if (wokeFromSleep(woke) && woke.path == SleepPath::WakeNoInput && woke.stage == 1 &&
      SETTINGS.sleepTimeoutMinutes < WAKE_NO_INPUT_MIN_TIMEOUT_MIN) {
    LOG_INF("TEST", "P5 no-input wake: sleep timeout %u -> %u min for this boot", SETTINGS.sleepTimeoutMinutes,
            WAKE_NO_INPUT_MIN_TIMEOUT_MIN);
    SETTINGS.sleepTimeoutMinutes = WAKE_NO_INPUT_MIN_TIMEOUT_MIN;
  }
  LOG_INF("TEST", "Silkscreen hardware test firmware %s, board tag %.*s, reset reason %d, SD %s", CROSSPOINT_VERSION,
          static_cast<int>(board_tag::boardNameLen()), board_tag::boardName(), static_cast<int>(esp_reset_reason()),
          Storage.ready() ? "mounted" : "not mounted");
  removeStaleScratchFiles();
  options::load();
}

bool usbHostAttached() {
#if ARDUINO_USB_MODE && ARDUINO_USB_CDC_ON_BOOT
  // isPlugged() follows the host's start-of-frame packets and can drop out briefly.
  constexpr unsigned long USB_HOST_HOLD_MS = 2000;
  static bool seen = false;
  static unsigned long lastSeenMs = 0;
  static bool reported = false;
  const unsigned long now = millis();
  if (HWCDC::isPlugged()) {
    seen = true;
    lastSeenMs = now;
  }
  // The S3's USB Serial/JTAG enumerates without VBUS (its D+ pull-up runs from
  // the board's 3V3, and there is no VBUS sense, HW-11), so a VBUS-less data link
  // on the cell sees frames too: USB power from the USB_STAT ladder is required
  // as well (its decoded record, no ADC read).
  const bool attached = seen && now - lastSeenMs < USB_HOST_HOLD_MS && gpio.isUsbConnected();
  if (attached != reported) {
    reported = attached;
    LOG_INF("TEST", "USB host %s",
            attached ? "on USB power: auto-sleep held off" : "gone or no USB power: auto-sleep back on");
  }
  return attached;
#else
  return false;
#endif
}

}  // namespace testsuite
#endif

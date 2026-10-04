#include <Arduino.h>
#include <BatteryMonitor.h>
#include <BoardConfig.h>
#include <Epub.h>
#include <FontCacheManager.h>
#include <FontDecompressor.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalDisplay.h>
#include <HalFrontlight.h>
#include <HalGPIO.h>
#include <HalMemory.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <HalSystem.h>
#include <HalTiltSensor.h>
#include <I18n.h>
#include <Logging.h>
#include <PowerManager.h>
#include <SPI.h>
#include <TrustedTime.h>
#include <VectorFontSupport.h>
#include <WiFi.h>
#include <XteinkDetect.h>
#include <builtinFonts/all.h>
#include <esp_system.h>

#include <cstring>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "KOReaderCredentialStore.h"
#include "MappedInputManager.h"
#include "OpdsServerStore.h"
#include "RecentBooksStore.h"
#include "SdCardFontSystem.h"
#include "WifiCredentialStore.h"
#include "activities/Activity.h"
#include "activities/ActivityManager.h"
#include "activities/settings/SdFirmwareUpdateActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "platform/UsbSerialJtagHandoff.h"
#include "util/ButtonNavigator.h"
#include "util/PluginEvents.h"
#include "util/ScreenshotUtil.h"
#include "util/Timezones.h"
#if CROSSPOINT_TEST_SUITE
#include "activities/testsuite/TestSuite.h"
#endif
#if SILKSCREEN_BATLOG
#include <BatteryLog.h>

#include "util/BatteryLogWriter.h"
#endif

#if CROSSPOINT_VECTOR_FONTS
// Rendering (incl. FreeType TTF rasterization) runs on the Arduino loop task.
// The default 8 KB stack overflows inside FreeType's FT_Open_Face / variable-font
// parsing. This runtime override applies even with the prebuilt (dio_opi) core,
// where CONFIG_ARDUINO_LOOP_STACK_SIZE from sdkconfig is baked in and ignored.
// Vector-font boards only: without TTF the stock loop stack has always sufficed,
// and non-PSRAM boards need the 16KB back in DRAM.
SET_LOOP_TASK_STACK_SIZE(24 * 1024)
#endif

GfxRenderer renderer(display);
MappedInputManager mappedInputManager(gpio, renderer);
ActivityManager activityManager(renderer, mappedInputManager);
FontDecompressor fontDecompressor;
SdCardFontSystem sdFontSystem;
FontCacheManager fontCacheManager(renderer.getFontMap(), renderer.getSdCardFonts(), renderer.getTtfFonts());
static unsigned long allowSleepAt = 0;
static unsigned long lastPowerClickAt = 0;

namespace {
constexpr unsigned long POWER_DOUBLE_CLICK_MS = 500;
constexpr unsigned long POWER_CLICK_MAX_HOLD_MS = 300;
}  // namespace

// A wake hold must never become an in-app power-button action.  Boot may continue
// while the button is held; swallow the one release that ends that wake gesture.
static bool wakePowerReleasePending = false;

// Fonts
EpdFont notoserif14RegularFont(&notoserif_14_regular);
EpdFont notoserif14BoldFont(&notoserif_14_bold);
EpdFont notoserif14ItalicFont(&notoserif_14_italic);
EpdFont notoserif14BoldItalicFont(&notoserif_14_bolditalic);
EpdFontFamily notoserif14FontFamily(&notoserif14RegularFont, &notoserif14BoldFont, &notoserif14ItalicFont,
                                    &notoserif14BoldItalicFont);
#ifndef OMIT_FONTS
EpdFont notoserif12RegularFont(&notoserif_12_regular);
EpdFont notoserif12BoldFont(&notoserif_12_bold);
EpdFont notoserif12ItalicFont(&notoserif_12_italic);
EpdFont notoserif12BoldItalicFont(&notoserif_12_bolditalic);
EpdFontFamily notoserif12FontFamily(&notoserif12RegularFont, &notoserif12BoldFont, &notoserif12ItalicFont,
                                    &notoserif12BoldItalicFont);
EpdFont notoserif16RegularFont(&notoserif_16_regular);
EpdFont notoserif16BoldFont(&notoserif_16_bold);
EpdFont notoserif16ItalicFont(&notoserif_16_italic);
EpdFont notoserif16BoldItalicFont(&notoserif_16_bolditalic);
EpdFontFamily notoserif16FontFamily(&notoserif16RegularFont, &notoserif16BoldFont, &notoserif16ItalicFont,
                                    &notoserif16BoldItalicFont);
EpdFont notoserif18RegularFont(&notoserif_18_regular);
EpdFont notoserif18BoldFont(&notoserif_18_bold);
EpdFont notoserif18ItalicFont(&notoserif_18_italic);
EpdFont notoserif18BoldItalicFont(&notoserif_18_bolditalic);
EpdFontFamily notoserif18FontFamily(&notoserif18RegularFont, &notoserif18BoldFont, &notoserif18ItalicFont,
                                    &notoserif18BoldItalicFont);

EpdFont notosans12RegularFont(&notosans_12_regular);
EpdFont notosans12BoldFont(&notosans_12_bold);
EpdFont notosans12ItalicFont(&notosans_12_italic);
EpdFont notosans12BoldItalicFont(&notosans_12_bolditalic);
EpdFontFamily notosans12FontFamily(&notosans12RegularFont, &notosans12BoldFont, &notosans12ItalicFont,
                                   &notosans12BoldItalicFont);
EpdFont notosans14RegularFont(&notosans_14_regular);
EpdFont notosans14BoldFont(&notosans_14_bold);
EpdFont notosans14ItalicFont(&notosans_14_italic);
EpdFont notosans14BoldItalicFont(&notosans_14_bolditalic);
EpdFontFamily notosans14FontFamily(&notosans14RegularFont, &notosans14BoldFont, &notosans14ItalicFont,
                                   &notosans14BoldItalicFont);
EpdFont notosans16RegularFont(&notosans_16_regular);
EpdFont notosans16BoldFont(&notosans_16_bold);
EpdFont notosans16ItalicFont(&notosans_16_italic);
EpdFont notosans16BoldItalicFont(&notosans_16_bolditalic);
EpdFontFamily notosans16FontFamily(&notosans16RegularFont, &notosans16BoldFont, &notosans16ItalicFont,
                                   &notosans16BoldItalicFont);
EpdFont notosans18RegularFont(&notosans_18_regular);
EpdFont notosans18BoldFont(&notosans_18_bold);
EpdFont notosans18ItalicFont(&notosans_18_italic);
EpdFont notosans18BoldItalicFont(&notosans_18_bolditalic);
EpdFontFamily notosans18FontFamily(&notosans18RegularFont, &notosans18BoldFont, &notosans18ItalicFont,
                                   &notosans18BoldItalicFont);

#endif  // OMIT_FONTS

EpdFont smallFont(&notosans_8_regular);
EpdFontFamily smallFontFamily(&smallFont);

EpdFont ui10RegularFont(&ubuntu_10_regular);
EpdFont ui10BoldFont(&ubuntu_10_bold);
EpdFontFamily ui10FontFamily(&ui10RegularFont, &ui10BoldFont);

EpdFont ui12RegularFont(&ubuntu_12_regular);
EpdFont ui12BoldFont(&ubuntu_12_bold);
EpdFontFamily ui12FontFamily(&ui12RegularFont, &ui12BoldFont);

// Definitions for SilentRestart.h. RTC_NOINIT survives ESP.restart() but not power loss.
RTC_NOINIT_ATTR uint32_t silentRebootMagic;
RTC_NOINIT_ATTR uint32_t silentRebootTarget;
RTC_NOINIT_ATTR uint32_t silentRebootPayload;
constexpr uint32_t SILENT_REBOOT_MAGIC = 0xC1EAB007;
constexpr uint32_t SILENT_REBOOT_TARGET_HOME = 0;
constexpr uint32_t SILENT_REBOOT_TARGET_READER = 1;
constexpr uint32_t SILENT_REBOOT_TARGET_SETTINGS = 2;
constexpr uint32_t SILENT_REBOOT_TARGET_JOIN_NETWORK = 3;
constexpr uint32_t SILENT_REBOOT_TARGET_MAX = SILENT_REBOOT_TARGET_JOIN_NETWORK;
constexpr uint32_t SILENT_REBOOT_LIGHT_ON = 1U << 0;

// PowerConfig::earlyWakeDecision: Short Power Button Press lives on SD, which the
// early sleep-or-boot decision must not wait for. Every sleep entry that had the
// settings loaded leaves its wake rule here (RTC memory survives deep sleep);
// any other value, such as a power-on's garbage, means unknown.
RTC_NOINIT_ATTR uint32_t sleepWakePolicy;
constexpr uint32_t WAKE_POLICY_HOLD = 0x57414B48;   // "WAKH": the button must still be held at verification
constexpr uint32_t WAKE_POLICY_CLICK = 0x57414B43;  // "WAKC": a click wakes (Short Power Button Press = Sleep)
static bool settingsLoaded = false;

// PowerConfig::abnormalResetLimit counts resets in a row; a session that stays up
// this long ends the run.
constexpr unsigned long HEALTHY_UPTIME_MS = 60UL * 1000UL;

// PowerConfig::wakeNoInputTimeoutS: armed by a power-button wake that boots,
// disarmed by the first key, touch or tilt input after it.
static bool wakeNoInputArmed = false;
// PowerConfig::storageErrorSleepS: setup() ended on the SD-card error screen.
static bool storageErrorScreen = false;

// FrontlightConfig::monitorAdc: a tripped light is switched off and the saved
// preference with it, so no wake, restart or power cycle turns the boost on
// into the fault again; the user switching it on is the retry. A switch-on still
// deciding its load (FrontlightConfig::loadProbeWindowMs) is not a trip: its
// result is settled first, and a "no light" one is the HAL's notice instead.
// Returns true when it switched a tripped light off. From the loop task only (the
// trip itself happens on the esp_timer task).
static bool handleFrontlightTrip() {
  if (!Frontlight.present()) return false;
  Frontlight.update();
  if (!Frontlight.fault() || Frontlight.switchOnPending()) return false;
  Frontlight.setOn(false);
  LOG_ERR("LIGHT", "Front light tripped its LED over-voltage monitor; switched off until switched on again");
  if (SETTINGS.frontlightOn != 0) {
    SETTINGS.frontlightOn = 0;
    SETTINGS.saveToFile();
  }
  return true;
}

#if FREEINK_CAP_FRONTLIGHT_PROBE && !CROSSPOINT_TEST_SUITE
// "No front light detected. Wait about 10 s before connecting a light." The
// boost has just run with no load, or a trip took it off near its OVP level
// (HARDWARE.md 13.1), and plugging a light in before C9 has fallen discharges it
// through one string.
static void showFrontlightNotFound() {
  RenderLock lock;
  GUI.drawWrappedPopup(renderer, tr(STR_FRONTLIGHT_NOT_DETECTED), 3);
}

// FrontlightConfig::loadProbeWindowMs: the one message, with the wait the boost's
// charged output needs before a light is plugged in, for every way the boost ran
// with nothing to light or went off near its OVP level (an over-voltage trip
// means no light connected):
// - a switch-on the user asked for that found no light (HalFrontlight has already
//   left it off): the light is saved as off, as it now is. The light panel shows
//   the message itself while it is open (Activity::showsFrontlightNotice()).
// - a restore at boot that found none (Restore Light on Wake, or a silent
//   restart's replay): the saved "on" stays (a cold boot clears the SDK's record,
//   so a restore may try once again).
// - a trip in use, or a switch-on whose second string the rail blocked
//   (handleFrontlightTrip() saves the light as off).
// The light controls stay where they are, so switching it on again is the retry.
// The popup waits for the second loop pass, once the first pass has started the
// first screen's render, which it must not go under. The screen is first drawn
// again from the settled state, so a light checkbox drawn while the switch-on
// was still deciding (or before a trip) does not stay checked beside the popup.
// From the loop task only.
static void handleFrontlightNotices() {
  static bool firstPassDone = false;
  static bool noticeDue = false;
  if (handleFrontlightTrip()) noticeDue = true;
  if (Frontlight.takeRestoreNoLightNotice()) noticeDue = true;
  if (!activityManager.showsFrontlightNotice() && Frontlight.takeNoLightNotice()) {
    if (SETTINGS.frontlightOn != 0) {
      SETTINGS.frontlightOn = 0;
      SETTINGS.saveToFile();
    }
    noticeDue = true;
  }
  if (noticeDue && firstPassDone) {
    noticeDue = false;
    activityManager.requestUpdateAndWait();
    showFrontlightNotFound();
  }
  firstPassDone = true;
}

// FrontlightConfig::loadProbeWindowMs: a single-colour light. The SDK records the
// missing string until a cold boot; saving the warmth on the string that is
// there keeps a cold boot (and Restore Light on Wake) from selecting the missing
// one again. Only a warmth that selects the missing string changes, so the
// user's own value otherwise stays. From the loop task only.
static void keepWarmthOnPresentString() {
  uint8_t warmth = SETTINGS.frontlightWarmth;
  if (Frontlight.stringMissing(true) && warmth >= 50) warmth = 0;
  if (Frontlight.stringMissing(false) && warmth < 50) warmth = 100;
  if (warmth == SETTINGS.frontlightWarmth) return;
  LOG_INF("LIGHT", "Warmth saved as %u%%: the %s string is missing", warmth, warmth == 0 ? "warm" : "cool");
  SETTINGS.frontlightWarmth = warmth;
  SETTINGS.saveToFile();
}
#endif

// How the device is coming back to life, resolved once at boot. Both resume
// flows suppress the splash and leave the panel holding its pre-boot frame; a
// plain boot shows the splash. See setup() for the resolution.
enum class BootResume : uint8_t {
  Splash,          // cold boot, flash, panic, or plain reboot
  Silent,          // heap-defrag ESP.restart() (RTC flag; lost on power loss)
  SplashlessWake,  // wake from deep sleep with the splash suppressed by the SD flag
};

// Latched true once enterDeepSleep() commits to sleeping, before it tears down
// the current activity. WiFi activities call silentRestart() in onExit() to
// clear heap fragmentation on the way out, but deep sleep is a full chip reset
// on wake and already clears the heap, so rebooting here would just power the
// device back up against the user's sleep gesture. Never cleared:
// startDeepSleep() does not return, so a set latch only ends at the wakeup reset.
static bool deepSleepInProgress = false;

// A silent restart is internal maintenance, so the light must come back exactly
// as the user left it. SETTINGS.frontlightOn is the saved preference and
// legitimately diverges from the live state (a wake with Restore Light on Wake
// off leaves the light off while the saved "was on" preference is kept), so
// carry the live state across the reboot instead of re-deriving it from
// settings. Cleared with the magic in setup().
static void armSilentReboot(const uint32_t target) {
#if SILKSCREEN_BATLOG
  freeink::batterylog::carry();
#endif
  silentRebootTarget = target;
  silentRebootPayload = Frontlight.isOn() ? SILENT_REBOOT_LIGHT_ON : 0;
  silentRebootMagic = SILENT_REBOOT_MAGIC;
}

// Returns instead of rebooting when sleep supersedes the reboot; callers keep
// running in that case.
static void silentRestartTo(const uint32_t target, const char* targetName) {
  if (deepSleepInProgress) return;  // sleeping supersedes the heap-defrag reboot
  armSilentReboot(target);
  LOG_DBG("MAIN", "Silent restart (target=%s)", targetName);
  // E-ink retains the previous frame until the target's first paint lands
  // (~2-3s). Without an overlay, users don't see the reboot and fire input
  // through to the new activity. On Home, Select on the default
  // selectorIndex=0 opens the most-recent book, looking like a trampoline back
  // to the reader they just exited.
  GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
  delay(50);
  ESP.restart();
}

void silentRestart() { silentRestartTo(SILENT_REBOOT_TARGET_HOME, "home"); }

void silentRestartToReader() { silentRestartTo(SILENT_REBOOT_TARGET_READER, "reader"); }

void silentRestartToSettings() { silentRestartTo(SILENT_REBOOT_TARGET_SETTINGS, "settings"); }

void silentRestartToJoinNetwork() {
  if (deepSleepInProgress) return;
#if FREEINK_CAP_TOUCH
  // A software reset would cycle touch/frontlight rails; those boards proceed
  // into Join Network without the fresh-heap reboot (return, don't stop WiFi —
  // this runs on the way *in*, unlike the exit-time silentRestart()).
  if (BoardConfig::hasTouch()) return;
#endif
  armSilentReboot(SILENT_REBOOT_TARGET_JOIN_NETWORK);
  LOG_DBG("MAIN", "Silent restart (target=join-network)");
  GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
  delay(50);
  ESP.restart();
}

void restartToHomeAfterStorageHandoff() {
  if (deepSleepInProgress) return;  // sleeping supersedes the storage handoff reboot
  armSilentReboot(SILENT_REBOOT_TARGET_HOME);
  LOG_DBG("MAIN", "Restart after storage handoff (target=home)");
  GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
  delay(50);
  handoffUsbOtgToSerialJtag();
  ESP.restart();
}

void toggleFrontlight() {
  if (!Frontlight.present()) return;
  const bool lightOn = !Frontlight.isOn();
  Frontlight.setOn(lightOn);
  SETTINGS.frontlightOn = lightOn ? 1 : 0;
  SETTINGS.saveToFile();
  LOG_INF("LIGHT", "Frontlight toggled %s", lightOn ? "on" : "off");
}

bool handlePowerDoubleClickLight() {
  if (!SETTINGS.powerDoubleClickLight() || !gpio.wasReleased(HalGPIO::BTN_POWER)) {
    return false;
  }

  const unsigned long now = millis();
  if (gpio.getPowerButtonHeldTime() > POWER_CLICK_MAX_HOLD_MS) {
    lastPowerClickAt = 0;
    return false;
  }

  if (lastPowerClickAt == 0 || now - lastPowerClickAt > POWER_DOUBLE_CLICK_MS) {
    lastPowerClickAt = now;
    return false;
  }

  lastPowerClickAt = 0;
  toggleFrontlight();
  return true;
}

constexpr char SLEEP_FRAME_FILE[] = "/.crosspoint/sleep_frame.bin";

#if !CROSSPOINT_TEST_SUITE
static void saveSleepFrameBuffer() {
  HalFile file;
  if (!Storage.openFileForWrite("SLP", SLEEP_FRAME_FILE, file)) return;
  file.write(renderer.getFrameBuffer(), renderer.getBufferSize());
  file.close();
}
#endif

static bool loadSleepFrameBuffer() {
  HalFile file;
  if (!Storage.openFileForRead("SLP", SLEEP_FRAME_FILE, file)) return false;
  const size_t bufferSize = display.getBufferSize();
  const size_t bytesRead = file.read(display.getFrameBuffer(), bufferSize);
  file.close();
  if (bytesRead != bufferSize) {
    Storage.remove(SLEEP_FRAME_FILE);
    return false;
  }
  Storage.remove(SLEEP_FRAME_FILE);
  return true;
}

#if !CROSSPOINT_TEST_SUITE
// Plugin-event delivery on the way into deep sleep. sleep.enter is delivered
// now — over the live connection, or by bringing WiFi up when a plugin
// subscribes (e.g. fetching a fresh /sleep.bmp so THIS sleep shows it — the
// drain runs before goToSleep() renders the sleep screen). The connect path
// is bounded (join deadline + drain event budget), skipped on low battery,
// and sleep is never blocked on the network: a failed join or delivery just
// sleeps with the previous image and the queued events retry on the next
// drain (at-least-once). The caller's WiFi shutdown tears the radio down
// either way. Deferrable events already queued (reader.exit) ride along in
// the same drain. An unattended sleep (a wake that got no input,
// PowerConfig::wakeNoInputTimeoutS) emits and delivers nothing.
static void deliverSleepPluginEvents(const bool unattended) {
  // Activity-owned state must be queued before sleep.enter and before this
  // same-sleep drain. The hook is idempotent with ordinary activity teardown.
  activityManager.prepareForSleep();

  // Nobody is at the device and nothing changed since the last sleep entry: no
  // stale sleep.enter fills the outbox, and no join or drain runs; events
  // already queued wait for the next drain.
  if (unattended) {
    LOG_DBG("MAIN", "Unattended wake sleep; plugin delivery deferred");
    return;
  }

  // Sleeping straight out of a book is the common flow, but the reader's own
  // reader.exit only fires later, inside goToSleep() — after this drain. Carry
  // the book and progress on sleep.enter itself so a sync handler bound to it
  // pushes current progress on THIS connection, not the next one.
  pluginevents::Var vars[2];
  size_t varCount = 0;
  char percent[8];
  const ScreenshotInfo info = activityManager.getScreenshotInfo();
  if (info.readerType != ScreenshotInfo::ReaderType::None && !APP_STATE.openEpubPath.empty()) {
    snprintf(percent, sizeof(percent), "%d", info.progressPercent);
    vars[varCount++] = {"book", APP_STATE.openEpubPath.c_str()};
    vars[varCount++] = {"percent", percent};
  }
  pluginevents::emit(pluginevents::Event::SleepEnter, vars, varCount);
  // At the low-battery cutoff, or once the awake ceiling or the input cap has
  // passed (PowerConfig::lowBatteryCutoffMv, awakeCeilingMin, awakeInputCapMin:
  // nobody is at the device), no network work runs on the way down; the events
  // stay queued for the next drain. The drain stops on the same check.
  if (powerManager.backgroundNetworkMustStop(SETTINGS.getSleepTimeoutMs())) return;
  if (WiFi.status() == WL_CONNECTED) {
    pluginevents::drain(&renderer);
    return;
  }
  // Any connect-flagged queued event justifies the join, not only
  // sleep.enter: reader.session is queued while reading and delivered on this
  // same sleep, and a progress-sync plugin usually subscribes to it alone.
  if (!pluginevents::wantsConnectAny()) return;
  if (powerManager.getBatteryPercentage() < 20) return;
  const auto cred = WIFI_STORE.findCredential(WIFI_STORE.getLastConnectedSsid());
  if (!cred) return;

  GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
  WiFi.mode(WIFI_STA);
  WiFi.begin(cred->ssid.c_str(), cred->password.c_str());
  const unsigned long joinDeadline = millis() + 10000;
  while (WiFi.status() != WL_CONNECTED && millis() < joinDeadline) {
    delay(100);
  }
  if (WiFi.status() == WL_CONNECTED) {
    trustedtime::startSync();  // snap the clock floor while the network is up
    pluginevents::drain(&renderer);
  } else {
    LOG_DBG("MAIN", "Sleep-event WiFi join timed out; deferring delivery");
  }
}
#endif

#if SILKSCREEN_BATLOG
// The battery log's two reads of live state. Both only read: nothing here drives the light, a pin, the radio or the
// card.
//
// What the sleep entry knows at the trigger, staged in RAM for the SDK's tail at the head of
// PowerManager::deepSleep(). The path is the default one; a tagPath() call from the call site overrides it.
static void stageBatteryLogEntry(const bool fromTimeout, const bool unattended) {
  using freeink::batterylog::Path;
  freeink::batterylog::Intent intent;
  intent.path = unattended ? Path::Unattended : (fromTimeout ? Path::Auto : Path::Manual);
  intent.usb = BatteryLogWriter::usbPresent();
  intent.chg = BatteryLogWriter::chargeState();
  intent.uiTenths = static_cast<uint16_t>(powerManager.batteryCachedTenths());
  intent.idleMs = powerManager.msSinceInput();
  intent.lightOn = Frontlight.isOn();
  intent.radioOn = WiFi.getMode() != WIFI_MODE_NULL;
  intent.settingsLoaded = settingsLoaded;  // not on the SD-error boot: SETTINGS then holds struct defaults
  if (settingsLoaded) {
    // cfg: bit 0 Restore Light on Wake, 1 the click-wake rule, 2 touch fitted, 3 front light present, 4 Fading Fix.
    uint8_t cfg = 0;
    if (SETTINGS.frontlightRestoreOnWake != 0) cfg |= 1U << 0;
    if (SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::SLEEP) cfg |= 1U << 1;
    if (BoardConfig::hasTouch()) cfg |= 1U << 2;
    if (Frontlight.present()) cfg |= 1U << 3;
    if (SETTINGS.fadingFix != 0) cfg |= 1U << 4;
    intent.cfg = cfg;
    const unsigned long timeoutMin = SETTINGS.getSleepTimeoutMs() / 60000UL;  // 0 for Never
    intent.tmoMin = static_cast<uint8_t>(timeoutMin > 255UL ? 255UL : timeoutMin);
  }
  freeink::batterylog::beginEntry(intent);
}

// One loop pass of the session ledger.
static void tickBatteryLog() {
  freeink::batterylog::TickIn in;
  in.lit = Frontlight.isOn();
  in.litPct = Frontlight.brightness();
  in.radioOn = WiFi.getMode() != WIFI_MODE_NULL;
  in.fullClock = powerManager.atFullClock();
  in.usb = gpio.isUsbConnected();
  freeink::batterylog::tick(in);
}
#endif

// Enter deep sleep mode. unattended: a power-button wake that got no input
// (PowerConfig::wakeNoInputTimeoutS) is going back to sleep.
void enterDeepSleep(bool fromTimeout = false, bool unattended = false) {
#if SILKSCREEN_BATLOG
  stageBatteryLogEntry(fromTimeout, unattended);
#endif
  HalPowerManager::Lock powerLock;  // Ensure we are at normal CPU frequency for sleep preparation
  handleFrontlightTrip();
  // An enable-gated front light goes dark now, not in sleepImpl(): the plugin
  // delivery below may join Wi-Fi and run handlers, and nothing on the way down
  // needs the light. sleepImpl()'s call then returns early; the saved light
  // preference (Restore Light on Wake) is unchanged. No-op on other lights.
  Frontlight.prepareForSleep();
  APP_STATE.lastSleepFromReader = activityManager.isReaderActivity();

#if CROSSPOINT_TEST_SUITE  // no plugins or loan clock in the hardware test firmware
  (void)unattended;
#else
  // Sleep may end in a power-off (battery death, latch); persist the clock
  // floor now so a later cold boot resumes from it.
  trustedtime::note();

  deliverSleepPluginEvents(unattended);
#endif

  const bool isQuickResumeSleep =
      SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::QUICK_RESUME ||
      (fromTimeout &&
       SETTINGS.quickResumeSleepScreen == CrossPointSettings::QUICK_RESUME_SLEEP_SCREEN::QUICK_RESUME_AFTER_TIMEOUT);
  // Every sleep mode leaves a complete retained frame on the e-ink panel. Keep
  // it visible until the first useful reader or home paint replaces it.
  APP_STATE.showBootScreen = false;

  APP_STATE.saveToFile();

  // Commit to sleeping before goToSleep() runs the outgoing activity's onExit():
  // a WiFi activity would otherwise silentRestart() here and reboot instead.
  deepSleepInProgress = true;
  activityManager.goToSleep(fromTimeout);

#if CROSSPOINT_TEST_SUITE  // the reader's Quick Resume frame is left as the reader wrote it
  (void)isQuickResumeSleep;
#else
  if (isQuickResumeSleep) {
    saveSleepFrameBuffer();
  } else if (Storage.exists(SLEEP_FRAME_FILE)) {
    // A stale Quick Resume frame must not replace the selected sleep screen during wake.
    Storage.remove(SLEEP_FRAME_FILE);
  }
#endif

#if SILKSCREEN_BATLOG
  BatteryLogWriter::flushAtEntry();  // a row the loop never flushed, with the card still mounted
#endif

  // Tear down WiFi so the modem power domain isn't held alive across deep sleep.
  // Wake from deep sleep is effectively a chip reset, so no state needs to survive.
  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
  }

  halTiltSensor.deepSleep();
  display.deepSleep();
  Storage.prepareForDeepSleep();
  LOG_DBG("MAIN", "Entering deep sleep");

  if (BoardConfig::powerPolicy().earlyWakeDecision && settingsLoaded) {
    sleepWakePolicy =
        SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::SLEEP ? WAKE_POLICY_CLICK : WAKE_POLICY_HOLD;
  }
  powerManager.startDeepSleep(gpio);
}

static void resolvePanelController();

#if FREEINK_CAP_POWER_POLICY
// PowerConfig::powerPinIdleCheck: what the power button's input read at this
// boot, before anything can arm its wake. Between the logic levels (R36 and R72
// both fitted, HARDWARE.md 9.2) the SDK has taken the pin out of service, so
// every sleep from here on has no wake source.
static void checkPowerButtonIdle() {
  const auto check = freeink::PowerManager::checkPowerPinIdle();
  switch (check.idle) {
    case freeink::PowerManager::PowerPinIdle::Undefined:
      LOG_ERR("PWR", "Power button IO%d idles at %u mV, between the logic levels (R36 and R72 both fitted?)", check.pin,
              check.mv);
      LOG_ERR("PWR", "Power button off for this session; sleep has no wake source, the reset button starts the board");
      break;
    case freeink::PowerManager::PowerPinIdle::High:
      LOG_INF("PWR", "Power button IO%d held at boot (%u mV)", check.pin, check.mv);
      break;
    case freeink::PowerManager::PowerPinIdle::Low:
      LOG_DBG("PWR", "Power button IO%d idles at %u mV", check.pin, check.mv);
      break;
    case freeink::PowerManager::PowerPinIdle::NotChecked:
      break;
  }
}
#endif

#if FREEINK_CAP_POWER_POLICY && !CROSSPOINT_TEST_SUITE
void setupDisplayAndFonts(bool seamless);

// PowerConfig::powerPinIdleCheck: the power button cannot wake the board, so the
// reader stops on a message saying why. While USB_STAT shows external power it
// stays there, so the board can still be flashed; on the cell (or once USB is
// unplugged) it sleeps. That sleep has no wake source, and the reset button
// starts the board again. Does nothing unless the check found the fault; call
// it before the display is set up.
static void haltOnPowerButtonFault() {
  if (!BoardConfig::powerPolicy().powerPinIdleCheck) return;
  const auto check = freeink::PowerManager::powerPinCheck();
  if (check.idle != freeink::PowerManager::PowerPinIdle::Undefined) return;
  setupDisplayAndFonts(/*seamless=*/false);
  {
    RenderLock lock;
    char message[224];
    snprintf(message, sizeof(message), tr(STR_POWER_BUTTON_FAULT), static_cast<unsigned>(check.mv / 1000U),
             static_cast<unsigned>(check.mv % 1000U / 100U));
    renderer.clearScreen();
    GUI.drawWrappedPopup(renderer, message, 6);
  }
  // USB_STAT decodes about 1.5 s after boot, or up to about 9.5 s when the cell
  // is already charged; until then the supply is unknown.
  constexpr unsigned long USB_DECODE_WAIT_MS = 12000;
  const BatteryMonitor battery;
  const unsigned long since = millis();
  for (;;) {
    BatteryMonitor::pollChargeStatus();
    bool known = false;
    const bool external = battery.isExternalPowerPresent(&known);
    if (known ? !external : millis() - since >= USB_DECODE_WAIT_MS) break;
    delay(100);
  }
  LOG_ERR("PWR", "Power button fault: sleeping with no wake source");
  display.deepSleep();
  Storage.prepareForDeepSleep();
  powerManager.startDeepSleep(gpio);
}
#endif

// PowerConfig::abnormalResetLimit (0 on boards without one): after that many
// panics, watchdog resets, brown-outs or aborted sleep entries in a row, go back
// to deep sleep at once, before SD, touch, the panel or the radio start and
// without their drivers (any of them may be what crashed). A crash or brown-out
// loop then costs one short boot per power-button press instead of boot after
// boot at tens of mA; the press's deep-sleep wake starts a new count.
static void sleepIfResetLoop() {
  const uint8_t limit = BoardConfig::powerPolicy().abnormalResetLimit;
  if (limit == 0) return;
  const auto aborted = freeink::PowerManager::takeAbortedSleepInfo();
  if (aborted.aborted) {
    LOG_ERR("PWR", "The last deep-sleep entry aborted (wake cause %d, power pin %d)", aborted.wakeupCause,
            aborted.wakePinLevel);
  }
  const uint8_t count = freeink::PowerManager::countAbnormalResets(aborted.aborted);
  if (count < limit) {
    if (count > 0) LOG_INF("PWR", "Abnormal reset %u in a row (limit %u)", count, limit);
    return;
  }
  LOG_ERR("PWR", "%u abnormal resets in a row; sleeping until the power button", count);
  freeink::PowerManager::clearAbnormalResets();
  // No sleep entry has put the panel or a touch controller on an always-on rail
  // to sleep since the crash, and a crash after a fast refresh leaves the panel's
  // analog block on. Reset each and send its sleep command without its driver
  // (no-ops on boards whose profile does not call for it). The controller is
  // resolved first so the panel gets its own command; after a crash that is a
  // probe, which is only a reset and a register read.
  resolvePanelController();
  if (freeink::PowerManager::sleepPanelWithoutDriver()) LOG_DBG("PWR", "Panel reset and asleep");
  gpio.resetTouchForSleep();
  powerManager.startDeepSleepMinimal(gpio);
}

#if FREEINK_CAP_DISPLAY_PROBE_POLICY
// DisplayProbeConfig boards: the controller this boot chose and how, with the
// probe's IDs (VER byte 2 is an UltraChip part's LUT_VER), for tuning a panel.
static void logPanelController() {
  const char* source = "not resolved";
  switch (freeink::displayControllerSource()) {
    case freeink::DisplayControllerSource::Probed:
      source = "probed";
      break;
    case freeink::DisplayControllerSource::Cached:
      source = "kept from the probe before the sleep";
      break;
    case freeink::DisplayControllerSource::Forced:
      source = "forced by the build";
      break;
    case freeink::DisplayControllerSource::NotResolved:
      break;
  }
  const char* controller = BoardConfig::controllerPartName(BoardConfig::ACTIVE.displayController);
  const auto& probe = freeink::getXteinkDisplayProbeDiag();
  if (!probe.valid) {
    LOG_INF("MAIN", "Panel controller %s (%s)", controller, source);
    return;
  }
  LOG_INF("MAIN", "Panel controller %s (%s): VER %02X %02X %02X %02X %02X, FLG %02X, variant %02X", controller, source,
          probe.ver[0], probe.ver[1], probe.ver[2], probe.ver[3], probe.ver[4], probe.flg,
          BoardConfig::ACTIVE.displayControllerVariant);
}
#endif

// The panel controller probe some boards need before display.begin().
static void resolvePanelController() {
#if !FREEINK_MCU_C3
  // C3 resolves its controller in HalGPIO::begin() before SPI claims the
  // display pins. X4 Pro skips that C3-only path, so probe here before
  // display.begin() selects and initializes its panel driver.
  static bool controllerResolved = false;
  if (!controllerResolved) {
    controllerResolved = true;
    if (freeink::applyXteinkDisplayController()) {
      LOG_DBG("MAIN", "Panel controller: UltraChip UC81xx variant detected");
    }
#if FREEINK_CAP_DISPLAY_PROBE_POLICY
    logPanelController();
#endif
  }
#endif
}

// Low-battery cutoff at boot (PowerConfig::lowBatteryCutoffMv; 0 on boards
// without one). A normal boot pays for one averaged reading. Only a reading near
// the cutoff takes the slow path: wait for the battery divider (after a cold
// power-on its reservoir capacitor is still charging) and the charge-status
// ladder to settle, then take BatteryMonitor::CUTOFF_SAMPLES readings a second
// apart. A board still below the cutoff goes back to sleep before SD or the
// panel draw any current.
void sleepIfBatteryEmptyAtBoot() {
  const uint16_t cutoffMv = BoardConfig::powerPolicy().lowBatteryCutoffMv;
  if (cutoffMv == 0) return;
  constexpr uint16_t kQuickCheckMarginMv = 100;
  const BatteryMonitor battery;
  const uint16_t quickMv = battery.readMillivoltsAveraged(16);
  if (quickMv >= cutoffMv + kQuickCheckMarginMv) return;

  const unsigned long dividerSettledAt = esp_reset_reason() == ESP_RST_POWERON ? 2500 : 0;
  const unsigned long ladderSettledAt =
      BoardConfig::hasChargeStatusLadder() ? BoardConfig::ACTIVE.chargeLadder.settleAfterBootMs : 0;
  const unsigned long waitUntil = dividerSettledAt > ladderSettledAt ? dividerSettledAt : ladderSettledAt;
  LOG_INF("PWR", "Battery reads %u mV at boot; checking it against the %u mV cutoff", quickMv, cutoffMv);
  while (millis() < waitUntil) delay(10);

  bool below = false;
  for (uint8_t i = 0; i < BatteryMonitor::CUTOFF_SAMPLES; ++i) {
    if (i > 0) delay(1010);  // BatteryMonitor takes at most one reading per second
    BatteryMonitor::pollChargeStatus();
    bool known = false;
    uint16_t mv = 0;
    below = battery.isBelowCutoff(&known, &mv);
    if (!known || mv > cutoffMv) break;  // external power, no cell, or above the cutoff
  }
  if (!below) return;
  LOG_ERR("PWR", "Battery below the %u mV cutoff at boot; sleeping", cutoffMv);
  if (esp_reset_reason() != ESP_RST_DEEPSLEEP) {
    // No sleep entry put the panel into deep sleep before this boot (power-on,
    // brown-out, crash), so it may still be in normal mode. Begin it without a
    // refresh and send it the deep-sleep command.
    resolvePanelController();
    display.begin(/*seamless=*/true);
    display.deepSleep();
  }
#if SILKSCREEN_BATLOG
  freeink::batterylog::early(freeink::batterylog::EarlyKind::Cutoff, millis());
#endif
  Storage.prepareForDeepSleep();
  powerManager.startDeepSleep(gpio);
}

void setupDisplayAndFonts(bool seamless = false) {
  resolvePanelController();
  display.begin(seamless);
  renderer.begin();
  activityManager.begin();
  LOG_DBG("MAIN", "Display initialized");

  // Initialize font decompressor for compressed reader fonts
  if (!fontDecompressor.init()) {
    LOG_ERR("MAIN", "Font decompressor init failed");
  }
  fontCacheManager.setFontDecompressor(&fontDecompressor);
  renderer.setFontCacheManager(&fontCacheManager);
  renderer.insertFont(NOTOSERIF_14_FONT_ID, notoserif14FontFamily);
#ifndef OMIT_FONTS
  renderer.insertFont(NOTOSERIF_12_FONT_ID, notoserif12FontFamily);
  renderer.insertFont(NOTOSERIF_16_FONT_ID, notoserif16FontFamily);
  renderer.insertFont(NOTOSERIF_18_FONT_ID, notoserif18FontFamily);

  renderer.insertFont(NOTOSANS_12_FONT_ID, notosans12FontFamily);
  renderer.insertFont(NOTOSANS_14_FONT_ID, notosans14FontFamily);
  renderer.insertFont(NOTOSANS_16_FONT_ID, notosans16FontFamily);
  renderer.insertFont(NOTOSANS_18_FONT_ID, notosans18FontFamily);
#endif  // OMIT_FONTS
  renderer.insertFont(UI_10_FONT_ID, ui10FontFamily);
  renderer.insertFont(UI_12_FONT_ID, ui12FontFamily);
  renderer.insertFont(SMALL_FONT_ID, smallFontFamily);

  // Discover and load SD card fonts
  sdFontSystem.begin(renderer);

  LOG_DBG("MAIN", "Fonts setup");
}

void setup() {
  BoardConfig::holdPowerRails();

#ifdef ENABLE_SERIAL_LOG
#ifdef CROSSPOINT_WAIT_FOR_USB_SERIAL
  // Development builds preserve reliable early CDC logs; release builds let
  // enumeration proceed asynchronously so users do not pay this startup cost.
  delay(250);
#endif
  Serial.begin(115200);
#if LOG_SERIAL_HAS_TX_TIMEOUT
  logSerial.setTxTimeoutMs(1);  // This is a load-bearing 1. Do not modify.
#endif
#endif

  // PowerConfig::releaseWaitMaxMs: the last sleep began with the power button
  // still held and woke on its release, which asks for nothing. Sleep again
  // before any peripheral starts.
  if (BoardConfig::powerPolicy().releaseWaitMaxMs != 0 && freeink::PowerManager::takeReleaseWake()) {
    LOG_DBG("MAIN", "Woke on the power button's release; sleeping again");
    powerManager.startDeepSleepMinimal(gpio);
  }
#if FREEINK_CAP_POWER_POLICY
  if (BoardConfig::powerPolicy().powerPinIdleCheck) checkPowerButtonIdle();
#endif

  HalSystem::begin();
  // checkPanic() clears the watchdog capture marker after a successful SD
  // dump, so retain the boot classification for the later activity route.
  const bool rebootedFromPanic = HalSystem::isRebootFromPanic();

  // Read-and-clear so a panic later in setup() doesn't loop into silent reboot.
  // Bound the target range too — RTC_NOINIT memory is uninitialized on cold boot.
  const bool isSilentReboot = (silentRebootMagic == SILENT_REBOOT_MAGIC);
  const uint32_t snapshotTarget =
      (isSilentReboot && silentRebootTarget <= SILENT_REBOOT_TARGET_MAX) ? silentRebootTarget : 0;
  const bool silentRebootLightOn = isSilentReboot && (silentRebootPayload & SILENT_REBOOT_LIGHT_ON) != 0;
  silentRebootMagic = 0;
  silentRebootTarget = 0;
  silentRebootPayload = 0;

  sleepIfResetLoop();

  const bool earlyWakeDecision = BoardConfig::powerPolicy().earlyWakeDecision;
  gpio.begin(/*deferTouch=*/earlyWakeDecision);
  powerManager.begin();

  const auto wakeupReason = gpio.getWakeupReason();
  // Sample the wake hold now — a click wake is released within milliseconds of
  // boot — but defer the sleep-or-boot decision until SETTINGS is loaded below:
  // click-to-wake is a setting, and an X4 battery power-off cuts all power, so
  // only SD state survives to the next boot.
  const bool wakeHoldVerified = wakeupReason != HalGPIO::WakeupReason::PowerButton || gpio.verifyPowerButtonWakeup();
  const bool unverifiedWake = wakeupReason == HalGPIO::WakeupReason::PowerButton && !wakeHoldVerified;
  if (earlyWakeDecision) {
    // PowerConfig::earlyWakeDecision: decide now, before touch, the RTC, SD, the
    // settings, the light, the panel or the radio start, from the wake rule the
    // last sleep entry left in RTC memory. An unknown rule waits for the
    // settings below, as on other boards.
    if (unverifiedWake && sleepWakePolicy == WAKE_POLICY_HOLD) {
      LOG_DBG("MAIN", "Power-button wake not held through verification, sleeping");
#if SILKSCREEN_BATLOG
      freeink::batterylog::early(freeink::batterylog::EarlyKind::Ghost, millis());
#endif
      powerManager.startDeepSleep(gpio);
    }
#if SILKSCREEN_BATLOG
    // A wake that goes on: an unverified one has passed under the click-wake rule.
    freeink::batterylog::onWake({wakeHoldVerified, unverifiedWake});
#endif
    gpio.startTouch();
  }

#if CROSSPOINT_TEST_SUITE
  constexpr bool recoveryFirmwareMode = false;  // no SD firmware picker: switch images over USB
#else
  // X4 Pro and X4 Classic both map BTN_UP to GPIO0 — an ESP32-S3 boot strap — so
  // gate recovery on the non-strap Down key (GPIO7) to avoid a stuck-in-recovery loop.
  const auto recoveryButton = (BoardConfig::isX4Pro() || BoardConfig::isX4Classic()) ? MappedInputManager::Button::Down
                                                                                     : MappedInputManager::Button::Up;
  const bool recoveryFirmwareMode = wakeupReason == HalGPIO::WakeupReason::PowerButton && !BoardConfig::isPaperMono() &&
                                    mappedInputManager.isPressed(recoveryButton);
#endif

  halTiltSensor.begin();
  halClock.begin();

#if FREEINK_DEVICE_X4 || FREEINK_DEVICE_X3
  LOG_INF("MAIN", "Hardware detect: %s", gpio.deviceIsX3() ? "X3" : "X4");
#else
  LOG_INF("MAIN", "Device: %s", BoardConfig::ACTIVE.name);
#endif

  sleepIfBatteryEmptyAtBoot();

  // SD Card Initialization
  // We need 6 open files concurrently when parsing a new chapter
  if (!Storage.begin()) {
    LOG_ERR("MAIN", "SD card initialization failed");
    if (earlyWakeDecision && unverifiedWake && sleepWakePolicy != WAKE_POLICY_CLICK) {
      // No settings can show that a click wakes this device, so an unverified
      // wake is a ghost wake, SD card or not.
      LOG_DBG("MAIN", "Power-button wake not held through verification, sleeping");
#if SILKSCREEN_BATLOG
      freeink::batterylog::early(freeink::batterylog::EarlyKind::Ghost, millis());
#endif
      Storage.prepareForDeepSleep();
      powerManager.startDeepSleep(gpio);
    }
#if !CROSSPOINT_TEST_SUITE  // the test menu opens without a card and says so
#if FREEINK_CAP_POWER_POLICY
    haltOnPowerButtonFault();
#endif
    setupDisplayAndFonts(isSilentReboot);
    activityManager.goToFullScreenMessage("SD card error", EpdFontFamily::BOLD);
    storageErrorScreen = true;
    return;
#endif
  }

  HalSystem::checkPanic();

#if !CROSSPOINT_TEST_SUITE  // no open book or reader resume in the test firmware: a splash on every boot
  APP_STATE.loadFromFile();
#endif
  const bool isSleepWake = wakeupReason == HalGPIO::WakeupReason::PowerButton;
  const bool isPersistedSleepWake = isSleepWake && !APP_STATE.showBootScreen;

  if (recoveryFirmwareMode) {
    LOG_INF("MAIN", "Recovery firmware mode (%s + POWER held at boot)",
            (BoardConfig::isX4Pro() || BoardConfig::isX4Classic()) ? "DOWN" : "UP");
  }

  // Touch boards default the reader menu to the toolbar overlay instead of the
  // full-screen list. Seeded before the load: fromJson() falls back to the
  // in-memory value only when the file carries no readerMenuStyle key, so a
  // user's saved choice (either style) still wins.
  if (gpio.hasTouch()) {
    SETTINGS.readerMenuStyle = CrossPointSettings::READER_MENU_TOOLBAR;
  }
  SETTINGS.loadFromFile();
  settingsLoaded = true;
#if CROSSPOINT_TEST_SUITE
  testsuite::beginAfterSettings();  // pins test values in RAM (never saved); housekeeping on the card
#endif
  // Push the saved timezone's POSIX rule into the clock (migrating the legacy
  // UTC-offset setting on first boot after the update).
  timezones::applyToClock();
  RECENT_BOOKS.loadFromFile();
  I18N.setLanguage(static_cast<Language>(SETTINGS.language));
  KOREADER_STORE.loadFromFile();
  OPDS_STORE.loadFromFile();
  UITheme::getInstance().reload();
  ButtonNavigator::setMappedInputManager(mappedInputManager);
#if FREEINK_CAP_POWER_POLICY && !CROSSPOINT_TEST_SUITE
  haltOnPowerButtonFault();  // in the saved language, before the light or the panel start
#endif
#if !CROSSPOINT_TEST_SUITE  // no plugins or loan clock in the hardware test firmware
  pluginevents::refreshSubscriptions();
  // Restore the monotonic clock floor before anything reads time() (event
  // timestamps, loan-expiry checks).
  trustedtime::init();
#endif

  switch (wakeupReason) {
    case HalGPIO::WakeupReason::PowerButton:
      // With Short Power Button Press = Sleep, a single click wakes on any
      // device; otherwise the button must still be held (ghost-wake debounce).
      if (!wakeHoldVerified && SETTINGS.shortPwrBtn != CrossPointSettings::SHORT_PWRBTN::SLEEP) {
        LOG_DBG("MAIN", "Power-button wake not held through verification, sleeping");
#if SILKSCREEN_BATLOG
        freeink::batterylog::early(freeink::batterylog::EarlyKind::Ghost, millis());
#endif
        Storage.prepareForDeepSleep();
        powerManager.startDeepSleep(gpio);
      }
      wakePowerReleasePending = true;
      wakeNoInputArmed = BoardConfig::powerPolicy().wakeNoInputTimeoutS > 0;
      break;
    case HalGPIO::WakeupReason::AfterUSBPower:
      // Most devices return to sleep after a USB-powered cold boot.
      LOG_DBG("MAIN", "Wakeup reason: After USB Power");
#if FREEINK_DEVICE_X4PRO || FREEINK_DEVICE_X4CLASSIC || FREEINK_DEVICE_PAPERMONO || FREEINK_DEVICE_EEGO_A4 || \
    FREEINK_DEVICE_METALIO_EINK4 || FREEINK_DEVICE_SILKSCREEN
      // X4 Pro must stay awake so USB Serial/JTAG remains available after leaving
      // USB Drive and reconnecting the cable. Paper Mono has no armable GPIO wake
      // (its button is behind the PMIC). Metalio also needs native USB available
      // after a USB-powered boot. EEGO A4's post-flash reset reads as
      // POWERON (native-USB), so a flash would otherwise be misclassified as a
      // USB-power cold boot and sleep. Sleeping any of these here would strand
      // the device in a USB-replug boot loop (or sleep right after a flash).
      // Silkscreen has no hardware off (3V3 always live) and, after a battery-
      // protection trip, restarts only when USB is plugged in. Its USB presence
      // comes from the USB_STAT ladder, which is still unknown this early in
      // boot, so a USB cold boot classifies as Other and boots normally; this
      // term keeps that outcome should the ladder ever be known at boot.
      break;
#else
      Storage.prepareForDeepSleep();
      powerManager.startDeepSleep(gpio);
      break;
#endif
    case HalGPIO::WakeupReason::AfterFlash:
      // After flashing, just proceed to boot
    case HalGPIO::WakeupReason::Other:
    default:
      break;
  }

  // Brightness and warmth are always restored. A normal wake starts with the
  // light off unless Restore Light on Wake is enabled; silent maintenance
  // reboots replay the live state captured at restart, so they neither go dark
  // nor light up against the user's wake preference. Only after the wake
  // decision above, so a wake that goes straight back to sleep never drives the
  // light.
  const bool restoreLightOn =
      isSilentReboot ? silentRebootLightOn : (SETTINGS.frontlightOn != 0 && SETTINGS.frontlightRestoreOnWake != 0);
#if CROSSPOINT_TEST_SUITE
  // Only a front-light test lights the boost, after the operator confirms a load:
  // no restore on wake and no replay after a silent restart.
  (void)restoreLightOn;
  Frontlight.begin(SETTINGS.frontlightBrightness, SETTINGS.frontlightWarmth, /*on=*/false);
#else
  Frontlight.begin(SETTINGS.frontlightBrightness, SETTINGS.frontlightWarmth, restoreLightOn);
#endif

  LOG_DBG("MAIN", "Starting CrossPoint version " CROSSPOINT_VERSION);

  // Resolve the single boot-presentation decision. Skipping the splash also
  // skips the panel-clearing pass and the X3 initial-full-sync arming (see
  // HalDisplay::begin), so the first paint is FAST_REFRESH (~500ms) over the
  // retained frame and input dispatches against a visible UI.
  // Only a verified deep-sleep wake may use the one-shot persisted flag.
  // Otherwise a stale flag could suppress the splash on a cold boot.
  const BootResume resume = isSilentReboot         ? BootResume::Silent
                            : isPersistedSleepWake ? BootResume::SplashlessWake
                                                   : BootResume::Splash;
  bool allowFastInitialReaderRefresh = false;
  bool needsWakeRefresh = false;

  setupDisplayAndFonts(resume != BootResume::Splash);

  switch (resume) {
    case BootResume::Silent:
      // Splash skipped: the routing block below picks the target activity; the
      // panel keeps showing the pre-reboot popup until that first paint lands.
      break;
    case BootResume::SplashlessWake:
      // One-shot flag: re-arm the splash for the next ordinary boot. Save
      // before any painting so a hang in the blocking paint path can't strand
      // us in a splashless-with-no-frame loop on the next boot.
      APP_STATE.showBootScreen = true;
      APP_STATE.saveToFile();
      if (Storage.exists(SLEEP_FRAME_FILE) && loadSleepFrameBuffer()) {
        if (gpio.deviceIsX3()) {
          // begin() clears the X3 controller RAM, so restore the saved frame as
          // the baseline for the first reader paint without refreshing the panel.
          renderer.cleanupGrayscaleWithFrameBuffer();
          allowFastInitialReaderRefresh = true;
        }
      } else {
        // Clean the retained sleep image as part of the first Home paint.
        needsWakeRefresh = true;
      }
      break;
    case BootResume::Splash:
      activityManager.goToBoot();
      break;
  }

  // Output polarity is resolved per render by ActivityManager (night mode
  // inverts only the reading surfaces), so nothing to restore here.

  if (recoveryFirmwareMode) {
    // Skip normal home/reader routing: jump straight into the SD firmware picker.
    activityManager.replaceActivity(
        std::make_unique<SdFirmwareUpdateActivity>(renderer, mappedInputManager, /*recoveryMode=*/true));
  } else if (rebootedFromPanic) {
    // If we rebooted from a panic, go to crash report screen to show the panic info
    activityManager.goToCrashReport();
  } else if (resume == BootResume::Silent && snapshotTarget == SILENT_REBOOT_TARGET_READER &&
             !APP_STATE.openEpubPath.empty()) {
    activityManager.goToReader(APP_STATE.openEpubPath);
  } else if (resume == BootResume::Silent && snapshotTarget == SILENT_REBOOT_TARGET_JOIN_NETWORK) {
    // Rebooted on the way *into* File Transfer > Join Network for a fresh heap;
    // resume that flow directly instead of landing on home.
    activityManager.goToJoinNetwork();
  } else if (resume == BootResume::Silent && snapshotTarget == SILENT_REBOOT_TARGET_SETTINGS) {
    // Back out of the WiFi rows and the user is where they left off, not on Home.
    activityManager.goToSettings();
  } else if (resume == BootResume::Silent) {
    // target == home (or reader with no open book): land on home — don't fall
    // through to the sleep-wake "resume reader" logic, which fires on stale
    // openEpubPath + lastSleepFromReader from a prior session.
    activityManager.goHome();
  } else if (APP_STATE.openEpubPath.empty() || !APP_STATE.lastSleepFromReader ||
             mappedInputManager.isPressed(MappedInputManager::Button::Back) || APP_STATE.readerActivityLoadCount > 0) {
    // Boot to home screen if no book is open, last sleep was not from reader, back button is held, or reader activity
    // crashed (indicated by readerActivityLoadCount > 0)
    activityManager.goHome(HomeMenuItem::NONE, needsWakeRefresh);
  } else {
    // Clear app state to avoid getting into a boot loop if the epub doesn't load
    const auto path = APP_STATE.openEpubPath;
    APP_STATE.openEpubPath = "";
    APP_STATE.readerActivityLoadCount++;
    APP_STATE.saveToFile();
    activityManager.goToReader(path, allowFastInitialReaderRefresh);
  }

  if (resume == BootResume::Silent) {
    // Block until the first paint physically completes. refreshDisplay()
    // waits on the panel BUSY pin so when this returns the user can see the
    // new activity. Without the wait, an edge captured by gpio.update()
    // during boot dispatches against an invisible Home and the default
    // selectorIndex=0 opens the most-recent book.
    activityManager.requestUpdateAndWait();
    // Absorb any button held at this point into currentState as a non-edge:
    // two gpio.update() calls separated by > InputManager's 5ms debounce
    // transition the held bit through lastDebounceTime into currentState
    // without setting pressedEvents, so the first loop()'s own gpio.update()
    // sees state == currentState and emits nothing.
    gpio.update();
    delay(10);
    gpio.update();
  }

  allowSleepAt = millis() + 2000;
}

void loop() {
  static unsigned long maxLoopDuration = 0;
  const unsigned long loopStartTime = millis();
  static unsigned long lastMemPrint = 0;

  gpio.setSharedConfirmPowerShortPressEmitsPower(SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::SLEEP);
  mappedInputManager.update();

#if SILKSCREEN_BATLOG
  tickBatteryLog();  // before the exclusive-storage branch, so USB Drive time counts
#endif

  if (activityManager.requiresExclusiveStorageLoop()) {
    // USB Drive handed the raw SD card to the host. Do not run screenshots,
    // sleep, shortcuts, or normal navigation while its filesystem is detached.
    activityManager.loop();
    if (activityManager.preventAutoSleep()) {
      powerManager.setPowerSaving(false);
      delay(10);
    } else {
      // No host is active, so a slower loop is safe. The activity itself times
      // out the raw-storage handoff rather than entering deep sleep detached.
      powerManager.setPowerSaving(true);
      delay(50);
    }
    return;
  }

  halTiltSensor.update(SETTINGS.tiltPageTurn, SETTINGS.orientation, activityManager.isReaderActivity());

  renderer.setFadingFix(SETTINGS.fadingFix);

#if FREEINK_CAP_FRONTLIGHT_PROBE && !CROSSPOINT_TEST_SUITE
  handleFrontlightNotices();
  keepWarmthOnPresentString();
#else
  handleFrontlightTrip();
#endif

  // The ROM console does not depend on Arduino USB CDC's connection state.
  if ((Serial || FREEINK_LOG_TRANSPORT == FREEINK_LOG_TRANSPORT_ROM_PRINTF) && millis() - lastMemPrint >= 10000) {
    const auto heap = HalMemory::getInternalHeap();
    LOG_INF("MEM", "Free: %zu bytes, Total: %zu bytes, Min Free: %zu bytes, MaxAlloc: %zu bytes", heap.freeBytes,
            heap.totalBytes, heap.minFreeBytes, heap.largestBlockBytes);
#ifdef BOARD_HAS_PSRAM
    const auto psram = HalMemory::getPsramHeap();
    LOG_INF("MEM", "PSRAM: Free: %zu bytes, Total: %zu bytes, Min Free: %zu bytes, MaxAlloc: %zu bytes",
            psram.freeBytes, psram.totalBytes, psram.minFreeBytes, psram.largestBlockBytes);
#endif
    lastMemPrint = millis();
  }

  // Handle incoming serial commands,
  // nb: we use logSerial from logging to avoid deprecation warnings
  if (logSerial.available() > 0) {
    String line = logSerial.readStringUntil('\n');
    if (line.startsWith("CMD:")) {
      String cmd = line.substring(4);
      cmd.trim();
      if (cmd == "SCREENSHOT") {
        const uint32_t bufferSize = display.getBufferSize();
        logSerial.printf("SCREENSHOT_START:%d\n", bufferSize);
        uint8_t* buf = display.getFrameBuffer();
        logSerial.write(buf, bufferSize);
        logSerial.printf("SCREENSHOT_END\n");
      }
    }
  }

  // Check for any user activity (button press or release) or active background work
  static unsigned long lastActivityTime = millis();
  // The input clock (HalPowerManager::noteInput()) starts with the first pass.
  static bool inputClockStarted = false;
  const bool userInput =
      gpio.wasAnyPressed() || gpio.wasAnyReleased() || gpio.wasTouchActivity() || halTiltSensor.hadActivity();
  if (userInput || !inputClockStarted) {
    powerManager.noteInput();
    inputClockStarted = true;
  }
#if SILKSCREEN_BATLOG
  if (userInput) freeink::batterylog::noteInput();
#endif
  // The release of the press that woke the device is not the user's first input.
  if (userInput && !wakePowerReleasePending) wakeNoInputArmed = false;
  if (userInput || activityManager.preventAutoSleep()) {
    lastActivityTime = millis();         // Reset inactivity timer
    powerManager.setPowerSaving(false);  // Restore normal CPU frequency on user activity
  }

  if (BoardConfig::powerPolicy().abnormalResetLimit > 0) {
    static bool resetRunEnded = false;
    if (!resetRunEnded && millis() >= HEALTHY_UPTIME_MS) {
      resetRunEnded = true;
      freeink::PowerManager::clearAbnormalResets();
    }
  }

  // Battery awake limits (BoardConfig::powerPolicy(); every value is 0 on boards
  // without one). They run before anything below can return early (the wake
  // release, the screenshot chord, the double-click), so a held key cannot keep
  // the device awake on battery. They count from HalPowerManager's input and
  // client-traffic clocks, not lastActivityTime, which a screen holding
  // auto-sleep off resets on every pass. Each applies on battery only.
  const unsigned long sleepTimeoutMs = SETTINGS.getSleepTimeoutMs();

  // Low-battery cutoff (PowerConfig::lowBatteryCutoffMv), at most one reading a
  // second. The divider reads low under the radio's load, so with Wi-Fi on the
  // reading is allowed PowerConfig::cutoffRadioAllowanceMv below the cutoff.
  // BatteryMonitor needs several consecutive readings at or below it and never
  // fires while external power carries the load. A blocking transfer checks the
  // same latch from its callbacks and aborts, so this pass sleeps.
  if (BoardConfig::powerPolicy().lowBatteryCutoffMv > 0 && powerManager.lowBatteryCutoffTripped()) {
#if SILKSCREEN_BATLOG
    freeink::batterylog::tagPath(freeink::batterylog::Path::Cutoff);
#endif
    enterDeepSleep();
    return;
  }

  // Battery awake ceiling (PowerConfig::awakeCeilingMin). On battery, a screen
  // that holds auto-sleep off (network, keyboard, update), a Never timeout or a
  // held key still ends once nothing has pressed a key or talked to the web
  // server for this long. The ceiling is never shorter than the user's timeout
  // (HalPowerManager::awakeCeilingMs()), so Never gets the profile's minutes.
  // enterDeepSleep() drops Wi-Fi. Where the user's timeout is reached later this
  // pass (one is set, no screen holds auto-sleep off and no key is held), that
  // timeout governs alone.
  if (BoardConfig::powerPolicy().awakeCeilingMin > 0) {
    bool keyHeld = false;
    for (uint8_t button = HalGPIO::BTN_BACK; button <= HalGPIO::BTN_POWER; ++button) {
      if (gpio.isPressed(button)) keyHeld = true;
    }
    const bool governedByTimeout = sleepTimeoutMs > 0 && !activityManager.preventAutoSleep() && !keyHeld;
    if (!governedByTimeout && powerManager.awakeCeilingPassed(sleepTimeoutMs)) {
      LOG_INF("SLP", "No input or client traffic for %lu min on battery; sleeping",
              HalPowerManager::awakeCeilingMs(sleepTimeoutMs) / 60000UL);
#if SILKSCREEN_BATLOG
      freeink::batterylog::tagPath(freeink::batterylog::Path::Ceiling);
#endif
      enterDeepSleep(true);
      return;
    }
  }

  // Input-only cap on battery (PowerConfig::awakeInputCapMin): client traffic
  // cannot extend it, since a desktop that keeps a WebDAV share mounted polls
  // without anyone at the device.
  if (powerManager.awakeInputCapPassed()) {
    LOG_INF("SLP", "No input for %u min on battery; sleeping", BoardConfig::powerPolicy().awakeInputCapMin);
#if SILKSCREEN_BATLOG
    freeink::batterylog::tagPath(freeink::batterylog::Path::InputCap);
#endif
    enterDeepSleep(true);
    return;
  }

  // A power-button wake that gets no input (a press in a pocket that passed the
  // hold check) sleeps again on battery (PowerConfig::wakeNoInputTimeoutS).
  if (const unsigned long wakeMs = BoardConfig::powerPolicy().wakeNoInputTimeoutS * 1000UL;
      wakeNoInputArmed && wakeMs > 0 && powerManager.msSinceInput() >= wakeMs && !activityManager.preventAutoSleep() &&
      !gpio.isUsbConnected()) {
    LOG_INF("SLP", "No input for %lu s after the wake on battery; sleeping", wakeMs / 1000UL);
    enterDeepSleep(true, /*unattended=*/true);
    return;
  }

  // Nothing can be done on the SD-card error screen (PowerConfig::storageErrorSleepS).
  if (const unsigned long errorMs = BoardConfig::powerPolicy().storageErrorSleepS * 1000UL;
      storageErrorScreen && errorMs > 0 && powerManager.msSinceInput() >= errorMs && !gpio.isUsbConnected()) {
    LOG_INF("SLP", "SD card error screen idle for %lu s on battery; sleeping", errorMs / 1000UL);
#if SILKSCREEN_BATLOG
    freeink::batterylog::tagPath(freeink::batterylog::Path::SdError);
#endif
    enterDeepSleep(true);
    return;
  }

  // Let wake continue as soon as its hold has been verified. The release can
  // arrive after setup, so consume that one input frame rather than making it
  // a page turn, refresh, or other short power-button action.
  if (wakePowerReleasePending && !gpio.isPressed(HalGPIO::BTN_POWER)) {
    wakePowerReleasePending = false;
    return;
  }

  // The chord returns below skip the idle wait at the end of loop(). On boards
  // with a battery policy, pause while the keys stay down so a held chord does
  // not spin the core at full clock (the awake limits above still end it).
  const auto pauseWhileChordHeld = [] {
    if (BoardConfig::powerPolicy().awakeCeilingMin > 0) delay(10);
  };
  static bool screenshotButtonsReleased = true;
  static bool screenshotComboActive = false;
  if (gpio.isPressed(HalGPIO::BTN_POWER) && gpio.isPressed(HalGPIO::BTN_DOWN)) {
    screenshotComboActive = true;
    if (screenshotButtonsReleased) {
      screenshotButtonsReleased = false;
      {
        RenderLock lock;
        ScreenshotUtil::takeScreenshot(renderer);
      }
    }
    pauseWhileChordHeld();
    return;
  }
  if (screenshotComboActive) {
    if (gpio.isPressed(HalGPIO::BTN_POWER)) {
      pauseWhileChordHeld();
      return;
    }
    if (gpio.wasReleased(HalGPIO::BTN_POWER)) {
      screenshotButtonsReleased = true;
      screenshotComboActive = false;
      return;
    }
    screenshotButtonsReleased = true;
    screenshotComboActive = false;
  }

  // A click already past its window is noted before the double-click handler below can
  // re-arm lastPowerClickAt with this pass's own release (a second tap that lands in the
  // first pass after the window): the first click is then still handed back.
  const unsigned long armedClickAt = lastPowerClickAt;
  const bool pendingClickExpired = armedClickAt != 0 && millis() - armedClickAt > POWER_DOUBLE_CLICK_MS;

  // Consume the second power-button release of a front-light double-click (X4 Pro,
  // Silkscreen) so it does not also run a configured short-power action.
  if (handlePowerDoubleClickLight()) {
    return;
  }

  const bool powerDoubleClickLight = SETTINGS.powerDoubleClickLight();
  const auto shortPwr = SETTINGS.shortPwrBtn;
  const bool clickWindowPassed = lastPowerClickAt != 0 && millis() - lastPowerClickAt > POWER_DOUBLE_CLICK_MS;
  // Silkscreen: with the double-click live, every other short-click action (Page Turn,
  // Refresh, Footnotes, Ignore) also waits out the window. Acting on the first release
  // made a double-click run that action, and when the action's refresh held the loop past
  // the window, run it twice and never toggle the light. The click's release is hidden
  // and handed back once the window passes with no second click. Sleep and, with a touch
  // panel, Confirm keep their own deferrals below (the X4 Pro's, unchanged); without a
  // panel Confirm has none, so it takes this one.
  const bool deferClickActions = powerDoubleClickLight && BoardConfig::isSilkscreen() &&
                                 shortPwr != CrossPointSettings::SHORT_PWRBTN::SLEEP &&
                                 !(shortPwr == CrossPointSettings::SHORT_PWRBTN::PWR_CONFIRM && gpio.hasTouch());
  if (deferClickActions) {
    const bool clickReleased =
        gpio.wasReleased(HalGPIO::BTN_POWER) && gpio.getPowerButtonHeldTime() <= POWER_CLICK_MAX_HOLD_MS;
    // This pass's own release re-armed the timer for the click it hides: keep that one armed.
    if (pendingClickExpired && lastPowerClickAt == armedClickAt) lastPowerClickAt = 0;
    mappedInputManager.setPowerClickDeferral(clickReleased, pendingClickExpired);
  } else if (clickWindowPassed &&
             !(powerDoubleClickLight && (shortPwr == CrossPointSettings::SHORT_PWRBTN::SLEEP ||
                                         shortPwr == CrossPointSettings::SHORT_PWRBTN::PWR_CONFIRM))) {
    // Expire a click no deferral below will consume, so a later switch to Sleep or
    // Confirm cannot act on it.
    lastPowerClickAt = 0;
  }

#if FREEINK_CAP_TOUCH
  // A single power click becomes Confirm only after the frontlight
  // double-click window expires without a second click.
  mappedInputManager.setPowerConfirmClickFrame(false);
  if (SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::PWR_CONFIRM && powerDoubleClickLight) {
    if (lastPowerClickAt != 0 && millis() - lastPowerClickAt > POWER_DOUBLE_CLICK_MS) {
      lastPowerClickAt = 0;
      mappedInputManager.setPowerConfirmClickFrame(true);
    }
    // A release held too long to be a double-click candidate (but still within
    // the normal Confirm press duration) never reaches handlePowerDoubleClickLight's
    // click tracking above, so it needs its own Confirm check here.
    if (mappedInputManager.wasReleased(MappedInputManager::Button::Power) &&
        gpio.getPowerButtonHeldTime() > POWER_CLICK_MAX_HOLD_MS &&
        gpio.getPowerButtonHeldTime() <= SETTINGS.getPowerButtonDuration()) {
      mappedInputManager.setPowerConfirmClickFrame(true);
    }
  }
#endif

  // Same deferral for SLEEP: getPowerButtonDuration() drops to 10ms so a quick
  // tap sleeps the device, which otherwise fires on button-down and never lets
  // a second click land. Sleep only once the double-click window has passed.
  if (SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::SLEEP && powerDoubleClickLight &&
      lastPowerClickAt != 0 && millis() - lastPowerClickAt > POWER_DOUBLE_CLICK_MS) {
    lastPowerClickAt = 0;
    enterDeepSleep();
    // This should never be hit as `enterDeepSleep` calls esp_deep_sleep_start
    return;
  }

  if (sleepTimeoutMs > 0 && millis() - lastActivityTime >= sleepTimeoutMs) {
    LOG_DBG("SLP", "Auto-sleep triggered after %lu ms of inactivity", sleepTimeoutMs);
    enterDeepSleep(true);
    // This should never be hit as `enterDeepSleep` calls esp_deep_sleep_start
    return;
  }

  // A hold that woke the device must be released before it can count as a new
  // in-app long press. Otherwise a user who keeps holding after wake would put
  // the device straight back to sleep once allowSleepAt expires.
  static bool powerReleasedSinceWake = false;
  if (!gpio.isPressed(HalGPIO::BTN_POWER)) powerReleasedSinceWake = true;

  // With the double-click and SLEEP, a press still within the click window is a
  // double-click candidate — let it be released and evaluated above instead
  // of sleeping on button-down.
  const bool awaitingDoubleClickWindow = powerDoubleClickLight &&
                                         SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::SLEEP &&
                                         gpio.getPowerButtonHeldTime() <= POWER_CLICK_MAX_HOLD_MS;

  if (!awaitingDoubleClickWindow && powerReleasedSinceWake && millis() >= allowSleepAt &&
      gpio.isPressed(HalGPIO::BTN_POWER) && gpio.getPowerButtonHeldTime() > SETTINGS.getPowerButtonDuration()) {
    // If the screenshot combination is potentially being pressed, don't sleep
    if (gpio.isPressed(HalGPIO::BTN_DOWN)) {
      return;
    }
    LOG_DBG("MAIN", "Power button held %lums, sleeping", gpio.getPowerButtonHeldTime());
    enterDeepSleep();
    // This should never be hit as `enterDeepSleep` calls esp_deep_sleep_start
    return;
  }

#if FREEINK_DEVICE_PAPERMONO
  // Paper Mono reports the PMIC power button as a one-tick click, so the held
  // path above cannot fire. With the default Ignore action, retain the normal
  // power-button meaning and shut down; explicit alternate bindings still win.
  if ((SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::SLEEP ||
       SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::IGNORE) &&
      millis() >= allowSleepAt && mappedInputManager.wasReleased(MappedInputManager::Button::Power)) {
    enterDeepSleep();
    return;
  }
#endif

  // Refresh screen when power button is short-pressed with FORCE_REFRESH setting.
  if (mappedInputManager.homeButtonAction() == HomeButtonAction::ToggleFrontlight ||
      (SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::TOGGLE_FRONTLIGHT &&
       mappedInputManager.wasReleased(MappedInputManager::Button::Power))) {
    toggleFrontlight();
  }
  if (mappedInputManager.homeButtonAction() == HomeButtonAction::Refresh ||
      (SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::FORCE_REFRESH &&
       mappedInputManager.wasReleased(MappedInputManager::Button::Power))) {
    LOG_DBG("MAIN", "Manual screen refresh triggered");
    if (!activityManager.handleForcedRefresh()) {
      RenderLock lock;
      renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    }
  }

  // Refresh the battery icon when USB is plugged or unplugged.
  // Placed after sleep guards so we never queue a render that won't be processed.
  // Not while reading: there a repaint is a full page re-render (visible
  // flash, the AA pass re-running, and a frontlight dip under the refresh
  // load); the reader's status bar picks the charging state up on the next
  // page turn instead.
  if (gpio.wasUsbStateChanged() && !activityManager.isReaderActivity()) {
    activityManager.requestUpdate();
  }

  const unsigned long activityStartTime = millis();
  activityManager.loop();
  const unsigned long activityDuration = millis() - activityStartTime;

  const unsigned long loopDuration = millis() - loopStartTime;
  if (loopDuration > maxLoopDuration) {
    maxLoopDuration = loopDuration;
    if (maxLoopDuration > 50) {
      LOG_DBG("LOOP", "New max loop duration: %lu ms (activity: %lu ms)", maxLoopDuration, activityDuration);
    }
  }

  bool skipLoopDelay = false;
#if SILKSCREEN_BATLOG
  // Set once the render task has been seen holding the lock. The first paint of a wake is only queued in the pass
  // that starts it, and the render task cannot run until the loop yields, so a free lock in that pass proves nothing.
  static bool sawRenderBusy = false;
#endif
  {
    RenderLock lock(RenderLock::Mode::Try);
    if (!lock.ownsLock()) {
#if SILKSCREEN_BATLOG
      sawRenderBusy = true;
#endif
      // Let rendering advance without treating lock contention as idle.
      delay(10);
      return;
    }
    skipLoopDelay = activityManager.skipLoopDelay();
  }

#if SILKSCREEN_BATLOG
  // The render task is idle again after a paint: the first such pass writes the wake's row. A wake that never shows
  // the render task busy is left to the flush at sleep entry.
  if (!deepSleepInProgress && sawRenderBusy) BatteryLogWriter::flushIfDue();
#endif

  // Add delay at the end of the loop to prevent tight spinning
  // When an activity requests skip loop delay (e.g., webserver running), use yield() for faster response
  // Otherwise, use longer delay to save power
  if (skipLoopDelay) {
    powerManager.setPowerSaving(false);  // Make sure we're at full performance when skipLoopDelay is requested
    yield();                             // Give FreeRTOS a chance to run tasks, but return immediately
  } else {
    if (millis() - lastActivityTime >= HalPowerManager::IDLE_POWER_SAVING_MS) {
      // If we've been inactive for a while, increase the delay to save power
      powerManager.setPowerSaving(true);  // Lower CPU frequency after extended inactivity
      // Sleep in short slices and wake the poll as soon as a button contact closes.
      // InputManager commits a press only when two consecutive polls agree, so a
      // press shorter than one 50 ms sleep could land in a single sample and be lost.
      const unsigned long idleStart = millis();
      while (millis() - idleStart < 50) {
        delay(10);
        if (gpio.rawInputActive()) break;
      }
    } else {
      // Short delay to prevent tight loop while still being responsive
      delay(10);
    }
  }
}

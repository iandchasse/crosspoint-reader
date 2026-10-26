#pragma once

#include "CrossPointSettings.h"
#include "activities/Activity.h"
#include "components/UiAppHost.h"
#include "util/ButtonNavigator.h"

// Top-anchored control center opened by a top-edge down-swipe, a status-bar tap,
// or a button bound to "Control Center"; boards with keys and a light also
// offer it in Display settings. The panel has a grabber,
// the frontlight brightness/warmth sliders (on boards with a light), and a row
// of quick-setting icons — night mode, ghost-cleanup refresh, reading
// orientation, reader touch controls, and Bluetooth. Enabled toggles have filled
// rounded squares; all other controls are outlined. The grabber sits
// along the panel's bottom edge, the edge the sheet is dragged from.
class FrontlightPanelActivity final : public Activity, private UiAppHost {
  ButtonNavigator buttonNavigator;

  uint8_t brightness = 60;
  uint8_t warmth = 50;
  bool lightOn = false;
  // lightOn is seeded from the live hardware state (Frontlight.isOn()), which
  // legitimately diverges from the saved SETTINGS.frontlightOn preference —
  // e.g. after a wake with frontlightRestoreOnWake off, the light stays off
  // live while the saved "was on" preference is deliberately kept (see
  // main.cpp's restoreLightOn). brightness/warmth have no such divergence
  // (always restored unconditionally on boot), so only lightOn needs a
  // touched-by-the-user flag: onExit() must not persist a mirror that never
  // reflected user intent in the first place.
  bool lightOnChanged = false;
  bool draggingSlider = false;
  // The touch tile toggles SETTINGS.touchReaderControls between off and this
  // remembered mode, so a Swipe or Inverted Tap user gets their mode back
  // rather than the Tap default. Seeded from the setting in onEnter().
  uint8_t touchModeRestore = CrossPointSettings::TOUCH_READER_ON;
  int panelBottom = 0;

  // Quick settings, left to right: night mode, refresh, orientation, touch, Bluetooth.
  static constexpr int kTileCount = 5;

  // SliderRowProps embeds a StyleSet, so keep it off the render stack.
  freeink::ui::SliderRowProps rowProps;

  static void panelScreen(UiScreen& screen, void* user);
  static void onBrightnessEvent(const freeink::ui::ActionEvent& event, void* user);
  static void onWarmthEvent(const freeink::ui::ActionEvent& event, void* user);
  static void onToggleEvent(const freeink::ui::ActionEvent& event, void* user);
  static void onBrightnessStepEvent(const freeink::ui::ActionEvent& event, void* user);
  static void onWarmthStepEvent(const freeink::ui::ActionEvent& event, void* user);
  static void onTileEvent(const freeink::ui::ActionEvent& event, void* user);

  void buildPanelScreen(UiScreen& screen);
  // One slider row: a caption line (name + live percentage) above
  // [-] [draggable 1-bit capsule] [+], plus a lamp on/off button after the +
  // when showToggle is set (the brightness row).
  void addSliderRow(UiScreen& screen, const char* label, uint8_t value, freeink::ui::ActionId sliderAction,
                    freeink::ui::ActionId stepAction, bool showToggle);
  int computePanelBottom() const;
  void adjustBrightness(int delta);
  void adjustWarmth(int delta);
  // Up/Down on a board with keys: warmer/cooler. A fixed warm/cool selection
  // jumps straight to the other string.
  void stepWarmthByKey(bool warmer);
  void toggleLight();
  // After a switch-on or colour change from this panel: a load probe that found no
  // light (FrontlightConfig::loadProbeWindowMs) leaves the light off and shows why.
  // The probe decides on the SDK's timer, so loop() asks again until it has.
  // One string of a colour-select light is missing (a single-colour light): the
  // warmth row stays on the other string, says so, and ignores input.
  // Without FREEINK_CAP_FRONTLIGHT_PROBE neither can happen.
#if FREEINK_CAP_FRONTLIGHT_PROBE
  void noteSwitchOnResult();
  bool warmthLocked() const;
  // A brightness change switches the light on, except after a switch-on found no
  // light: then the slider and the steps only set the level, so one drag or held
  // key does not run the unloaded probe again on every event, and the lamp toggle
  // is the retry.
  bool levelSwitchesOn() const { return !noLightNote; }
#else
  void noteSwitchOnResult() {}
  bool warmthLocked() const { return false; }
  bool levelSwitchesOn() const { return true; }
#endif
  void runTile(int idx);
  // Copy the panel's live brightness/warmth/lightOn into SETTINGS and save if
  // anything actually changed. onExit() runs it on every way out.
  void persistLightSettings();
  void close();

  // One-shot: a tile that rewrote the whole frame (night mode) re-drives it
  // with the ghost-cleanup waveform on the next render. The "refresh" tile does
  // not use this — it closes the panel and promotes the repaint underneath
  // instead (GfxRenderer::promoteNextRefresh).
  bool cleanRefreshPending = false;

#if FREEINK_CAP_FRONTLIGHT_PROBE
  // The last switch-on from this panel found no light: the message sits under the
  // brightness row until the light comes on.
  bool noLightNote = false;
  // A switch-on from this panel has not decided its load yet.
  bool awaitingSwitchOnResult = false;
  // From loop(): take a switch-on's result once it has decided, and follow a light
  // the main loop switched off after a trip.
  void syncLightState();
  // The line under the warmth row of a single-colour light, or nullptr.
  static const char* singleStringNote();
  // A note under a row: small text wrapped to the panel's content width, at most
  // 3 lines. noteHeight() is what drawNote() takes, less the gap after it.
  static freeink::ui::TextStyle noteStyle(const freeink::ui::ThemeTokens& tokens);
  int16_t noteHeight(const freeink::ui::DrawTarget& target, const freeink::ui::ThemeTokens& tokens,
                     const char* text) const;
  void drawNote(UiScreen& screen, const char* text) const;
#endif

 public:
  explicit FrontlightPanelActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool handleHomeGesture() override;
  bool showsFrontlightNotice() const override { return true; }
};

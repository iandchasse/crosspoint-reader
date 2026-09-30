#pragma once

#include "CrossPointSettings.h"
#include "activities/Activity.h"
#include "components/UiAppHost.h"
#include "util/ButtonNavigator.h"

// Top-anchored control center opened by a top-edge down-swipe, a status-bar tap,
// or, on a board with keys and a light, Settings > Display > Frontlight (iOS
// Control Center style): a grabber,
// the frontlight brightness/warmth sliders (on boards with a light), and a grid
// of quick-setting tiles — night mode, ghost-cleanup refresh, reading
// orientation, and reader touch controls on/off. The
// frontlight controls are always there: they are what the panel is for. Pure
// 1-bit: no dithered fills, selection reads as a filled tile. The grabber sits
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

  // Quick-setting tiles, in grid order (2 columns): night mode, refresh,
  // orientation, touch. Fixed set — shown on touch boards, absent elsewhere.
  static constexpr int kTileCount = 4;

  // fui::SliderRowProps and fui::TileGridProps embed a 324-byte fui::StyleSet,
  // so the props the render path fills in live here instead of on the stack
  // (AGENTS.md: locals stay under 256 bytes). The components take them by
  // const reference and draw immediately, so one instance per call site is
  // enough — every field either is reassigned on each use or keeps its
  // constructed default.
  freeink::ui::SliderRowProps rowProps;
  freeink::ui::TileGridProps gridProps;
  freeink::ui::TileGridItem gridItems[kTileCount];

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
  // light (FrontlightConfig::loadProbeMs) leaves the light off and shows why.
  // One string of a colour-select light is missing (a single-colour light): the
  // warmth row stays on the other string, says so, and ignores input.
  // Without FREEINK_CAP_FRONTLIGHT_PROBE neither can happen.
#if FREEINK_CAP_FRONTLIGHT_PROBE
  void noteSwitchOnResult();
  bool warmthLocked() const;
#else
  void noteSwitchOnResult() {}
  bool warmthLocked() const { return false; }
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
};

#if CROSSPOINT_TEST_SUITE
#include <BoardConfig.h>

#if !FREEINK_CAP_TOUCH
// No touch in this build (FREEINK_CAP_TOUCH=0, the Silkscreen 3.68" variant): T11 is a screen that says so.
#include <Memory.h>

#include "NoTouchTest.h"
#include "TestFactories.h"

namespace testsuite {

std::unique_ptr<Activity> createTouchMappingTest(const TestInfo& info, GfxRenderer& renderer,
                                                 MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<NoTouchTest>(info, renderer, mappedInput);
}

}  // namespace testsuite
#else
// T11: touch probe, mapping and gestures (bring-up B9). The profile's mounting
// (swapXY/flipX/flipY) and raw ranges are a guess until measured. Calibrate
// takes the controller's raw coordinates at five on-screen targets and fits
// the mounting and the ranges (a straight line per panel axis), applies them in
// RAM through InputManager::setTouchMapping() and has the targets tapped again
// to check them; the result goes to results.csv for the SDK profile. Up/Down
// try the eight swap/flip combinations by hand. While targets or gestures are
// on, every contact is the test's: the touch buttons do nothing and only the
// keys drive the screen (the mapping may still be wrong). The probe's chip-id
// check (TouchConfig::requireFocalTechId) and whether the build has the GT911
// fallback (alternate jumpers) are shown and logged.
#include <Arduino.h>
#include <BoardConfig.h>
#include <GfxRenderer.h>
#include <HalGPIO.h>
#include <InputManager.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <utility>

#include "BootProbes.h"
#include "MappedInputManager.h"
#include "TestFactories.h"
#include "TestNames.h"
#include "TextBuffer.h"
#include "activities/testsuite/TestCatalog.h"
#include "activities/testsuite/TestScreen.h"
#include "activities/testsuite/TestSuiteAccess.h"
#include "fontIds.h"

namespace testsuite {
namespace {

using Mapping = InputManager::TouchMapping;

constexpr int TARGETS = 5;
constexpr int TARGET_INSET = 44;  // target centre from the body band's edges, px
constexpr int TARGET_ARM = 16;    // crosshair half-length, px
constexpr int TOLERANCE_PX = 24;  // every target, per axis (proposal)
constexpr int TRAIL_MAX = 96;
constexpr int TRAIL_STEP_PX = 6;     // a drag point is kept once it moved this far
constexpr int DRAG_MIN_POINTS = 8;   // a drag, not a stray contact
constexpr int DRAG_MAX_GAP_PX = 40;  // between two samples of a slow drag (proposal)
// Poll and loop granularity around the long-press threshold: the loop stamps
// the touch-down a little after the SDK does.
constexpr unsigned long LONG_PRESS_EARLY_MS = 20;
constexpr unsigned long LONG_PRESS_LATE_MS = 150;
// A fit is trusted when each panel axis follows one raw axis in a straight
// line over a real part of the controller's range.
constexpr float FIT_MIN_CORRELATION = 0.95f;
constexpr float FIT_MIN_SPAN = 100.0f;  // raw units across the panel
constexpr float RAW_LIMIT = 4095.0f;    // 12-bit controller coordinates

constexpr int QUESTION_TAIL = 1;
constexpr const char* TAIL_ANSWERS[] = {"No tail on J4", "A tail is fitted"};
constexpr char TAIL_HEADING[] = "Touch tail";
constexpr char TAIL_BODY[] = "No touch controller answered at boot. Is a touch tail (-T01C/-FT01C) fitted on J4?";

constexpr char INTRO[] =
    "Confirm: calibrate at five targets. Left: check the targets with the current mapping. Right: gestures. "
    "Up/Down try the eight swap/flip combinations. A tap here shows where it lands.";
constexpr char TARGET_HINT[] =
    "Tap the boxed target. The touch buttons are off: keys only. Back stops, Confirm starts again.";
constexpr char CALIBRATE_HINT[] = "Calibrating: the raw point of each tap is used, not where it lands now.";
constexpr char GESTURE_HINT[] =
    "Tap, long-press, drag slowly, and swipe left, right, up and down. The touch buttons are off: Confirm records, "
    "Left clears, Back stops.";
constexpr char NO_TOUCH[] =
    "No touch controller answered the probe this boot (FT6336U at 0x38, and a GT911 only in an alternate-jumper "
    "build). Record says whether a tail is fitted: none is a pass (the board runs touchless), a fitted one a "
    "failure.";

enum class Mode : uint8_t { Idle, Calibrate, Verify, Gestures };
// Where the mapping in force came from, when it is not the profile's.
enum class Source : uint8_t { Fitted, ByHand };
enum SwipeIndex : uint8_t { SWIPE_LEFT, SWIPE_RIGHT, SWIPE_UP, SWIPE_DOWN, SWIPES };
constexpr const char* SWIPE_CODES[SWIPES] = {"L", "R", "U", "D"};

struct Point {
  int16_t x = 0;
  int16_t y = 0;
};

struct TargetTap {
  bool taken = false;
  int16_t x = 0;  // where the mapping in force put it (logical px)
  int16_t y = 0;
  uint16_t rawX = 0;  // controller units at touch-down
  uint16_t rawY = 0;
};

struct AxisFit {
  float offset = 0.0f;  // raw at the panel's 0
  float span = 0.0f;    // raw across the panel (negative: the axis runs backwards)
  float correlation = 0.0f;
};

InputManager& touchInput() { return TestSuiteAccess::input(gpio); }

// The mounting and ranges the profile gave, before any change this boot.
const Mapping& profileMapping() {
  static const Mapping mapping = touchInput().touchMapping();
  return mapping;
}

bool sameMapping(const Mapping& a, const Mapping& b) {
  return a.swapXY == b.swapXY && a.flipX == b.flipX && a.flipY == b.flipY && a.rawMinX == b.rawMinX &&
         a.rawMaxX == b.rawMaxX && a.rawMinY == b.rawMinY && a.rawMaxY == b.rawMaxY;
}

// Logical px to the panel's native frame, 0..1: the inverse of GfxRenderer::tapToLogical().
void logicalToNative(const GfxRenderer& renderer, const int x, const int y, float& nx, float& ny) {
  const int width = renderer.getDisplayWidth();
  const int height = renderer.getDisplayHeight();
  int px = x;
  int py = y;
  switch (renderer.getOrientation()) {
    case GfxRenderer::Portrait:
      px = y;
      py = height - 1 - x;
      break;
    case GfxRenderer::PortraitInverted:
      px = width - 1 - y;
      py = x;
      break;
    case GfxRenderer::LandscapeClockwise:
      px = width - 1 - x;
      py = height - 1 - y;
      break;
    case GfxRenderer::LandscapeCounterClockwise:
      break;
  }
  nx = (static_cast<float>(px) + 0.5f) / static_cast<float>(width);
  ny = (static_cast<float>(py) + 0.5f) / static_cast<float>(height);
}

// Raw controller units to the panel's native frame under `m`, as InputManager maps them.
void rawToNative(const Mapping& m, const uint16_t rawX, const uint16_t rawY, float& nx, float& ny) {
  const auto axis = [](const uint16_t raw, const uint16_t lo, const uint16_t hi, const bool flip) {
    float v = hi > lo ? (static_cast<float>(raw) - lo) / static_cast<float>(hi - lo) : 0.0f;
    v = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
    return flip ? 1.0f - v : v;
  };
  nx = axis(m.swapXY ? rawY : rawX, m.rawMinX, m.rawMaxX, m.flipX);
  ny = axis(m.swapXY ? rawX : rawY, m.rawMinY, m.rawMaxY, m.flipY);
}

// Least squares of raw = offset + span * n.
AxisFit fitAxis(const float* n, const float* raw, const int count) {
  float meanN = 0.0f;
  float meanRaw = 0.0f;
  for (int i = 0; i < count; ++i) {
    meanN += n[i];
    meanRaw += raw[i];
  }
  meanN /= static_cast<float>(count);
  meanRaw /= static_cast<float>(count);
  float snn = 0.0f;
  float srr = 0.0f;
  float snr = 0.0f;
  for (int i = 0; i < count; ++i) {
    const float dn = n[i] - meanN;
    const float dr = raw[i] - meanRaw;
    snn += dn * dn;
    srr += dr * dr;
    snr += dn * dr;
  }
  AxisFit fit;
  if (snn <= 0.0f || srr <= 0.0f) return fit;
  fit.span = snr / snn;
  fit.offset = meanRaw - fit.span * meanN;
  fit.correlation = snr / std::sqrt(snn * srr);
  return fit;
}

uint16_t clampRaw(const float value) {
  if (value <= 0.0f) return 0;
  if (value >= RAW_LIMIT) return static_cast<uint16_t>(RAW_LIMIT);
  return static_cast<uint16_t>(std::lround(value));
}

// The range InputManager wants for a fitted axis: min/max along the panel, flipped
// when the raw values fall as the panel coordinate rises.
void rangeFrom(const AxisFit& fit, bool& flip, uint16_t& lo, uint16_t& hi) {
  flip = fit.span < 0.0f;
  const float atStart = fit.offset;
  const float atEnd = fit.offset + fit.span;
  lo = clampRaw(flip ? atEnd : atStart);
  hi = clampRaw(flip ? atStart : atEnd);
}

class TouchMappingTest final : public TestScreen {
 public:
  TouchMappingTest(const TestInfo& info, GfxRenderer& renderer, MappedInputManager& mappedInput)
      : TestScreen(info, renderer, mappedInput) {}

 private:
  void onStart() override {
    active = gpio.hasTouch();
    diag = touchInput().touchDiagnostics();
    if (active) profileMapping();  // captured before anything here changes it
  }

  bool touchIsTestInput() const override { return mode != Mode::Idle; }
  bool showsLastResult() const override { return mode != Mode::Calibrate && mode != Mode::Verify; }

  // The bottom-edge swipe is a gesture here, not the way home, while touch is the test's.
  bool handleHomeGesture() override {
    if (mode == Mode::Idle) return false;
    if (mode == Mode::Gestures) {
      ++swipes[SWIPE_UP];
      lastEvent = "swipe up (bottom edge)";
      requestUpdate();
    }
    return true;
  }

  const char* slotLabel(const Slot slot) const override {
    if (!active) return slot == Slot::Confirm ? "Record" : nullptr;
    switch (mode) {
      case Mode::Idle:
        return slot == Slot::Confirm ? "Calibrate" : slot == Slot::Left ? "Check" : "Gestures";
      case Mode::Calibrate:
        return slot == Slot::Confirm ? "Restart" : slot == Slot::Left ? "Profile" : nullptr;
      case Mode::Verify:
        return slot == Slot::Confirm ? "Restart" : nullptr;
      case Mode::Gestures:
        return slot == Slot::Confirm ? "Record" : slot == Slot::Left ? "Clear" : nullptr;
    }
    return nullptr;
  }

  void onSlot(const Slot slot) override {
    if (!active) {
      if (slot == Slot::Confirm) ask(QUESTION_TAIL, TAIL_HEADING, TAIL_BODY, TAIL_ANSWERS, 2);
      return;
    }
    switch (mode) {
      case Mode::Idle:
        if (slot == Slot::Confirm) startTargets(Mode::Calibrate);
        if (slot == Slot::Left) startTargets(Mode::Verify);
        if (slot == Slot::Right) startGestures();
        return;
      case Mode::Calibrate:
        if (slot == Slot::Confirm) startTargets(Mode::Calibrate);
        if (slot == Slot::Left) {
          touchInput().setTouchMapping(profileMapping());
          fitResidualPx = -1;
          LOG_INF("TEST", "T11: profile touch mapping back in force");
          hint = "The profile's mapping is back in force.";
          setMode(Mode::Idle);
        }
        return;
      case Mode::Verify:
        if (slot == Slot::Confirm) startTargets(Mode::Verify);
        return;
      case Mode::Gestures:
        if (slot == Slot::Confirm) recordGestures();
        if (slot == Slot::Left) startGestures();
        return;
    }
  }

  void onStep(const int direction) override {
    if (!active || mode != Mode::Idle) return;
    Mapping m = touchInput().touchMapping();
    const int index = ((m.swapXY ? 4 : 0) | (m.flipX ? 2 : 0) | (m.flipY ? 1 : 0)) + direction;
    const int next = (index % 8 + 8) % 8;
    const bool swap = (next & 4) != 0;
    // The ranges describe the axes after the swap, so they follow the raw axis.
    if (swap != m.swapXY) {
      std::swap(m.rawMinX, m.rawMinY);
      std::swap(m.rawMaxX, m.rawMaxY);
    }
    m.swapXY = swap;
    m.flipX = (next & 2) != 0;
    m.flipY = (next & 1) != 0;
    touchInput().setTouchMapping(m);
    source = Source::ByHand;
    fitResidualPx = -1;
    LOG_INF("TEST", "T11: mapping swapXY=%d flipX=%d flipY=%d (by hand)", m.swapXY, m.flipX, m.flipY);
    hint = "Mapping changed by hand (RAM). Tap to see where it lands; Left checks the targets.";
    requestUpdate();
  }

  bool onBack() override {
    if (mode == Mode::Idle) return false;
    hint = nullptr;
    setMode(Mode::Idle);
    return true;
  }

  void onAnswer(const int question, const int answer) override {
    if (question != QUESTION_TAIL || answer < 0) return;
    const bool fitted = answer == 1;
    char touchId[24];
    formatTouchIdToken(touchId, sizeof(touchId));
    record(fitted ? Verdict::Fail : Verdict::Pass,
           "touch=absent tail=%s probe_ran=%d cached=%d resets=%u touch_id=%s gt911_fallback=%d fails=%s",
           fitted ? "fitted" : "none", diag.probeRan ? 1 : 0, diag.probeCached ? 1 : 0,
           static_cast<unsigned>(diag.resetPulses), touchId, gt911Fallback() ? 1 : 0,
           fitted ? "tail_not_found" : "none");
  }

  void setMode(const Mode next) {
    mode = next;
    requestUpdate();
  }

  void startTargets(const Mode next) {
    for (auto& tap : taps) tap = TargetTap{};
    nextTarget = 0;
    hint = next == Mode::Calibrate ? CALIBRATE_HINT : nullptr;
    LOG_INF("TEST", "T11: %s at %d targets", next == Mode::Calibrate ? "calibration" : "check", TARGETS);
    setMode(next);
  }

  void startGestures() {
    taps0 = 0;
    longPresses = 0;
    lastLongPressMs = 0;
    for (auto& count : swipes) count = 0;
    drags = 0;
    dragGapPx = 0;
    trailCount = 0;
    lastEvent = nullptr;
    setMode(Mode::Gestures);
  }

  void onTick() override {
    if (!active) return;
    float nx = 0.0f;
    float ny = 0.0f;
    // The raw point of the contact's first frame: the tap's position is its touch-down.
    if (gpio.wasTouchDown(nx, ny)) {
      const auto d = touchInput().touchDiagnostics();
      downRawValid = d.rawValid;
      downRawX = d.rawX;
      downRawY = d.rawY;
      downAt = millis();
      if (mode == Mode::Gestures) {
        int x = 0;
        int y = 0;
        renderer.tapToLogical(nx, ny, x, y);
        trailCount = 0;
        trailGapPx = 0;
        addTrailPoint(x, y, true);
      }
    }
    switch (mode) {
      case Mode::Idle:
        idleTouch();
        return;
      case Mode::Calibrate:
      case Mode::Verify:
        targetTouch();
        return;
      case Mode::Gestures:
        gestureTouch();
        return;
    }
  }

  void idleTouch() {
    float nx = 0.0f;
    float ny = 0.0f;
    if (!gpio.wasTouchTap(nx, ny)) return;
    int x = 0;
    int y = 0;
    renderer.tapToLogical(nx, ny, x, y);
    lastTap = {static_cast<int16_t>(x), static_cast<int16_t>(y)};
    lastTapValid = true;
    diag = touchInput().touchDiagnostics();
    requestUpdate();
  }

  void targetTouch() {
    float nx = 0.0f;
    float ny = 0.0f;
    if (!gpio.wasTouchTap(nx, ny)) {
      float ex = 0.0f;
      float ey = 0.0f;
      if (gpio.wasSwipe(nx, ny, ex, ey)) {
        hint = "That was a swipe: tap the target and lift straight off.";
        requestUpdate();
      }
      return;
    }
    if (!targetsReady.load() || nextTarget >= TARGETS) return;
    int x = 0;
    int y = 0;
    renderer.tapToLogical(nx, ny, x, y);
    diag = touchInput().touchDiagnostics();
    TargetTap& tap = taps[nextTarget];
    tap.taken = true;
    tap.x = static_cast<int16_t>(x);
    tap.y = static_cast<int16_t>(y);
    tap.rawX = downRawValid ? downRawX : diag.rawX;
    tap.rawY = downRawValid ? downRawY : diag.rawY;
    ++nextTarget;
    hint = mode == Mode::Calibrate ? CALIBRATE_HINT : nullptr;
    if (nextTarget < TARGETS) {
      requestUpdate();
      return;
    }
    if (mode == Mode::Calibrate) {
      finishCalibration();
    } else {
      finishCheck();
    }
  }

  void targetPosition(const int i, int& x, int& y) const {
    x = targetX[i].load();
    y = targetY[i].load();
  }

  void finishCalibration() {
    float nX[TARGETS];
    float nY[TARGETS];
    float rawX[TARGETS];
    float rawY[TARGETS];
    for (int i = 0; i < TARGETS; ++i) {
      int x = 0;
      int y = 0;
      targetPosition(i, x, y);
      logicalToNative(renderer, x, y, nX[i], nY[i]);
      rawX[i] = taps[i].rawX;
      rawY[i] = taps[i].rawY;
    }
    // Which raw axis follows which panel axis, then a line for each.
    const float straight =
        std::fabs(fitAxis(nX, rawX, TARGETS).correlation) + std::fabs(fitAxis(nY, rawY, TARGETS).correlation);
    const float crossed =
        std::fabs(fitAxis(nX, rawY, TARGETS).correlation) + std::fabs(fitAxis(nY, rawX, TARGETS).correlation);
    Mapping fitted{};
    fitted.swapXY = crossed > straight;
    const AxisFit fx = fitAxis(nX, fitted.swapXY ? rawY : rawX, TARGETS);
    const AxisFit fy = fitAxis(nY, fitted.swapXY ? rawX : rawY, TARGETS);
    rangeFrom(fx, fitted.flipX, fitted.rawMinX, fitted.rawMaxX);
    rangeFrom(fy, fitted.flipY, fitted.rawMinY, fitted.rawMaxY);
    char raws[64] = "";
    for (int i = 0; i < TARGETS; ++i) {
      appendf(raws, sizeof(raws), "%s%u/%u", i == 0 ? "" : ",", taps[i].rawX, taps[i].rawY);
    }
    const char* why = nullptr;
    if (std::fabs(fx.correlation) < FIT_MIN_CORRELATION || std::fabs(fy.correlation) < FIT_MIN_CORRELATION) {
      why = "not_straight";
    } else if (std::fabs(fx.span) < FIT_MIN_SPAN || std::fabs(fy.span) < FIT_MIN_SPAN) {
      why = "no_spread";
    } else if (!touchInput().setTouchMapping(fitted)) {
      why = "empty_range";
    }
    const int corrX = static_cast<int>(std::lround(std::fabs(fx.correlation) * 1000.0f));
    const int corrY = static_cast<int>(std::lround(std::fabs(fy.correlation) * 1000.0f));
    if (why != nullptr) {
      LOG_ERR("TEST", "T11: calibration failed (%s), raw %s", why, raws);
      hint = "The taps did not line up with the targets: calibrate again, one clean tap per target.";
      record(Verdict::Fail, "fit=failed why=%s corr=%d,%d raw=%s fails=fit_%s", why, corrX, corrY, raws, why);
      setMode(Mode::Idle);
      return;
    }
    fitResidualPx = 0;
    for (int i = 0; i < TARGETS; ++i) {
      float nx = 0.0f;
      float ny = 0.0f;
      rawToNative(fitted, taps[i].rawX, taps[i].rawY, nx, ny);
      int x = 0;
      int y = 0;
      renderer.tapToLogical(nx, ny, x, y);
      int tx = 0;
      int ty = 0;
      targetPosition(i, tx, ty);
      fitResidualPx = std::max(fitResidualPx, std::max(std::abs(x - tx), std::abs(y - ty)));
    }
    source = Source::Fitted;
    LOG_INF("TEST", "T11: fitted swapXY=%d flipX=%d flipY=%d x %u-%u y %u-%u (RAM)", fitted.swapXY, fitted.flipX,
            fitted.flipY, fitted.rawMinX, fitted.rawMaxX, fitted.rawMinY, fitted.rawMaxY);
    record(Verdict::Info, "fit swap=%d flipx=%d flipy=%d rawx=%u-%u rawy=%u-%u corr=%d,%d residual=%dpx raw=%s",
           fitted.swapXY ? 1 : 0, fitted.flipX ? 1 : 0, fitted.flipY ? 1 : 0, fitted.rawMinX, fitted.rawMaxX,
           fitted.rawMinY, fitted.rawMaxY, corrX, corrY, fitResidualPx, raws);
    // Now the same targets through the fitted mapping.
    startTargets(Mode::Verify);
    hint = "Fitted mapping in force (RAM). Tap the targets again to check it.";
  }

  void finishCheck() {
    int worst = 0;
    char errors[64] = "";
    for (int i = 0; i < TARGETS; ++i) {
      int tx = 0;
      int ty = 0;
      targetPosition(i, tx, ty);
      const int dx = taps[i].x - tx;
      const int dy = taps[i].y - ty;
      worst = std::max(worst, std::max(std::abs(dx), std::abs(dy)));
      appendf(errors, sizeof(errors), "%s%d/%d", i == 0 ? "" : ",", dx, dy);
    }
    const Mapping m = touchInput().touchMapping();
    char fails[48] = "";
    if (worst > TOLERANCE_PX) addFailure(fails, sizeof(fails), "off_target");
    if (!diag.active) addFailure(fails, sizeof(fails), "inactive");
    if (diag.probeRan && !diag.probeFound) addFailure(fails, sizeof(fails), "probe");
    record(fails[0] != '\0' ? Verdict::Fail : Verdict::Pass,
           "swap=%d flipx=%d flipy=%d rawx=%u-%u rawy=%u-%u mapping=%s maxerr=%dpx err=%s ctrl=%s@0x%02x id=%s "
           "gt911_fallback=%d fails=%s",
           m.swapXY ? 1 : 0, m.flipX ? 1 : 0, m.flipY ? 1 : 0, m.rawMinX, m.rawMaxX, m.rawMinY, m.rawMaxY,
           mappingName(m), worst, errors, touchControllerName(BoardConfig::ACTIVE.touch.controller), diag.address,
           idText(), gt911Fallback() ? 1 : 0, fails[0] != '\0' ? fails : "none");
    setMode(Mode::Idle);
  }

  // The profile's GT911 fallback (SILKSCREEN_TOUCH_ALT_JUMPERS on Silkscreen).
  static bool gt911Fallback() {
    return BoardConfig::ACTIVE.touch.fallbackController == BoardConfig::TouchController::Gt911;
  }

  // The probe's chip-id check and the GT911 fallback.
  void drawIdentityRows(TestPage& page) const {
    if (!BoardConfig::touchIdentityCheck()) {
      page.row("FocalTech chip-id check", "off");
    } else if (diag.idRejected && diag.idRead) {
      page.rowf("FocalTech chip-id check", "0x%02x at 0x%02x: not FocalTech, left alone", diag.id[0],
                BoardConfig::ACTIVE.touch.i2cAddress);
    } else if (diag.idRejected) {
      page.row("FocalTech chip-id check", "refused before the sleep, left alone");
    } else if (diag.idRead) {
      page.rowf("FocalTech chip-id check", "0x%02x accepted", diag.id[0]);
    } else {
      page.row("FocalTech chip-id check", diag.active ? "n/a (no id read)" : "nothing answered");
    }
    page.row("GT911 fallback", gt911Fallback() ? "on (alternate jumpers)" : "off (default jumpers)");
  }

  // "chip/fw/panel" from the probe's 0xA3/0xA6/0xA8, or "n/a".
  const char* idText() const {
    if (!diag.idRead) return "n/a";
    snprintf(idBuffer, sizeof(idBuffer), "%02x/%02x/%02x", diag.id[0], diag.id[3], diag.id[5]);
    return idBuffer;
  }

  void addTrailPoint(const int x, const int y, const bool first) {
    if (!first && trailCount > 0) {
      const Point& last = trail[trailCount - 1];
      const int gap = std::max(std::abs(x - lastHeld.x), std::abs(y - lastHeld.y));
      trailGapPx = std::max(trailGapPx, gap);
      lastHeld = {static_cast<int16_t>(x), static_cast<int16_t>(y)};
      if (std::max(std::abs(x - last.x), std::abs(y - last.y)) < TRAIL_STEP_PX) return;
    }
    lastHeld = {static_cast<int16_t>(x), static_cast<int16_t>(y)};
    if (trailCount < TRAIL_MAX) trail[trailCount++] = lastHeld;
  }

  void gestureTouch() {
    float nx = 0.0f;
    float ny = 0.0f;
    if (gpio.wasTouchLongPress(nx, ny)) {
      ++longPresses;
      lastLongPressMs = millis() - downAt;
      gpio.suppressTouchContact();  // its lift is not a tap
      lastEvent = "long press";
      requestUpdate();
      return;
    }
    int x = 0;
    int y = 0;
    if (mappedInput.isScreenTouchHeld(x, y)) addTrailPoint(x, y, false);
    switch (mappedInput.wasSwipe()) {
      case MappedInputManager::SwipeDir::Left:
        countSwipe(SWIPE_LEFT, "swipe left");
        return;
      case MappedInputManager::SwipeDir::Right:
        countSwipe(SWIPE_RIGHT, "swipe right");
        return;
      case MappedInputManager::SwipeDir::Up:
        countSwipe(SWIPE_UP, "swipe up");
        return;
      case MappedInputManager::SwipeDir::Down:
        countSwipe(SWIPE_DOWN, "swipe down");
        return;
      case MappedInputManager::SwipeDir::None:
        break;
    }
    if (gpio.wasTouchTap(nx, ny)) {
      ++taps0;
      renderer.tapToLogical(nx, ny, x, y);
      lastTap = {static_cast<int16_t>(x), static_cast<int16_t>(y)};
      lastTapValid = true;
      lastEvent = "tap";
      requestUpdate();
      return;
    }
    if (gpio.wasTouchReleased()) {
      // Neither a tap nor a swipe: a drag, when it covered some ground.
      if (trailCount >= DRAG_MIN_POINTS) {
        ++drags;
        dragGapPx = std::max(dragGapPx, trailGapPx);
        lastEvent = "drag";
      } else {
        lastEvent = "short contact";
      }
      requestUpdate();
    }
  }

  void countSwipe(const SwipeIndex which, const char* name) {
    ++swipes[which];
    lastEvent = name;
    requestUpdate();
  }

  void recordGestures() {
    char fails[96] = "";
    const unsigned long longPressMs = diag.longPressMs;
    if (taps0 == 0) addFailure(fails, sizeof(fails), "no_tap");
    if (longPresses == 0) {
      addFailure(fails, sizeof(fails), "no_long_press");
    } else if (lastLongPressMs + LONG_PRESS_EARLY_MS < longPressMs ||
               lastLongPressMs > longPressMs + LONG_PRESS_LATE_MS) {
      addFailure(fails, sizeof(fails), "long_press_time");
    }
    for (int i = 0; i < SWIPES; ++i) {
      if (swipes[i] == 0) {
        char what[16];
        snprintf(what, sizeof(what), "no_swipe_%s", SWIPE_CODES[i]);
        addFailure(fails, sizeof(fails), what);
      }
    }
    if (drags == 0) addFailure(fails, sizeof(fails), "no_drag");
    if (drags != 0 && dragGapPx > DRAG_MAX_GAP_PX) addFailure(fails, sizeof(fails), "drag_gaps");
    record(fails[0] != '\0' ? Verdict::Fail : Verdict::Pass,
           "taps=%u long=%u@%lums swipes=L%u/R%u/U%u/D%u drags=%u gap=%dpx fails=%s", taps0, longPresses,
           lastLongPressMs, swipes[SWIPE_LEFT], swipes[SWIPE_RIGHT], swipes[SWIPE_UP], swipes[SWIPE_DOWN], drags,
           dragGapPx, fails[0] != '\0' ? fails : "none");
  }

  // --- drawing (render task) --------------------------------------------------

  void layoutTargets(const TestPage& page) const {
    if (targetsReady.load()) return;
    const int left = page.left() + TARGET_INSET;
    const int right = page.left() + page.pageWidth() - TARGET_INSET;
    const int top = page.topY() + TARGET_INSET;
    const int bottom = page.bottomY() - TARGET_INSET;
    const int xs[TARGETS] = {left, right, right, left, (left + right) / 2};
    const int ys[TARGETS] = {top, top, bottom, bottom, (top + bottom) / 2};
    for (int i = 0; i < TARGETS; ++i) {
      targetX[i] = static_cast<int16_t>(xs[i]);
      targetY[i] = static_cast<int16_t>(ys[i]);
    }
    targetsReady = true;
  }

  void drawCross(const int x, const int y, const int arm, const int width) const {
    renderer.drawLine(x - arm, y, x + arm, y, width, true);
    renderer.drawLine(x, y - arm, x, y + arm, width, true);
  }

  void drawTargets() const {
    for (int i = 0; i < TARGETS; ++i) {
      int x = 0;
      int y = 0;
      targetPosition(i, x, y);
      drawCross(x, y, TARGET_ARM, 2);
      if (i == nextTarget) {
        renderer.drawRect(x - TARGET_ARM - 6, y - TARGET_ARM - 6, 2 * (TARGET_ARM + 6), 2 * (TARGET_ARM + 6), 3, true);
      }
      if (taps[i].taken) {
        // Where the mapping in force put the tap.
        renderer.fillRect(taps[i].x - 3, taps[i].y - 3, 7, 7, true);
        renderer.drawLine(x, y, taps[i].x, taps[i].y, 1, true);
      }
    }
  }

  void drawTrail() const {
    for (int i = 1; i < trailCount; ++i) {
      renderer.drawLine(trail[i - 1].x, trail[i - 1].y, trail[i].x, trail[i].y, 3, true);
    }
  }

  const char* mappingName(const Mapping& m) const {
    if (sameMapping(m, profileMapping())) return "profile";
    return source == Source::Fitted ? "fitted" : "by_hand";
  }

  void drawMappingRows(TestPage& page) const {
    const Mapping m = touchInput().touchMapping();
    page.rowf("Mapping", "swap %d, flipX %d, flipY %d (%s)", m.swapXY ? 1 : 0, m.flipX ? 1 : 0, m.flipY ? 1 : 0,
              mappingName(m));
    page.rowf("Raw range X / Y", "%u-%u / %u-%u", m.rawMinX, m.rawMaxX, m.rawMinY, m.rawMaxY);
  }

  void drawBody(TestPage& page) const override {
    if (!active) {
      page.paragraph(NO_TOUCH, 5);
      page.gap(4);
      page.setCompact(true);
      page.rowf("Probe ran / cached", "%s / %s", diag.probeRan ? "yes" : "no", diag.probeCached ? "yes" : "no");
      page.rowf("Reset pulses", "%u", static_cast<unsigned>(diag.resetPulses));
      drawIdentityRows(page);
      return;
    }
    layoutTargets(page);
    switch (mode) {
      case Mode::Idle:
        drawIdle(page);
        return;
      case Mode::Calibrate:
      case Mode::Verify:
        drawTargetMode(page);
        return;
      case Mode::Gestures:
        drawGestures(page);
        return;
    }
  }

  void drawIdle(TestPage& page) const {
    page.paragraph(INTRO, 5);
    if (hint != nullptr) page.paragraph(hint, 2);
    page.gap(4);
    page.setCompact(true);
    const auto& t = BoardConfig::ACTIVE.touch;
    page.rowf("Controller", "%s at 0x%02x", touchControllerName(t.controller), diag.address);
    page.rowf("Probe", "%s%s%s",
              diag.probeFound ? "found"
              : diag.probeRan ? "not found"
                              : "not optional",
              diag.probeCached ? ", RTC cache" : "", diag.probeOnFallback ? ", fallback" : "");
    if (diag.idRead) {
      page.rowf("Chip / firmware / panel id", "0x%02x / 0x%02x / 0x%02x", diag.id[0], diag.id[3], diag.id[5]);
    } else {
      page.row("Chip / firmware / panel id", "n/a (FocalTech probe only)");
    }
    page.rowf("Reset pulses this boot", "%u", static_cast<unsigned>(diag.resetPulses));
    drawIdentityRows(page);
    drawMappingRows(page);
    if (diag.rawValid) {
      page.rowf("Last contact, raw", "%u, %u (%u frames)", diag.rawX, diag.rawY, static_cast<unsigned>(diag.rawFrames));
    } else {
      page.row("Last contact, raw", "none yet: touch the panel");
    }
    if (lastTapValid) {
      page.rowf("Last tap lands at", "%d, %d px", lastTap.x, lastTap.y);
      drawCross(lastTap.x, lastTap.y, 12, 3);
    }
    page.rowf("Long press after", "%u ms", diag.longPressMs);
  }

  void drawTargetMode(TestPage& page) const {
    drawTargets();
    // Below the top targets, clear of the centre one.
    page.gap(TARGET_INSET + TARGET_ARM + 10);
    page.heading(mode == Mode::Calibrate ? "Calibrate" : "Check the mapping");
    page.paragraph(TARGET_HINT, 3);
    if (hint != nullptr) page.paragraph(hint, 2);
    page.setCompact(true);
    page.rowf("Target", "%d of %d", nextTarget < TARGETS ? nextTarget + 1 : TARGETS, TARGETS);
    if (downRawValid) page.rowf("Raw at the last touch-down", "%u, %u", downRawX, downRawY);
    drawMappingRows(page);
    if (mode == Mode::Verify && fitResidualPx >= 0) page.rowf("Fit residual", "%d px", fitResidualPx);
  }

  void drawGestures(TestPage& page) const {
    drawTrail();
    page.paragraph(GESTURE_HINT, 3);
    page.gap(4);
    page.setCompact(true);
    page.rowf("Taps", "%u", taps0);
    page.rowf("Long presses (after)", "%u (%lu ms; %u expected)", longPresses, lastLongPressMs, diag.longPressMs);
    page.rowf("Swipes L / R / U / D", "%u / %u / %u / %u", swipes[SWIPE_LEFT], swipes[SWIPE_RIGHT], swipes[SWIPE_UP],
              swipes[SWIPE_DOWN]);
    page.rowf("Drags (largest gap)", "%u (%d px; up to %d)", drags, dragGapPx, DRAG_MAX_GAP_PX);
    page.row("Last", lastEvent != nullptr ? lastEvent : "-");
  }

  bool active = false;
  Mode mode = Mode::Idle;
  InputManager::TouchDiagnostics diag{};
  const char* hint = nullptr;
  Source source = Source::ByHand;
  int fitResidualPx = -1;  // the last fit's worst target, px; -1: the mapping in force is not a fit
  // Target centres, laid out by the first render and read by the loop task.
  mutable std::atomic<int16_t> targetX[TARGETS] = {};
  mutable std::atomic<int16_t> targetY[TARGETS] = {};
  mutable std::atomic<bool> targetsReady{false};
  TargetTap taps[TARGETS];
  int nextTarget = 0;
  bool downRawValid = false;
  uint16_t downRawX = 0;
  uint16_t downRawY = 0;
  unsigned long downAt = 0;
  Point lastTap;
  bool lastTapValid = false;
  // Gestures.
  unsigned taps0 = 0;
  unsigned longPresses = 0;
  unsigned long lastLongPressMs = 0;
  unsigned swipes[SWIPES] = {};
  unsigned drags = 0;
  int dragGapPx = 0;
  const char* lastEvent = nullptr;
  Point trail[TRAIL_MAX];
  int trailCount = 0;
  int trailGapPx = 0;
  Point lastHeld;
  mutable char idBuffer[12] = "";
};

}  // namespace

std::unique_ptr<Activity> createTouchMappingTest(const TestInfo& info, GfxRenderer& renderer,
                                                 MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<TouchMappingTest>(info, renderer, mappedInput);
}

}  // namespace testsuite
#endif  // FREEINK_CAP_TOUCH
#endif

#if CROSSPOINT_TEST_SUITE
// T7: orientation and bezel ruler (bring-up B2). The whole screen, header and
// touch buttons included, is drawn in each of the four orientations with an
// arrow toward the logical bottom; the operator judges each. Every edge carries
// a staircase of 1 px steps, one pixel deeper each, so the first step that shows
// whole is the bezel inset on that edge; in the Insets mode (portrait, the frame
// BoardConfig's viewableInsets uses) the operator enters the four and they are
// logged for the SDK profile. The renderer's orientation is set per render and
// put back to portrait on exit.
#include <Arduino.h>
#include <BoardConfig.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstdio>

#include "TestFactories.h"
#include "activities/testsuite/TestCatalog.h"
#include "activities/testsuite/TestScreen.h"
#include "fontIds.h"

namespace testsuite {
namespace {

constexpr int QUESTION_UPRIGHT = 1;
constexpr int EDGE_BAND = 40;    // px along each edge kept for the ruler
constexpr int RULER_STEPS = 21;  // depths 0-20 px
constexpr int STEP_PITCH = 14;   // px between steps along the edge
constexpr int STEP_LENGTH = 10;
constexpr int LABEL_EVERY = 5;
constexpr int LABEL_DEPTH = RULER_STEPS + 1;  // px from the edge to a label, clear of the deepest step

struct View {
  GfxRenderer::Orientation orientation;
  const char* name;
  const char* key;
  const char* arrow;  // where the arrow points with the board upright, keys at the bottom
};

// The logical bottom follows GfxRenderer's rotation: Landscape CW is the board
// turned a quarter clockwise, so the arrow points at the board's right edge.
constexpr View VIEWS[] = {
    {GfxRenderer::Portrait, "Portrait", "portrait", "down, at the front keys"},
    {GfxRenderer::LandscapeClockwise, "Landscape CW", "landscape_cw", "right"},
    {GfxRenderer::PortraitInverted, "Portrait inverted", "inverted", "up, away from the keys"},
    {GfxRenderer::LandscapeCounterClockwise, "Landscape CCW", "landscape_ccw", "left"},
};
constexpr int VIEW_COUNT = static_cast<int>(sizeof(VIEWS) / sizeof(VIEWS[0]));

constexpr const char* EDGE_NAMES[] = {"Top", "Right", "Bottom", "Left"};
constexpr const char* UPRIGHT_ANSWERS[] = {"Yes", "No"};

constexpr char INSETS_INTRO[] =
    "Each edge has a staircase of 1 px steps, each a pixel deeper than the last, labelled every 5 px. For each "
    "edge enter the depth of the first step that shows whole: +1, Up or Down change it, Edge picks the next "
    "edge, Record logs all four. Back returns to the orientations.";

class OrientationTest final : public TestScreen {
 public:
  OrientationTest(const TestInfo& info, GfxRenderer& renderer, MappedInputManager& mappedInput)
      : TestScreen(info, renderer, mappedInput) {}

 private:
  void onStart() override {
    const auto& insets = BoardConfig::ACTIVE.viewableInsets;
    profile[0] = insets.top;
    profile[1] = insets.right;
    profile[2] = insets.bottom;
    profile[3] = insets.left;
    std::copy(std::begin(profile), std::end(profile), std::begin(measured));
    requestCleanRepaint();
  }

  void onExit() override {
    // The ActivityManager holds the RenderLock here.
    renderer.setOrientation(GfxRenderer::Portrait);
    renderer.promoteNextRefresh(HalDisplay::HALF_REFRESH);
    TestScreen::onExit();
  }

  void render(RenderLock&& lock) override {
    renderer.setOrientation(insetsMode ? GfxRenderer::Portrait : VIEWS[view].orientation);
    TestScreen::render(std::move(lock));
  }

  EdgeBands edgeBands() const override { return {EDGE_BAND, EDGE_BAND, EDGE_BAND, EDGE_BAND}; }

  const char* slotLabel(const Slot slot) const override {
    switch (slot) {
      case Slot::Confirm:
        return insetsMode ? "Record" : "Judge";
      case Slot::Left:
        return insetsMode ? "Edge" : "Rotate";
      case Slot::Right:
        return insetsMode ? "+1 px" : "Insets";
      default:
        return nullptr;
    }
  }

  void onSlot(const Slot slot) override {
    if (insetsMode) {
      if (slot == Slot::Confirm) recordInsets();
      if (slot == Slot::Left) {
        edge = (edge + 1) % 4;
        requestUpdate();
      }
      if (slot == Slot::Right) {
        measured[edge] = static_cast<uint8_t>((measured[edge] + 1) % RULER_STEPS);
        requestUpdate();
      }
      return;
    }
    if (slot == Slot::Confirm) judge();
    if (slot == Slot::Left) showView(view + 1);
    if (slot == Slot::Right) {
      insetsMode = true;
      repaintClean();
    }
  }

  void onStep(const int direction) override {
    if (insetsMode) {
      measured[edge] = static_cast<uint8_t>(std::clamp(measured[edge] + direction, 0, RULER_STEPS - 1));
      requestUpdate();
      return;
    }
    showView(view - direction);
  }

  bool onBack() override {
    if (!insetsMode) return false;
    insetsMode = false;
    repaintClean();
    return true;
  }

  // A new layout: a HALF clears the old one, so the ruler reads cleanly.
  void repaintClean() {
    requestCleanRepaint();
    requestUpdate();
  }

  void showView(const int next) {
    view = (next % VIEW_COUNT + VIEW_COUNT) % VIEW_COUNT;
    repaintClean();
  }

  void judge() {
    snprintf(question, sizeof(question),
             "Board upright, keys at the bottom: does the arrow point %s? Turned so the arrow points down, does the "
             "text read upright?",
             VIEWS[view].arrow);
    ask(QUESTION_UPRIGHT, VIEWS[view].name, question, UPRIGHT_ANSWERS, 2);
  }

  void onAnswer(const int q, const int answer) override {
    if (q != QUESTION_UPRIGHT || answer < 0) return;
    judged[view] = answer == 0 ? 1 : 0;
    LOG_INF("TEST", "T7 %s: %s", VIEWS[view].key, answer == 0 ? "upright, arrow as expected" : "WRONG");
    int next = -1;
    for (int i = 1; i <= VIEW_COUNT && next < 0; ++i) {
      if (judged[(view + i) % VIEW_COUNT] < 0) next = (view + i) % VIEW_COUNT;
    }
    if (next < 0) {
      recordOrientations();
      requestUpdate();
      return;
    }
    showView(next);
  }

  void recordOrientations() {
    bool allUpright = true;
    for (const int8_t j : judged) allUpright = allUpright && j == 1;
    record(allUpright ? Verdict::Pass : Verdict::Fail, "%s=%s %s=%s %s=%s %s=%s", VIEWS[0].key,
           judged[0] == 1 ? "ok" : "wrong", VIEWS[1].key, judged[1] == 1 ? "ok" : "wrong", VIEWS[2].key,
           judged[2] == 1 ? "ok" : "wrong", VIEWS[3].key, judged[3] == 1 ? "ok" : "wrong");
  }

  void recordInsets() {
    const bool match = std::equal(std::begin(measured), std::end(measured), std::begin(profile));
    // A reading for the SDK profile (BoardConfig viewableInsets), not a pass/fail.
    record(Verdict::Info, "frame=portrait measured_trbl=%u/%u/%u/%u profile_trbl=%u/%u/%u/%u match=%s", measured[0],
           measured[1], measured[2], measured[3], profile[0], profile[1], profile[2], profile[3], match ? "yes" : "no");
  }

  void drawRulers() const {
    const int width = renderer.getScreenWidth();
    const int height = renderer.getScreenHeight();
    const int span = (RULER_STEPS - 1) * STEP_PITCH + STEP_LENGTH;
    const int x0 = (width - span) / 2;
    const int y0 = (height - span) / 2;
    const int lineHeight = renderer.getLineHeight(SMALL_FONT_ID);
    char label[4];
    for (int depth = 0; depth < RULER_STEPS; ++depth) {
      const int along = depth * STEP_PITCH;
      renderer.fillRect(x0 + along, depth, STEP_LENGTH, 1, true);               // top
      renderer.fillRect(x0 + along, height - 1 - depth, STEP_LENGTH, 1, true);  // bottom
      renderer.fillRect(depth, y0 + along, 1, STEP_LENGTH, true);               // left
      renderer.fillRect(width - 1 - depth, y0 + along, 1, STEP_LENGTH, true);   // right
      if (depth % LABEL_EVERY != 0) continue;
      snprintf(label, sizeof(label), "%d", depth);
      const int textWidth = renderer.getTextWidth(SMALL_FONT_ID, label);
      const int labelX = x0 + along + (STEP_LENGTH - textWidth) / 2;
      const int labelY = y0 + along + (STEP_LENGTH - lineHeight) / 2;
      renderer.drawText(SMALL_FONT_ID, labelX, LABEL_DEPTH, label);
      renderer.drawText(SMALL_FONT_ID, labelX, height - LABEL_DEPTH - lineHeight, label);
      renderer.drawText(SMALL_FONT_ID, LABEL_DEPTH, labelY, label);
      renderer.drawText(SMALL_FONT_ID, width - LABEL_DEPTH - textWidth, labelY, label);
    }
  }

  void drawArrow(const int centerX, const int top, const int bottom) const {
    if (bottom - top < 40) return;
    const int head = std::min(40, (bottom - top) / 2);
    renderer.fillRect(centerX - 5, top, 10, bottom - top - head, true);
    const int xs[] = {centerX - head / 2 - 8, centerX + head / 2 + 8, centerX};
    const int ys[] = {bottom - head, bottom - head, bottom};
    renderer.fillPolygon(xs, ys, 3, true);
  }

  void drawBody(TestPage& page) const override {
    page.setCompact(true);
    if (insetsMode) {
      page.paragraph(INSETS_INTRO, 8);
      page.gap(4);
      for (int i = 0; i < 4; ++i) {
        char label[24];
        snprintf(label, sizeof(label), "%s%s", i == edge ? "> " : "", EDGE_NAMES[i]);
        page.rowf(label, "%u px (profile %u)", measured[i], profile[i]);
      }
    } else {
      char text[160];
      page.rowf("Orientation", "%d/%d  %s", view + 1, VIEW_COUNT, VIEWS[view].name);
      page.rowf("Screen", "%d x %d", renderer.getScreenWidth(), renderer.getScreenHeight());
      snprintf(text, sizeof(text),
               "Board upright, keys at the bottom: the arrow points %s. Turn the board so the arrow points down: the "
               "text reads upright. Judge asks; Rotate or Up/Down change orientation.",
               VIEWS[view].arrow);
      page.paragraph(text, 5);
      int top = 0;
      int right = 0;
      int bottom = 0;
      int left = 0;
      renderer.getOrientedViewableTRBL(&top, &right, &bottom, &left);
      page.rowf("Profile insets T/R/B/L here", "%d / %d / %d / %d px", top, right, bottom, left);
      snprintf(text, sizeof(text), "P %s  CW %s  INV %s  CCW %s",
               judged[0] < 0 ? "-"
               : judged[0]   ? "ok"
                             : "NO",
               judged[1] < 0 ? "-"
               : judged[1]   ? "ok"
                             : "NO",
               judged[2] < 0 ? "-"
               : judged[2]   ? "ok"
                             : "NO",
               judged[3] < 0 ? "-"
               : judged[3]   ? "ok"
                             : "NO");
      page.row("Judged", text);
      drawArrow(page.left() + page.pageWidth() / 2, page.cursorY() + 8, page.bottomY() - 4);
    }
    drawRulers();
  }

  char question[192] = "";  // the ask() body; must outlive the prompt
  int view = 0;
  bool insetsMode = false;
  int edge = 0;
  int8_t judged[VIEW_COUNT] = {-1, -1, -1, -1};
  uint8_t profile[4] = {};
  uint8_t measured[4] = {};
};

}  // namespace

std::unique_ptr<Activity> createOrientationTest(const TestInfo& info, GfxRenderer& renderer,
                                                MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<OrientationTest>(info, renderer, mappedInput);
}

}  // namespace testsuite
#endif

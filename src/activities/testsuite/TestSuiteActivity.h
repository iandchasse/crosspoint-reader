#pragma once

#if CROSSPOINT_TEST_SUITE
#include "TestCatalog.h"
#include "activities/UiListActivity.h"

// Root of the hardware test firmware: the list of tests (TestCatalog), each
// opening its own screen. Stands in for Home, so boot, the home gesture, a
// silent restart and an empty activity stack all come back here. Keys (Up/Down
// or Left/Right move, Confirm opens) and touch (tap a row, swipe to scroll).
class TestSuiteActivity final : public UiListActivity {
 public:
  TestSuiteActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiListActivity("TestSuite", renderer, mappedInput) {}

  void onEnter() override;
  void render(RenderLock&&) override;
  bool isHomeActivity() const override { return true; }

 private:
  int listCount() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onBackButton() override {}  // the root: nothing to go back to
  void drawChrome() override;
  void drawFooter() override;

  freeink::ui::ListItem rows[testsuite::MAX_TESTS]{};
  char labels[testsuite::MAX_TESTS][28] = {};
  bool firstPaint = true;
};
#endif

#pragma once

// Base screen of every hardware test (CROSSPOINT_TEST_SUITE builds only).
#if CROSSPOINT_TEST_SUITE
#include <cstdint>

#include "TestResultLog.h"
#include "activities/Activity.h"
#include "components/UiAppHost.h"

namespace testsuite {

struct TestInfo;

// Top-to-bottom text layout for a test's body, clipped to the band between the
// header and the touch footer. Render task only.
class TestPage {
 public:
  TestPage(const GfxRenderer& renderer, int x, int y, int width, int bottom)
      : renderer(renderer), x(x), y(y), width(width), bottom(bottom) {}

  void paragraph(const char* text, int maxLines = 8);  // wrapped, small UI font
  void heading(const char* text);                      // bold
  void row(const char* label, const char* value);      // label left, value right-aligned
  void rowf(const char* label, const char* valueFmt, ...) __attribute__((format(printf, 3, 4)));
  void gap(int px = 8) { y += px; }

 private:
  const GfxRenderer& renderer;
  const int x;
  int y;
  const int width;
  const int bottom;
};

// One test on one screen: a header with the test's id and title, the last result
// recorded here, a body the test draws, and a touch footer whose four buttons
// mirror the front keys (Back, Confirm, Left, Right), so every test runs from the
// keys or from touch. Up/Down step values. Back (the key, the header's back
// button or the footer's Exit) leaves the test; a test that reads the keys
// itself sets keysAreTestInput(), and then only the Exit button or a 1.5 s Back
// hold leaves. Leaving switches the front light and the radio off.
class TestScreen : public Activity, protected UiAppHost {
 public:
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 protected:
  enum class Slot : uint8_t { Back, Confirm, Left, Right };

  TestScreen(const TestInfo& info, GfxRenderer& renderer, MappedInputManager& mappedInput);

  // --- test contract ---------------------------------------------------------
  virtual void onStart() {}                                      // end of onEnter()
  virtual void onTick() {}                                       // every loop pass, after input
  virtual void onSlot(Slot) {}                                   // Confirm, Left or Right, by key or by touch
  virtual void onStep(int) {}                                    // Up (+1) or Down (-1)
  virtual const char* slotLabel(Slot) const { return nullptr; }  // nullptr: slot unused
  virtual void drawBody(TestPage&) const {}
  virtual bool keysAreTestInput() const { return false; }
  // After the operator confirmed an LED load (confirmLightLoad()).
  virtual void onLightLoadConfirmed() {}

  // --- helpers ---------------------------------------------------------------
  // Logs the result (TestResultLog) and shows it at the top of the screen.
  void record(Verdict verdict, const char* detailsFmt, ...) __attribute__((format(printf, 3, 4)));
  // A front-light test asks this every time before it lights the boost: a
  // Cancel/Confirm prompt that an LED load (J3 panel light or J6 strip) is
  // connected. onLightLoadConfirmed() runs only after Confirm. Returns false,
  // without asking, when the build or the board has no front light.
  bool confirmLightLoad();

  const TestInfo& info;

 private:
  static constexpr freeink::ui::ActionId ACTION_SLOT = 1;
  static constexpr int EXIT_HOLD_MS = 1500;

  static void screenTrampoline(UiScreen& screen, void* user);
  static void slotTrampoline(const freeink::ui::ActionEvent& event, void* user);
  void buildFooter(UiScreen& screen);
  void pressSlot(Slot slot);
  void exitTest();
  void drawResult(TestPage& page) const;

  char title[40] = "";
  char details[160] = "";
  bool hasResult = false;
  Verdict verdict = Verdict::Info;
  LogOutcome outcome = LogOutcome::SerialOnly;
  // Written by the screen function, read by render(); both on the render task.
  int headerBottom = 0;
  int footerTop = 0;
};

}  // namespace testsuite
#endif

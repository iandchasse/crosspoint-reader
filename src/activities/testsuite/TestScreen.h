#pragma once

// Base screen of every hardware test (CROSSPOINT_TEST_SUITE builds only).
#if CROSSPOINT_TEST_SUITE
#include <atomic>
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
      : renderer(renderer), x(x), top(y), y(y), width(width), bottom(bottom) {}

  void paragraph(const char* text, int maxLines = 8);  // wrapped, small UI font
  void heading(const char* text);                      // bold
  void row(const char* label, const char* value);      // label left, value right-aligned
  void rowf(const char* label, const char* valueFmt, ...) __attribute__((format(printf, 3, 4)));
  void gap(int px = 8) { y += px; }
  // Rows in the small UI font from here on, for a screen with many readings.
  void setCompact(bool on) { compact = on; }
  // The page's frame and where the next line goes, for graphics below the text.
  int left() const { return x; }
  int topY() const { return top; }
  int cursorY() const { return y; }
  int pageWidth() const { return width; }
  int bottomY() const { return bottom; }

 private:
  const GfxRenderer& renderer;
  const int x;
  const int top;
  int y;
  const int width;
  const int bottom;
  bool compact = false;
};

// One test on one screen: a header with the test's id and title, the last result
// recorded here, a body the test draws, and a touch footer whose four buttons
// mirror the front keys (Back, Confirm, Left, Right), so every test runs from the
// keys or from touch. Up/Down step values. Back (the key, the header's back
// button or the footer's Exit) leaves the test unless onBack() uses it; a test
// that reads the keys itself sets keysAreTestInput(), and then a front key held
// for ACTION_HOLD_MS does what its footer button does (Back: leave). A test whose
// input is the touch panel sets touchIsTestInput(): the footer is not routed and
// a touch Back (header button, edge swipe) does not leave, so only the keys
// drive the screen. Leaving switches the front light and the radio off.
class TestScreen : public Activity, protected UiAppHost {
 public:
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 protected:
  enum class Slot : uint8_t { Back, Confirm, Left, Right };
  static constexpr unsigned long ACTION_HOLD_MS = 1500;
  // Bands along the screen edges that a test draws itself (T7's rulers): the
  // header, the body and the footer stay inside them.
  struct EdgeBands {
    int top = 0;
    int right = 0;
    int bottom = 0;
    int left = 0;
  };

  TestScreen(const TestInfo& info, GfxRenderer& renderer, MappedInputManager& mappedInput);

  // --- test contract ---------------------------------------------------------
  virtual void onStart() {}                                      // end of onEnter()
  virtual void onTick() {}                                       // every loop pass, after input
  virtual void onSlot(Slot) {}                                   // Confirm, Left or Right, by key or by touch
  virtual void onStep(int) {}                                    // Up (+1) or Down (-1)
  virtual const char* slotLabel(Slot) const { return nullptr; }  // nullptr: slot unused
  virtual void drawBody(TestPage&) const {}
  virtual bool keysAreTestInput() const { return false; }
  // Every contact is the test's (T11), while it returns true: see the class comment.
  virtual bool touchIsTestInput() const { return false; }
  // false: the last result is not drawn above the body (a body that needs the whole band).
  virtual bool showsLastResult() const { return true; }
  // The LED load the operator named in confirmLightLoad().
  enum class LightLoad : uint8_t { J3Panel, J6Strip };
  virtual void onLightLoadConfirmed(LightLoad) {}
  // The operator's answer to ask(): the answer's index, or -1 after Back.
  virtual void onAnswer(int /*question*/, int /*answer*/) {}
  // Back (the key, the header's back button or Exit): true when the test used
  // it (to end a run, or leave a sub-mode) and stays open.
  virtual bool onBack() { return false; }
  virtual EdgeBands edgeBands() const { return {}; }  // none by default

  // --- helpers ---------------------------------------------------------------
  // Logs the result (TestResultLog) and shows it at the top of the screen.
  void record(Verdict verdict, const char* detailsFmt, ...) __attribute__((format(printf, 3, 4)));
  // A front-light test asks this before each lighting session lights the boost
  // from dark (T9's Confirm, one T10 run): which LED load is connected, the J3
  // panel light or a J6 strip, with "no load" first and preselected.
  // onLightLoadConfirmed() runs only after a load is picked. Within a session
  // the light may go dark and relight on the same load without asking again
  // (T10's retry and restore, T9's string switch). Returns false, without
  // asking, when the build or the board has no front light.
  bool confirmLightLoad();
  // Asks for something only the operator can observe (an LED, a meter): up to
  // four answers, picked with Up/Down and Confirm or by a tap. onAnswer() gets
  // `question` back with the answer. The strings must outlive the prompt.
  bool ask(int question, const char* heading, const char* body, const char* const* answers, int count);
  // Milliseconds since the last key edge or touch contact on this screen.
  unsigned long msSinceInput() const;
  // The next repaint of this screen is a HALF refresh, which clears what a
  // display test left on the glass.
  void requestCleanRepaint() { cleanRepaint = true; }

  const TestInfo& info;

 private:
  static constexpr freeink::ui::ActionId ACTION_SLOT = 1;

  static void screenTrampoline(UiScreen& screen, void* user);
  static void slotTrampoline(const freeink::ui::ActionEvent& event, void* user);
  void buildFooter(UiScreen& screen);
  void pressSlot(Slot slot);
  void exitTest();
  void drawResult(TestPage& page) const;
  void splitFailedChecks(Verdict v);

  char title[40] = "";
  // The record as logged, and as drawn: the failed checks (the "fails=" value)
  // on a line of their own, the rest of the details without them. Members of
  // the heap-allocated screen, not locals.
  char details[320] = "";
  char failedChecks[72] = "";
  char shownDetails[320] = "";
  bool hasResult = false;
  Verdict verdict = Verdict::Info;
  LogOutcome outcome = LogOutcome::SerialOnly;
  unsigned long lastInputAt = 0;
  std::atomic<bool> cleanRepaint{false};  // set by the loop task, consumed by render()
  // Written by render() and the screen function, both on the render task.
  EdgeBands bands;
  int headerBottom = 0;
  int footerTop = 0;
};

}  // namespace testsuite
#endif

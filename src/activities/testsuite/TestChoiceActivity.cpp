#if CROSSPOINT_TEST_SUITE
#include "TestChoiceActivity.h"

#include <GfxRenderer.h>
#include <HalDisplay.h>

#include <utility>

namespace testsuite {

// Room for the longest question (8 lines of the dialog's body font at its
// portrait width); the dialog grows to fit and stays inside the screen.
constexpr uint8_t BODY_MAX_LINES = 12;

void TestChoiceActivity::onEnter() {
  Activity::onEnter();
  popup.setHeadlineMaxLines(BODY_MAX_LINES);
  popup.show(heading, body, answers, count, 0, [this](const int index) {
    ActivityResult res;
    res.data = MenuResult{index};
    setResult(std::move(res));
    finish();
  });
  requestUpdate(true);
}

void TestChoiceActivity::render(RenderLock&&) {
  renderer.clearScreen();
  if (popup.processRender(renderer, mappedInput)) return;
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void TestChoiceActivity::loop() {
  if (popup.handleInput(mappedInput, [this] { requestUpdate(); })) return;
  // Dismissed without an answer (Back, or a tap outside the dialog).
  ActivityResult res;
  res.isCancelled = true;
  setResult(std::move(res));
  finish();
}

}  // namespace testsuite
#endif

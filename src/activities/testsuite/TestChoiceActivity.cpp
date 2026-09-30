#if CROSSPOINT_TEST_SUITE
#include "TestChoiceActivity.h"

#include <GfxRenderer.h>
#include <HalDisplay.h>

#include <utility>

namespace testsuite {

void TestChoiceActivity::onEnter() {
  Activity::onEnter();
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

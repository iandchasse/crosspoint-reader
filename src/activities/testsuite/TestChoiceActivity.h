#pragma once

#if CROSSPOINT_TEST_SUITE
#include "activities/Activity.h"
#include "components/OptionPopup.h"

namespace testsuite {

// A question with up to four answers, for an observation only the operator can
// make (TestScreen::ask()). Keys: Up/Down and Confirm, Back backs out; touch: tap
// an answer, or outside to back out. The result carries the answer's index in
// MenuResult::action, or isCancelled. The strings are not copied beyond the
// popup's own labels, so they must outlive the activity (constexpr).
class TestChoiceActivity final : public Activity {
 public:
  TestChoiceActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const char* heading, const char* body,
                     const char* const* answers, int count)
      : Activity("TestChoice", renderer, mappedInput), heading(heading), body(body), answers(answers), count(count) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  const char* heading;
  const char* body;
  const char* const* answers;
  int count;
  OptionPopup popup;
};

}  // namespace testsuite
#endif

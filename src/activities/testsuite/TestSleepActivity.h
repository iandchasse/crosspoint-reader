#pragma once

#if CROSSPOINT_TEST_SUITE
#include "activities/Activity.h"

// Sleep screen of the hardware test firmware. Draws synchronously from
// onEnter(), like the reader's sleep screen, so enterDeepSleep() finds the panel
// idle; it never opens a book for a cover.
class TestSleepActivity final : public Activity {
 public:
  TestSleepActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const bool fromTimeout)
      : Activity("TestSleep", renderer, mappedInput), fromTimeout(fromTimeout) {}
  void onEnter() override;

 private:
  const bool fromTimeout;
};
#endif

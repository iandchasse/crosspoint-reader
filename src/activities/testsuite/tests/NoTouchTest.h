#pragma once

// T11 and T12 on a build with no touch (FREEINK_CAP_TOUCH=0, the Silkscreen 3.68" variant), which has
// nothing to test: the touch driver is not in the image, so its tests are not either. The screen says so
// and records a SKIP when it opens, so results.csv has a row for the test and the report reads "no touch
// in this build" instead of a failure (CROSSPOINT_TEST_SUITE builds only).
#if CROSSPOINT_TEST_SUITE
#include "activities/testsuite/TestCatalog.h"
#include "activities/testsuite/TestScreen.h"

namespace testsuite {

class NoTouchTest final : public TestScreen {
 public:
  NoTouchTest(const TestInfo& info, GfxRenderer& renderer, MappedInputManager& mappedInput)
      : TestScreen(info, renderer, mappedInput) {}

 private:
  static constexpr char HEADING[] = "No touch in this build";
  static constexpr char BODY[] =
      "This image is built with FREEINK_CAP_TOUCH=0, the Silkscreen 3.68\" variant's: it has no touch tail and "
      "the touch driver is not linked, so there is nothing to probe, map or put to sleep. The keys are the whole "
      "input.";
  static constexpr char BENCH[] =
      "On the bench: a logic analyser on SDA/SCL (IO38, IO39) at boot and at each sleep entry must show no "
      "transaction to 0x38, 0x5D or 0x14, and IO11 (TP_RST) and IO41 (TP_INT) stay quiet.";

  void onStart() override { record(Verdict::Skip, "touch=none reason=no_touch_in_build cap_touch=0 fails=none"); }

  void drawBody(TestPage& page) const override {
    page.heading(HEADING);
    page.gap(4);
    page.paragraph(BODY, 6);
    page.gap(8);
    page.paragraph(BENCH, 5);
  }
};

}  // namespace testsuite
#endif

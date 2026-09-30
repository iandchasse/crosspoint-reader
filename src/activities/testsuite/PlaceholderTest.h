#pragma once

#if CROSSPOINT_TEST_SUITE
#include "TestCatalog.h"
#include "TestScreen.h"
#include "TestStrings.h"

namespace testsuite {

// Opened for a catalog entry whose test is not implemented yet: says so, runs
// nothing and records nothing.
class PlaceholderTest final : public TestScreen {
 public:
  PlaceholderTest(const TestInfo& info, GfxRenderer& renderer, MappedInputManager& mappedInput)
      : TestScreen(info, renderer, mappedInput) {}

 private:
  void drawBody(TestPage& page) const override {
    page.paragraph(info.summary, 3);
    page.gap(16);
    page.heading(text::NOT_IMPLEMENTED);
    page.paragraph(text::NOT_IMPLEMENTED_BODY, 4);
  }
};

}  // namespace testsuite
#endif

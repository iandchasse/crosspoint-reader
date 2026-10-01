#if CROSSPOINT_TEST_SUITE
// OPT: what the operator says about the board under test (BoardOptions.h), for
// the checks the firmware cannot make itself: a board shortened at its cut line,
// and UP(2) reworked into the power button. Each change is saved to
// /.silkscreen-test/options.cfg at once and logged as an INFO row.
#include <HalStorage.h>
#include <Memory.h>

#include "TestFactories.h"
#include "activities/testsuite/BoardOptions.h"
#include "activities/testsuite/TestCatalog.h"
#include "activities/testsuite/TestScreen.h"

namespace testsuite {
namespace {

constexpr char INTRO[] =
    "Set these to match the board before running the tests. They are kept on the card "
    "(/.silkscreen-test/options.cfg) and apply from now on.";
constexpr char CUT_DOWN_NOTE[] =
    "Cut-down board: shortened at the cut line, so J6 and SW10 are gone. P2's J6 scan and T9's J6 strip are N/A, "
    "and UP(2) is POWER by default.";
constexpr char UP2_POWER_NOTE[] =
    "UP(2) is POWER: R72/R74 fitted, R36/R73 removed (HARDWARE.md section 9.2). T3 counts SW7 as POWER on IO18, and "
    "the UP(2) window on IO4 must never fire.";

class BoardOptionsTest final : public TestScreen {
 public:
  BoardOptionsTest(const TestInfo& info, GfxRenderer& renderer, MappedInputManager& mappedInput)
      : TestScreen(info, renderer, mappedInput) {}

 private:
  const char* slotLabel(const Slot slot) const override {
    switch (slot) {
      case Slot::Confirm:
        return "Cut-down";
      case Slot::Left:
        return "UP(2)=PWR";
      default:
        return nullptr;
    }
  }

  void onSlot(const Slot slot) override {
    bool saved = false;
    if (slot == Slot::Confirm) {
      saved = options::setCutDown(!options::cutDown());
    } else if (slot == Slot::Left) {
      saved = options::setUp2IsPower(!options::up2IsPower());
    } else {
      return;
    }
    char token[32];
    options::formatToken(token, sizeof(token));
    record(Verdict::Info, "%s saved=%s", token, saved ? "card" : "ram_only");
  }

  void drawBody(TestPage& page) const override {
    page.paragraph(INTRO, 3);
    page.gap(4);
    page.row("Cut-down board (Confirm)", options::cutDown() ? "YES" : "no");
    page.row("UP(2) is POWER (Left)", options::up2IsPower() ? "YES" : "no");
    page.row("Kept", options::onCard() ? "on the card"
                     : Storage.ready() ? "not saved yet: defaults"
                                       : "SD not mounted: until restart");
    page.gap(8);
    page.paragraph(CUT_DOWN_NOTE, 4);
    page.gap(4);
    page.paragraph(UP2_POWER_NOTE, 4);
  }
};

}  // namespace

std::unique_ptr<Activity> createBoardOptionsTest(const TestInfo& info, GfxRenderer& renderer,
                                                 MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<BoardOptionsTest>(info, renderer, mappedInput);
}

}  // namespace testsuite
#endif

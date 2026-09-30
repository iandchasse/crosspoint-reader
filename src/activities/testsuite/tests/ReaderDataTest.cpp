#if CROSSPOINT_TEST_SUITE
// P8: the reader's own files on the card are left untouched by the test
// firmware. Each boot compares /.crosspoint with the baseline the first boot
// took (tests/ReaderData.h) and records the result; this screen shows the last
// check, runs it again on demand, and replaces the baseline after the reader
// has legitimately changed the files (the operator ran the reader in between).
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include <cstdio>

#include "ReaderData.h"
#include "TestFactories.h"
#include "activities/testsuite/TestCatalog.h"
#include "activities/testsuite/TestScreen.h"

namespace testsuite {
namespace {

constexpr int MAX_ROWS = 12;
constexpr int QUESTION_REBASELINE = 1;
constexpr const char* REBASELINE_ANSWERS[] = {"Cancel", "Take a new baseline"};
constexpr char REBASELINE_HEADING[] = "New baseline?";
constexpr char REBASELINE_BODY[] =
    "Only after running the reader image in between: the reader changes these files itself. A new baseline hides "
    "any change the test firmware made since the last one.";

constexpr char INTRO[] =
    "Every boot compares the reader's files in /.crosspoint (settings, state, Wi-Fi, recent books, the Quick Resume "
    "frame, and the book-cache folder names) with the baseline the first boot took. Confirm checks now; Right takes "
    "a new baseline.";
constexpr char NO_CARD[] = "SD not mounted: nothing to check.";

class ReaderDataTest final : public TestScreen {
 public:
  ReaderDataTest(const TestInfo& info, GfxRenderer& renderer, MappedInputManager& mappedInput)
      : TestScreen(info, renderer, mappedInput) {}

 private:
  void onStart() override {
    report = makeUniqueNoThrow<readerdata::Report>();
    if (!report) {
      LOG_ERR("TEST", "OOM: reader-data report");
      return;
    }
    // Compared again for the screen; the boot check has recorded this boot's result.
    checked = Storage.ready() && readerdata::check(*report);
  }

  void onExit() override {
    report.reset();
    TestScreen::onExit();
  }

  const char* slotLabel(const Slot slot) const override {
    if (!report || !Storage.ready()) return nullptr;
    switch (slot) {
      case Slot::Confirm:
        return "Check";
      case Slot::Right:
        return "New baseline";
      default:
        return nullptr;
    }
  }

  void onSlot(const Slot slot) override {
    if (!report || !Storage.ready()) return;
    if (slot == Slot::Confirm) runCheck(/*replace=*/false);
    if (slot == Slot::Right) ask(QUESTION_REBASELINE, REBASELINE_HEADING, REBASELINE_BODY, REBASELINE_ANSWERS, 2);
  }

  void onAnswer(const int question, const int answer) override {
    if (question == QUESTION_REBASELINE && answer == 1) runCheck(/*replace=*/true);
  }

  void runCheck(const bool replace) {
    {
      // The render task draws from the report.
      RenderLock lock;
      checked = replace ? readerdata::rebaseline(*report) : readerdata::check(*report);
    }
    if (!checked) return;
    char details[240];
    snprintf(details, sizeof(details), "when=%s ", replace ? "new_baseline" : "on_demand");
    readerdata::describe(*report, details, sizeof(details));
    Verdict verdict = Verdict::Pass;
    if (report->ioError || (!report->baselineCreated && !report->clean())) {
      verdict = Verdict::Fail;
    } else if (report->baselineCreated) {
      verdict = Verdict::Info;
    }
    record(verdict, "%s", details);
  }

  void drawBody(TestPage& page) const override {
    page.paragraph(INTRO, 6);
    page.gap(4);
    if (!Storage.ready()) {
      page.paragraph(NO_CARD, 1);
      return;
    }
    if (!report || !checked) return;
    const readerdata::Report& r = *report;
    page.setCompact(true);
    page.rowf("Baseline", "%s", r.baselineCreated ? "taken now" : r.takenAt);
    if (r.takenBy[0] != '\0') page.row("Taken by", r.takenBy);
    page.rowf("/.crosspoint", "%s%s", r.dirNow ? "present" : "absent",
              r.dirNow == r.dirBaseline ? ""
              : r.dirBaseline           ? " (was present)"
                                        : " (was absent)");
    page.rowf("Files same / changed / missing / new", "%d / %d / %d / %d", r.same, r.changed, r.missing, r.added);
    page.rowf("Folders now / baseline", "%u / %u%s", r.foldersNow, r.foldersBaseline,
              r.foldersSame ? "" : " (names changed)");
    if (r.ioError) page.row("Card", "read or write error");
    if (r.truncated) page.rowf("Compared", "the first %d files only", readerdata::MAX_FILES);
    page.gap(4);
    int rows = 0;
    for (int i = 0; i < r.count && rows < MAX_ROWS; ++i, ++rows) {
      page.rowf(r.checks[i].name, "%lu B, %s", static_cast<unsigned long>(r.checks[i].size),
                readerdata::fileStateName(r.checks[i].state));
    }
    page.setCompact(false);
  }

  std::unique_ptr<readerdata::Report> report;
  bool checked = false;
};

}  // namespace

std::unique_ptr<Activity> createReaderDataTest(const TestInfo& info, GfxRenderer& renderer,
                                               MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<ReaderDataTest>(info, renderer, mappedInput);
}

}  // namespace testsuite
#endif

#if CROSSPOINT_TEST_SUITE
#include "TestSuiteActivity.h"

#include <GfxRenderer.h>
#include <HalFrontlight.h>
#include <HalStorage.h>
#include <Logging.h>

#include <cstdio>
#include <utility>

#include "MappedInputManager.h"
#include "TestResultLog.h"
#include "TestStrings.h"
#include "components/UITheme.h"
#include "tests/LightCheck.h"

namespace fui = freeink::ui;
namespace text = testsuite::text;

namespace {
// The row opened last, so a new menu (the home gesture) keeps the place.
int lastOpened = 0;
}  // namespace

void TestSuiteActivity::onEnter() {
  UiListActivity::onEnter();
  // Nothing outside a front-light test leaves the boost lit, or the trip level lowered (T10).
  if (Frontlight.isOn()) Frontlight.setOn(false);
  testsuite::light::restoreTripMv();
  const int count = listCount();
  for (int i = 0; i < count; ++i) {
    const auto& test = testsuite::testAt(static_cast<size_t>(i));
    snprintf(labels[i], sizeof(labels[i]), "%s  %s", test.id, test.title);
    rows[i].label = labels[i];
    rows[i].subtitle = test.create != nullptr ? test.summary : text::NOT_IMPLEMENTED;
    rows[i].actionValue = static_cast<int16_t>(i);
  }
  if (lastOpened > 0 && lastOpened < count) nav.requestSelection(lastOpened);
}

int TestSuiteActivity::listCount() const { return static_cast<int>(testsuite::testCount()); }

void TestSuiteActivity::activateIndex(const int index) {
  // The row leaves this screen; a lingering flash would gray the next one.
  app.clearTapFlash();
  nav.selected = index;
  lastOpened = index;
  const auto& test = testsuite::testAt(static_cast<size_t>(index));
  auto screen = testsuite::createTest(test, renderer, mappedInput);
  if (!screen) {
    LOG_ERR("TEST", "OOM: %s screen", test.id);
    return;
  }
  startActivityForResult(std::move(screen), nullptr);
}

void TestSuiteActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(
      fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight), 0,
                  static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  const int count = listCount();
  for (int i = 0; i < count; ++i) {
    testsuite::Verdict verdict = testsuite::Verdict::Info;
    rows[i].value = testsuite::lastVerdict(testsuite::testAt(static_cast<size_t>(i)), verdict)
                        ? testsuite::verdictName(verdict)
                        : nullptr;
  }

  fui::ListProps props;
  props.items = rows;
  props.count = static_cast<uint16_t>(count);
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.subtitleText = screen.theme().smallText;
  props.subtitleText.maxLines = 1;
  props.valueInset = 8;
  syncListViewport(screen, props);
  screen.list(props);
}

void TestSuiteActivity::drawChrome() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int width = renderer.getScreenWidth();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, width, metrics.headerHeight}, text::MENU_TITLE, nullptr,
                 /*backButton=*/false);
  GUI.drawSubHeader(renderer, Rect{0, metrics.topPadding + metrics.headerHeight, width, metrics.tabBarHeight},
                    Storage.ready() ? text::RESULTS_ON_CARD : text::SD_NOT_MOUNTED, CROSSPOINT_VERSION);
}

void TestSuiteActivity::drawFooter() {
  const auto hints = mappedInput.mapLabels("", text::OPEN, text::UP, text::DOWN);
  GUI.drawButtonHints(renderer, hints.btn1, hints.btn2, hints.btn3, hints.btn4);
}

void TestSuiteActivity::render(RenderLock&&) {
  // UiListActivity::render(), with a clean first paint over the boot splash.
  renderer.clearScreen();
  drawChrome();
  renderUi();
  for (int pass = 0; activeNav().consumeRebuildNeeded() && pass < 8; ++pass) {
    renderer.clearScreen();
    drawChrome();
    renderUi();
  }
  drawFooter();
  renderer.displayBuffer(firstPaint ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH);
  firstPaint = false;
}
#endif

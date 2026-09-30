#if CROSSPOINT_TEST_SUITE
#include "TestSleepActivity.h"

#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <Logging.h>

#include <cstdio>

#include "SleepRecord.h"
#include "TestStrings.h"
#include "fontIds.h"
#include "network/FirmwareBoardTag.h"

namespace text = testsuite::text;

void TestSleepActivity::onEnter() {
  Activity::onEnter();
  LOG_INF("TEST", "Sleeping (%s)", fromTimeout ? "timeout" : "power button or battery cutoff");
  {
    // Drawn here, outside the render task's per-render polarity and orientation.
    RenderLock lock;
    display.setInverted(false);
    renderer.setOrientation(GfxRenderer::Orientation::Portrait);
    constexpr int MARGIN = 24;
    const int line10 = renderer.getLineHeight(UI_10_FONT_ID);
    int y = 60;
    renderer.clearScreen();
    renderer.drawCenteredText(UI_12_FONT_ID, y, text::ASLEEP, true, EpdFontFamily::BOLD);
    y += renderer.getLineHeight(UI_12_FONT_ID) + 8;
    renderer.drawCenteredText(UI_10_FONT_ID, y, text::WAKE_HINT);
    y += line10;
    renderer.drawCenteredText(UI_10_FONT_ID, y, fromTimeout ? text::SLEEP_FROM_TIMEOUT : text::SLEEP_FROM_REQUEST);
    y += line10;
    const testsuite::SleepPath path = testsuite::pendingSleepPath();
    char line[64];
    if (path != testsuite::SleepPath::None) {
      snprintf(line, sizeof(line), "%s: %s", text::SLEEP_P5_PATH, testsuite::sleepPathTitle(path));
      renderer.drawCenteredText(UI_10_FONT_ID, y, line);
      y += line10;
    }

    y += 24;
    renderer.drawText(UI_10_FONT_ID, MARGIN, y, text::SLEEP_CURRENT_HEADING, true, EpdFontFamily::BOLD);
    y += line10;
    for (const char* entry : text::SLEEP_CURRENT_LINES) {
      renderer.drawText(UI_10_FONT_ID, MARGIN, y, entry);
      y += line10;
    }
    y += 16;
    renderer.drawText(UI_10_FONT_ID, MARGIN, y, text::SLEEP_PINS_HEADING, true, EpdFontFamily::BOLD);
    y += line10;
    for (const char* entry : text::SLEEP_PIN_LINES) {
      renderer.drawText(UI_10_FONT_ID, MARGIN, y, entry);
      y += line10;
    }

    y += 24;
    snprintf(line, sizeof(line), "%s: %.*s", text::BOARD_TAG, static_cast<int>(board_tag::boardNameLen()),
             board_tag::boardName());
    renderer.drawCenteredText(SMALL_FONT_ID, y, line);
    y += renderer.getLineHeight(SMALL_FONT_ID) + 4;
    renderer.drawCenteredText(SMALL_FONT_ID, y, CROSSPOINT_VERSION);
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
  }
  // The refresh has returned: the screen the operator times from is up (T14).
  testsuite::finishSleepEntry();
}
#endif

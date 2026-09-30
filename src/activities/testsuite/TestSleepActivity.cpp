#if CROSSPOINT_TEST_SUITE
#include "TestSleepActivity.h"

#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <Logging.h>

#include <cstdio>

#include "TestStrings.h"
#include "fontIds.h"
#include "network/FirmwareBoardTag.h"

namespace text = testsuite::text;

void TestSleepActivity::onEnter() {
  Activity::onEnter();
  LOG_INF("TEST", "Sleeping (%s)", fromTimeout ? "timeout" : "power button or battery cutoff");
  // Drawn here, outside the render task's per-render polarity and orientation.
  RenderLock lock;
  display.setInverted(false);
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  const int middle = renderer.getScreenHeight() / 2;
  renderer.clearScreen();
  renderer.drawCenteredText(UI_12_FONT_ID, middle - 60, text::ASLEEP, true, EpdFontFamily::BOLD);
  renderer.drawCenteredText(UI_10_FONT_ID, middle - 20, text::WAKE_HINT);
  renderer.drawCenteredText(UI_10_FONT_ID, middle + 20,
                            fromTimeout ? text::SLEEP_FROM_TIMEOUT : text::SLEEP_FROM_REQUEST);
  char line[64];
  snprintf(line, sizeof(line), "%s: %.*s", text::BOARD_TAG, static_cast<int>(board_tag::boardNameLen()),
           board_tag::boardName());
  renderer.drawCenteredText(SMALL_FONT_ID, middle + 60, line);
  renderer.drawCenteredText(SMALL_FONT_ID, middle + 85, CROSSPOINT_VERSION);
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}
#endif

#if CROSSPOINT_TEST_SUITE
#include "TestScreen.h"

#include <GfxRenderer.h>
#include <HalFrontlight.h>
#include <HalGPIO.h>
#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <variant>

#include "MappedInputManager.h"
#include "TestCatalog.h"
#include "TestChoiceActivity.h"
#include "TestStrings.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace fui = freeink::ui;

namespace testsuite {

// --- TestPage ----------------------------------------------------------------

void TestPage::paragraph(const char* text, const int maxLines) {
  if (text == nullptr || text[0] == '\0') return;
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  for (const auto& line : renderer.wrappedText(UI_10_FONT_ID, text, width, maxLines)) {
    if (y + lineHeight > bottom) return;
    renderer.drawText(UI_10_FONT_ID, x, y, line.c_str());
    y += lineHeight;
  }
}

void TestPage::heading(const char* text) {
  const int lineHeight = renderer.getLineHeight(UI_12_FONT_ID);
  if (text == nullptr || y + lineHeight > bottom) return;
  const std::string fitted = renderer.truncatedText(UI_12_FONT_ID, text, width, EpdFontFamily::BOLD);
  renderer.drawText(UI_12_FONT_ID, x, y, fitted.c_str(), true, EpdFontFamily::BOLD);
  y += lineHeight;
}

void TestPage::row(const char* label, const char* value) {
  const int font = compact ? UI_10_FONT_ID : UI_12_FONT_ID;
  const int lineHeight = renderer.getLineHeight(font);
  if (y + lineHeight > bottom) return;
  if (value == nullptr) value = "";
  const int valueWidth = renderer.getTextWidth(font, value, EpdFontFamily::BOLD);
  if (valueWidth > width) {
    // A value wider than the page keeps its start and drops the label.
    const std::string fitted = renderer.truncatedText(font, value, width, EpdFontFamily::BOLD);
    renderer.drawText(font, x, y, fitted.c_str(), true, EpdFontFamily::BOLD);
    y += lineHeight;
    return;
  }
  renderer.drawText(font, x + width - valueWidth, y, value, true, EpdFontFamily::BOLD);
  constexpr int labelGap = 12;
  if (label != nullptr && width - valueWidth - labelGap > 0) {
    const std::string fitted = renderer.truncatedText(font, label, width - valueWidth - labelGap);
    renderer.drawText(font, x, y, fitted.c_str());
  }
  y += lineHeight;
}

void TestPage::rowf(const char* label, const char* valueFmt, ...) {
  char value[64];
  va_list args;
  va_start(args, valueFmt);
  vsnprintf(value, sizeof(value), valueFmt, args);
  va_end(args);
  row(label, value);
}

// --- TestScreen --------------------------------------------------------------

TestScreen::TestScreen(const TestInfo& info, GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity(std::string("Test ") + info.id, renderer, mappedInput), UiAppHost(renderer), info(info) {
  snprintf(title, sizeof(title), "%s  %s", info.id, info.title);
}

void TestScreen::onEnter() {
  Activity::onEnter();
  lastInputAt = millis();
  resetUi();
  app.on(ACTION_SLOT, &TestScreen::slotTrampoline, this);
  app.setScreen(&TestScreen::screenTrampoline, this);
  LOG_INF("TEST", "Opened %s (%s)", info.id, info.title);
  onStart();
  requestUpdate();
}

void TestScreen::onExit() {
  // No test leaves the boost lit or the radio up behind it.
  if (Frontlight.isOn()) Frontlight.setOn(false);
  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
  }
  Activity::onExit();
}

void TestScreen::loop() {
  if (gpio.wasAnyPressed() || gpio.wasAnyReleased() || gpio.wasTouchActivity()) lastInputAt = millis();
  const bool touchInput = touchIsTestInput();
  if (!touchInput) {
    const auto route = routeTouch(mappedInput);
    if (route.routed && app.invalidated()) requestUpdate();
    if (route) return;  // a footer button; slotTrampoline handled it
  }

  using Button = MappedInputManager::Button;
  if (keysAreTestInput()) {
    // A hold stands in for the footer button; its release never reaches the
    // test (wasLongPressed() consumes it).
    if (mappedInput.wasLongPressed(Button::Back, ACTION_HOLD_MS)) {
      exitTest();
      return;
    }
    constexpr struct {
      Button button;
      Slot slot;
    } HOLDS[] = {{Button::Confirm, Slot::Confirm}, {Button::Left, Slot::Left}, {Button::Right, Slot::Right}};
    for (const auto& hold : HOLDS) {
      if (slotLabel(hold.slot) != nullptr && mappedInput.wasLongPressed(hold.button, ACTION_HOLD_MS)) {
        pressSlot(hold.slot);
        break;
      }
    }
  } else if (mappedInput.wasReleased(Button::Back) && !(touchInput && mappedInput.wasBackGesture())) {
    // While touch is the test's input, only the Back key leaves.
    exitTest();
    return;
  } else if (mappedInput.wasReleased(Button::Confirm)) {
    pressSlot(Slot::Confirm);
  } else if (mappedInput.wasReleased(Button::Left)) {
    pressSlot(Slot::Left);
  } else if (mappedInput.wasReleased(Button::Right)) {
    pressSlot(Slot::Right);
  } else if (mappedInput.wasReleased(Button::Up)) {
    onStep(+1);
  } else if (mappedInput.wasReleased(Button::Down)) {
    onStep(-1);
  }
  onTick();
}

void TestScreen::screenTrampoline(UiScreen& screen, void* user) { static_cast<TestScreen*>(user)->buildFooter(screen); }

void TestScreen::slotTrampoline(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<TestScreen*>(user);
  const auto slot = static_cast<Slot>(event.value);
  // The touch Exit leaves even when the keys are test input.
  if (slot == Slot::Back) {
    self->exitTest();
    return;
  }
  self->pressSlot(slot);
}

void TestScreen::buildFooter(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(headerBottom), static_cast<int16_t>(bands.right),
                                                static_cast<int16_t>(4 + bands.bottom),
                                                static_cast<int16_t>(bands.left)});
  // Physical order of the front keys (the settings pin their default roles).
  constexpr Slot ORDER[] = {Slot::Back, Slot::Confirm, Slot::Left, Slot::Right};
  fui::FooterAction actions[4];
  for (int i = 0; i < 4; ++i) {
    const char* label = ORDER[i] == Slot::Back ? text::EXIT : slotLabel(ORDER[i]);
    actions[i].label = label;
    actions[i].action = label != nullptr ? ACTION_SLOT : fui::NO_ACTION;
    actions[i].value = static_cast<int16_t>(ORDER[i]);
  }
  fui::FooterProps props;
  props.actions = actions;
  props.count = 4;
  props.sidePadding = static_cast<int16_t>(metrics.contentSidePadding);
  props.gap = 8;
  props.buttonBorderEdges = fui::EdgesAll;
  screen.footer(props);
  footerTop = screen.body().bottom();
}

void TestScreen::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  bands = edgeBands();
  const int width = renderer.getScreenWidth() - bands.left - bands.right;
  renderer.clearScreen();
  // A test that reads the keys leaves only by Exit or a Back hold, and one that
  // reads the touch panel only by the Back key, so no header back button.
  GUI.drawHeader(renderer, Rect{bands.left, bands.top + metrics.topPadding, width, metrics.headerHeight}, title,
                 nullptr, /*backButton=*/!keysAreTestInput() && !touchIsTestInput());
  headerBottom = bands.top + metrics.topPadding + metrics.headerHeight;
  footerTop = renderer.getScreenHeight() - bands.bottom;
  renderUi();

  TestPage page(renderer, bands.left + metrics.contentSidePadding, headerBottom + metrics.verticalSpacing,
                width - 2 * metrics.contentSidePadding, footerTop - metrics.verticalSpacing);
  if (showsLastResult()) drawResult(page);
  if (keysAreTestInput()) {
    page.paragraph(text::HOLD_BACK_TO_EXIT, 3);
    page.gap();
  }
  drawBody(page);
  renderer.displayBuffer(cleanRepaint.exchange(false) ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH);
}

void TestScreen::drawResult(TestPage& page) const {
  if (!hasResult) return;
  char line[32];
  snprintf(line, sizeof(line), "%s: %s", text::RESULT, verdictName(verdict));
  page.heading(line);
  if (failedChecks[0] != '\0') page.paragraph(failedChecks, 2);
  page.paragraph(shownDetails, verdict == Verdict::Fail ? 6 : 4);
  switch (outcome) {
    case LogOutcome::Saved:
      page.paragraph(text::SAVED, 2);
      break;
    case LogOutcome::SerialOnly:
      page.paragraph(text::SERIAL_ONLY, 2);
      break;
    case LogOutcome::WriteFailed:
      page.paragraph(text::WRITE_FAILED, 2);
      break;
  }
  page.gap(12);
}

void TestScreen::record(const Verdict v, const char* detailsFmt, ...) {
  va_list args;
  va_start(args, detailsFmt);
  vsnprintf(details, sizeof(details), detailsFmt, args);
  va_end(args);
  splitFailedChecks(v);
  verdict = v;
  outcome = recordResult(info, v, details);
  hasResult = true;
  requestUpdate();
}

// A record that judges checks carries "fails=<check>+<check>" (or "fails=none"),
// mostly last, where the wrapped details would cut it off. A FAIL shows the list
// on its own line; any other verdict keeps "fails=" (an INFO's "too_short" and
// the like) in its details.
void TestScreen::splitFailedChecks(const Verdict v) {
  failedChecks[0] = '\0';
  snprintf(shownDetails, sizeof(shownDetails), "%s", details);
  if (v != Verdict::Fail) return;
  constexpr char KEY[] = "fails=";
  constexpr size_t KEY_LEN = sizeof(KEY) - 1;
  char* at = shownDetails;
  while ((at = strstr(at, KEY)) != nullptr && at != shownDetails && at[-1] != ' ') at += KEY_LEN;
  if (at == nullptr) return;
  const char* value = at + KEY_LEN;
  const size_t valueLen = strcspn(value, " ");
  if (valueLen != 0 && !(valueLen == 4 && strncmp(value, "none", 4) == 0)) {
    snprintf(failedChecks, sizeof(failedChecks), "%s %.*s", text::FAILED_CHECKS, static_cast<int>(valueLen), value);
  }
  // Drop "fails=<value>" and one space next to it from the drawn details.
  char* end = at + KEY_LEN + valueLen;
  if (*end == ' ') {
    ++end;
  } else if (at != shownDetails) {
    --at;
  }
  memmove(at, end, strlen(end) + 1);
}

bool TestScreen::confirmLightLoad() {
  if (!Frontlight.present()) {
    LOG_INF("TEST", "%s: no front light in this build or on this board", info.id);
    return false;
  }
  // "No load" first: it is the preselected answer.
  static constexpr const char* LOADS[] = {text::LIGHT_LOAD_NONE, text::LIGHT_LOAD_J3, text::LIGHT_LOAD_J6};
  auto prompt = makeUniqueNoThrow<TestChoiceActivity>(renderer, mappedInput, text::LIGHT_LOAD_TITLE,
                                                      text::LIGHT_LOAD_BODY, LOADS, 3);
  if (!prompt) {
    LOG_ERR("TEST", "OOM: LED load prompt");
    return false;
  }
  startActivityForResult(std::move(prompt), [this](const ActivityResult& result) {
    const auto* choice = std::get_if<MenuResult>(&result.data);
    const int answer = result.isCancelled || choice == nullptr ? -1 : choice->action;
    if (answer != 1 && answer != 2) {
      LOG_INF("TEST", "%s: no LED load confirmed; the light stays off", info.id);
      return;
    }
    LOG_INF("TEST", "%s: LED load confirmed by the operator: %s", info.id,
            answer == 1 ? text::LIGHT_LOAD_J3 : text::LIGHT_LOAD_J6);
    onLightLoadConfirmed(answer == 1 ? LightLoad::J3Panel : LightLoad::J6Strip);
  });
  return true;
}

bool TestScreen::ask(const int question, const char* heading, const char* body, const char* const* answers,
                     const int count) {
  auto prompt = makeUniqueNoThrow<TestChoiceActivity>(renderer, mappedInput, heading, body, answers, count);
  if (!prompt) {
    LOG_ERR("TEST", "OOM: question prompt");
    return false;
  }
  startActivityForResult(std::move(prompt), [this, question](const ActivityResult& result) {
    const auto* choice = std::get_if<MenuResult>(&result.data);
    onAnswer(question, result.isCancelled || choice == nullptr ? -1 : choice->action);
  });
  return true;
}

unsigned long TestScreen::msSinceInput() const { return millis() - lastInputAt; }

void TestScreen::pressSlot(const Slot slot) {
  if (slot == Slot::Back || slotLabel(slot) == nullptr) return;
  onSlot(slot);
}

void TestScreen::exitTest() {
  if (onBack()) return;
  app.clearTapFlash();
  finish();
}

}  // namespace testsuite
#endif

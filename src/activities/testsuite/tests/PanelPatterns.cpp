#if CROSSPOINT_TEST_SUITE
#include "PanelPatterns.h"

#include <GfxRenderer.h>

#include <algorithm>
#include <string>

#include "fontIds.h"

namespace testsuite::patterns {
namespace {

enum class Band : uint8_t { Checker, FineChecker, HLines, VLines, Text, Solid, Dither };
struct BandSpec {
  Band band;
  bool alt;  // the other phase, side or order of the band's pattern
};
// Every band changes pattern between the two frames.
constexpr BandSpec FRAME_A[] = {{Band::Checker, false}, {Band::HLines, false}, {Band::Text, false},
                                {Band::Solid, false},   {Band::Dither, false}, {Band::FineChecker, false}};
constexpr BandSpec FRAME_B[] = {{Band::Text, true},    {Band::FineChecker, false}, {Band::Checker, true},
                                {Band::VLines, false}, {Band::Solid, true},        {Band::Dither, true}};
constexpr int BANDS = sizeof(FRAME_A) / sizeof(FRAME_A[0]);
static_assert(sizeof(FRAME_B) / sizeof(FRAME_B[0]) == BANDS, "both frames have the same bands");

constexpr int CHECKER_CELL = 20;
constexpr int LINE_PITCH = 4;
constexpr char SAMPLE_TEXT[] = "The quick brown fox jumps over the lazy dog 0123456789";
constexpr char AA_LINE_1[] = "Anti-aliased text: quick brown fox";
constexpr char AA_LINE_2[] = "jumps over the lazy dog, 0123456789";
constexpr char AA_LABEL[] = "Anti-aliased (gray pass):";
constexpr char BW_LABEL[] = "The same text, B/W only:";
constexpr GrayLevel RAMP[] = {GrayLevel::White, GrayLevel::Light, GrayLevel::Dark, GrayLevel::Black};
constexpr const char* RAMP_NAMES[] = {"white", "light gray", "dark gray", "black"};
constexpr int COLUMN_WIDTH = 16;

void drawBand(const GfxRenderer& renderer, const BandSpec& spec, const int y, const int width, const int height) {
  switch (spec.band) {
    case Band::Checker:
      for (int row = 0; row * CHECKER_CELL < height; ++row) {
        for (int col = 0; col * CHECKER_CELL < width; ++col) {
          if (((row + col) & 1) != (spec.alt ? 1 : 0)) continue;
          renderer.fillRect(col * CHECKER_CELL, y + row * CHECKER_CELL,
                            std::min(CHECKER_CELL, width - col * CHECKER_CELL),
                            std::min(CHECKER_CELL, height - row * CHECKER_CELL), true);
        }
      }
      break;
    case Band::FineChecker:
      renderer.fillRectDither(0, y, width, height, Color::DarkGray);  // (x + y) even: black
      break;
    case Band::HLines:
      for (int line = 0; line < height; line += LINE_PITCH) renderer.fillRect(0, y + line, width, 1, true);
      break;
    case Band::VLines:
      for (int line = 0; line < width; line += LINE_PITCH) renderer.fillRect(line, y, 1, height, true);
      break;
    case Band::Text: {
      const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
      int line = 0;
      for (int top = y + 2; top + lineHeight <= y + height; top += lineHeight, ++line) {
        const int indent = 4 + (spec.alt ? (line % 3) * 12 : 0);
        const std::string fitted = renderer.truncatedText(UI_10_FONT_ID, SAMPLE_TEXT, width - indent - 4);
        renderer.drawText(UI_10_FONT_ID, indent, top, fitted.c_str());
      }
      break;
    }
    case Band::Solid:
      renderer.fillRect(spec.alt ? width / 2 : 0, y, spec.alt ? width - width / 2 : width / 2, height, true);
      break;
    case Band::Dither:
      renderer.fillRectDither(0, y, width / 2, height, spec.alt ? Color::DarkGray : Color::LightGray);
      renderer.fillRectDither(width / 2, y, width - width / 2, height, spec.alt ? Color::LightGray : Color::DarkGray);
      break;
  }
}

bool bwPass(const GfxRenderer& renderer) { return renderer.getRenderMode() == GfxRenderer::BW; }

}  // namespace

void drawBwFrame(const GfxRenderer& renderer, const bool frameB) {
  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();
  const BandSpec* bands = frameB ? FRAME_B : FRAME_A;
  for (int i = 0; i < BANDS; ++i) {
    const int top = height * i / BANDS;
    const int bottom = height * (i + 1) / BANDS;
    drawBand(renderer, bands[i], top, width, bottom - top);
  }
}

void fillGray(const GfxRenderer& renderer, const int x, const int y, const int width, const int height,
              const GrayLevel level) {
  // The B/W base inks every level but white; the overlay planes then lighten
  // dark gray (both planes) and light gray (MSB only). A set plane bit marks.
  switch (renderer.getRenderMode()) {
    case GfxRenderer::BW:
      if (level != GrayLevel::White) renderer.fillRect(x, y, width, height, true);
      break;
    case GfxRenderer::GRAYSCALE_LSB:
      if (level == GrayLevel::Dark) renderer.fillRect(x, y, width, height, false);
      break;
    case GfxRenderer::GRAYSCALE_MSB:
      if (level == GrayLevel::Dark || level == GrayLevel::Light) renderer.fillRect(x, y, width, height, false);
      break;
  }
}

void drawGrayChart(const GfxRenderer& renderer, const char* title, const char* footer) {
  // One-bit UI text leaves the gray planes unmarked, so it is drawn in every
  // pass; a white fill would mark them and is never used here.
  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();
  const int line10 = renderer.getLineHeight(UI_10_FONT_ID);
  const int line12 = renderer.getLineHeight(UI_12_FONT_ID);
  const int lineAa = renderer.getLineHeight(NOTOSANS_14_FONT_ID);
  constexpr int margin = 16;

  int y = 12;
  if (title != nullptr) renderer.drawText(UI_12_FONT_ID, margin, y, title, true, EpdFontFamily::BOLD);
  y += line12 + 12;

  const int rampHeight = height * 30 / 100;
  const int count = static_cast<int>(sizeof(RAMP) / sizeof(RAMP[0]));
  for (int i = 0; i < count; ++i) {
    const int left = width * i / count;
    const int right = width * (i + 1) / count;
    fillGray(renderer, left, y, right - left, rampHeight, RAMP[i]);
    const int nameWidth = renderer.getTextWidth(UI_10_FONT_ID, RAMP_NAMES[i]);
    renderer.drawText(UI_10_FONT_ID, left + (right - left - nameWidth) / 2, y + rampHeight + 4, RAMP_NAMES[i]);
  }
  if (bwPass(renderer)) renderer.drawRect(0, y, width, rampHeight, true);  // the white patch's edge
  y += rampHeight + 4 + line10 + 12;

  const int columnsHeight = height * 8 / 100;
  for (int x = 0, i = 0; x < width; x += COLUMN_WIDTH, ++i) {
    fillGray(renderer, x, y, std::min(COLUMN_WIDTH, width - x), columnsHeight, RAMP[i % count]);
  }
  y += columnsHeight + 16;

  renderer.drawText(UI_10_FONT_ID, margin, y, AA_LABEL);
  y += line10 + 2;
  // Two-bit glyphs (the built-in NotoSans 14, the one family left in the image since upstream moved NotoSerif to an SD
  // font, #4022): ink in the B/W pass, their edge levels in the gray passes.
  renderer.drawText(NOTOSANS_14_FONT_ID, margin, y, AA_LINE_1);
  renderer.drawText(NOTOSANS_14_FONT_ID, margin, y + lineAa, AA_LINE_2);
  y += 2 * lineAa + 10;
  renderer.drawText(UI_10_FONT_ID, margin, y, BW_LABEL);
  y += line10 + 2;
  if (bwPass(renderer)) {
    renderer.drawText(NOTOSANS_14_FONT_ID, margin, y, AA_LINE_1);
    renderer.drawText(NOTOSANS_14_FONT_ID, margin, y + lineAa, AA_LINE_2);
  }

  if (footer != nullptr) renderer.drawText(UI_10_FONT_ID, margin, height - line10 - 8, footer);
}

void drawPlainPage(const GfxRenderer& renderer, const char* title, const char* note) {
  const int width = renderer.getScreenWidth();
  constexpr int margin = 16;
  int y = 12;
  if (title != nullptr) renderer.drawText(UI_12_FONT_ID, margin, y, title, true, EpdFontFamily::BOLD);
  y += renderer.getLineHeight(UI_12_FONT_ID) + 4;
  if (note != nullptr) {
    const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
    for (const auto& line : renderer.wrappedText(UI_10_FONT_ID, note, width - 2 * margin, 3)) {
      renderer.drawText(UI_10_FONT_ID, margin, y, line.c_str());
      y += lineHeight;
    }
  }
  renderer.fillRect(0, y + 4, width, 1, true);
}

}  // namespace testsuite::patterns
#endif

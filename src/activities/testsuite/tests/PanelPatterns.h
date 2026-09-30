#pragma once

// Full-screen frames of the display tests (CROSSPOINT_TEST_SUITE builds only).
// Render task, RenderLock held; everything scales with the logical screen.
#if CROSSPOINT_TEST_SUITE
#include <cstdint>

class GfxRenderer;

namespace testsuite::patterns {

// The two B/W frames T4 and T8 alternate. The screen is cut into six bands
// (20 px checkerboard, 1 px checkerboard, 1 px lines, text, solid black and
// white, dithered grays), and frame B puts a different pattern in every band,
// so a ghost of the other frame shows as a foreign texture.
void drawBwFrame(const GfxRenderer& renderer, bool frameB);

// The chart of the grayscale tests: a four-level ramp (white, light gray, dark
// gray, black), alternating narrow columns of the four levels, anti-aliased text
// and the same text in B/W only. Draw it once per pass in the renderer's current
// mode (BW base, GRAYSCALE_LSB, GRAYSCALE_MSB, in a strip target or not): in the
// gray modes only the gray patches and the anti-aliased text mark the plane.
// `title` and `footer` are B/W only.
void drawGrayChart(const GfxRenderer& renderer, const char* title, const char* footer);

// A white page with a title and a note at the top, where the chart had its
// title: the B/W page that follows a gray pass.
void drawPlainPage(const GfxRenderer& renderer, const char* title, const char* note);

// Levels of the two-bit overlay (the reader's 2-bit bitmaps use the same codes).
enum class GrayLevel : uint8_t { Black = 0, Dark = 1, Light = 2, White = 3 };
// A rectangle of one level in the renderer's current mode.
void fillGray(const GfxRenderer& renderer, int x, int y, int width, int height, GrayLevel level);

}  // namespace testsuite::patterns
#endif

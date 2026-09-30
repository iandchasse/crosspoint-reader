#pragma once

// The reader's grayscale (anti-aliasing) pass, route by route, for the display
// tests (CROSSPOINT_TEST_SUITE builds only). Each route follows the reader's
// calls for that kind of page (EpubReaderActivity::renderContents, SleepActivity).
#if CROSSPOINT_TEST_SUITE
#include <cstddef>
#include <cstdint>

#include "PanelRun.h"

class GfxRenderer;

namespace testsuite {

enum class GrayPath : uint8_t {
  TiledPlanes,    // text page: FAST base fired async, both planes rendered into buffers meanwhile, then streamed
  TiledStrips,    // text page when the plane buffers do not fit: the planes in 80-row strips after the base
  FastBase,       // text page with the Sunlight Fading Fix: displayGrayscaleBase(FAST), no async base
  HalfBase,       // a cleanup page: HALF base, then the strips
  FastBaseImage,  // an image page: blocking FAST base, then the strips
  Buffered,       // sleep screen, images: HALF base, B/W frame stored, planes copied whole, B/W restored
};

inline constexpr int STRIP_ROWS = 80;  // the reader's band height

// Buffers a route needs, owned by the test (allocated when its screen opens).
struct GrayBuffers {
  uint8_t* lsbPlane = nullptr;  // TiledPlanes: one full plane each
  uint8_t* msbPlane = nullptr;
  uint8_t* stripScratch = nullptr;  // the strip routes: STRIP_ROWS panel rows
};

// What one pass did.
struct GrayRun {
  RefreshSample base;      // the B/W base refresh
  RefreshSample gray;      // the gray waveform; not reported when the pass was skipped
  uint16_t renderMs = 0;   // both planes rendered (strip routes: rendered and streamed; Buffered: and copied)
  uint16_t waitMs = 0;     // TiledPlanes: what was left of the base refresh once the planes were ready
  uint16_t writeMs = 0;    // TiledPlanes: both planes streamed to the controller
  uint16_t cleanupMs = 0;  // the differential baseline rebuilt (tiled), or the B/W frame restored (Buffered)
  size_t internalFreeBefore = 0;
  size_t internalFreeMin = 0;
  size_t psramFreeBefore = 0;
  size_t psramFreeMin = 0;
  bool missingBuffer = false;  // no plane buffers or scratch, or the B/W store failed: gray skipped
  bool grayRan() const { return gray.reported() && gray.customLut; }
};

const char* grayPathName(GrayPath path);  // for results.csv

// Render task, RenderLock held. Draws the gray chart (patterns::drawGrayChart)
// and runs it through `path`; the renderer is in BW mode afterwards and, except
// for Buffered (restored frame), holds the chart's B/W frame.
void runGrayPass(GfxRenderer& renderer, GrayPath path, const GrayBuffers& buffers, const char* title,
                 const char* footer, GrayRun& run);

}  // namespace testsuite
#endif

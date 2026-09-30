#if CROSSPOINT_TEST_SUITE
#include "GrayPass.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <HalMemory.h>

#include <algorithm>

#include "PanelPatterns.h"

namespace testsuite {
namespace {

uint16_t since(const unsigned long start) {
  return static_cast<uint16_t>(std::min<unsigned long>(millis() - start, UINT16_MAX));
}

void noteHeap(GrayRun& run) {
  run.internalFreeMin = std::min(run.internalFreeMin, HalMemory::getInternalHeap().freeBytes);
  run.psramFreeMin = std::min(run.psramFreeMin, HalMemory::getPsramHeap().freeBytes);
}

void selectPlane(GfxRenderer& renderer, const bool lsb) {
  renderer.setRenderMode(lsb ? GfxRenderer::GRAYSCALE_LSB : GfxRenderer::GRAYSCALE_MSB);
}

// One plane, band by band, into a whole-plane buffer.
void renderPlane(GfxRenderer& renderer, const bool lsb, uint8_t* plane, const char* title, const char* footer) {
  const int rows = renderer.getDisplayHeight();
  const size_t rowBytes = renderer.getDisplayWidthBytes();
  selectPlane(renderer, lsb);
  for (int y = 0; y < rows; y += STRIP_ROWS) {
    const int count = std::min(STRIP_ROWS, rows - y);
    renderer.beginStripTarget(plane + static_cast<size_t>(y) * rowBytes, y, count);
    renderer.clearScreen(0x00);
    patterns::drawGrayChart(renderer, title, footer);
    renderer.endStripTarget();
  }
}

// One plane, band by band, each band streamed to the controller from the scratch.
void streamPlane(GfxRenderer& renderer, const bool lsb, uint8_t* scratch, const char* title, const char* footer) {
  const int rows = renderer.getDisplayHeight();
  selectPlane(renderer, lsb);
  for (int y = 0; y < rows; y += STRIP_ROWS) {
    const int count = std::min(STRIP_ROWS, rows - y);
    renderer.beginStripTarget(scratch, y, count);
    renderer.clearScreen(0x00);
    patterns::drawGrayChart(renderer, title, footer);
    renderer.endStripTarget();
    renderer.writeGrayscalePlaneStrip(lsb, scratch, y, count);
  }
}

}  // namespace

const char* grayPathName(const GrayPath path) {
  switch (path) {
    case GrayPath::TiledPlanes:
      return "tiled_planes";
    case GrayPath::TiledStrips:
      return "tiled_strips";
    case GrayPath::FastBase:
      return "fast_base_fading_fix";
    case GrayPath::HalfBase:
      return "half_base";
    case GrayPath::FastBaseImage:
      return "fast_base_image";
    case GrayPath::Buffered:
      return "buffered";
  }
  return "?";
}

void runGrayPass(GfxRenderer& renderer, const GrayPath path, const GrayBuffers& buffers, const char* title,
                 const char* footer, GrayRun& run) {
  run = GrayRun{};
  run.internalFreeBefore = run.internalFreeMin = HalMemory::getInternalHeap().freeBytes;
  run.psramFreeBefore = run.psramFreeMin = HalMemory::getPsramHeap().freeBytes;

  renderer.setRenderMode(GfxRenderer::BW);
  renderer.clearScreen();
  patterns::drawGrayChart(renderer, title, footer);
  const int rows = renderer.getDisplayHeight();
  unsigned long start = 0;

  switch (path) {
    case GrayPath::TiledPlanes:
      if (buffers.lsbPlane == nullptr || buffers.msbPlane == nullptr) {
        run.missingBuffer = true;
        run.base = refreshTimed(renderer, HalDisplay::FAST_REFRESH);
        return;
      }
      run.base = refreshAsyncStart(renderer, HalDisplay::FAST_REFRESH);
      start = millis();
      renderPlane(renderer, true, buffers.lsbPlane, title, footer);
      renderPlane(renderer, false, buffers.msbPlane, title, footer);
      run.renderMs = since(start);
      start = millis();
      refreshAsyncFinish(renderer, run.base, /*reseed=*/false);
      run.waitMs = since(start);
      start = millis();
      renderer.writeGrayscalePlaneStrip(true, buffers.lsbPlane, 0, rows);
      renderer.writeGrayscalePlaneStrip(false, buffers.msbPlane, 0, rows);
      run.writeMs = since(start);
      break;

    case GrayPath::TiledStrips:
    case GrayPath::FastBase:
    case GrayPath::HalfBase:
    case GrayPath::FastBaseImage:
      if (buffers.stripScratch == nullptr) {
        run.missingBuffer = true;
        run.base = refreshTimed(renderer, HalDisplay::FAST_REFRESH);
        return;
      }
      if (path == GrayPath::TiledStrips) {
        // The strips go to controller RAM, so the base has finished first.
        run.base = refreshAsyncStart(renderer, HalDisplay::FAST_REFRESH);
        refreshAsyncFinish(renderer, run.base, /*reseed=*/false);
      } else if (path == GrayPath::FastBase) {
        run.base = measureRefresh(HalDisplay::FAST_REFRESH,
                                  [&renderer] { renderer.displayGrayscaleBase(HalDisplay::FAST_REFRESH); });
      } else if (path == GrayPath::HalfBase) {
        run.base = refreshTimed(renderer, HalDisplay::HALF_REFRESH);
        renderer.preconditionGrayscale();
      } else {
        run.base = refreshTimed(renderer, HalDisplay::FAST_REFRESH);
      }
      start = millis();
      streamPlane(renderer, true, buffers.stripScratch, title, footer);
      streamPlane(renderer, false, buffers.stripScratch, title, footer);
      run.renderMs = since(start);
      break;

    case GrayPath::Buffered:
      run.base = measureRefresh(HalDisplay::HALF_REFRESH,
                                [&renderer] { renderer.displayGrayscaleBase(HalDisplay::HALF_REFRESH); });
      if (!renderer.storeBwBuffer()) {
        run.missingBuffer = true;
        return;
      }
      noteHeap(run);
      start = millis();
      renderer.clearScreen(0x00);
      selectPlane(renderer, true);
      patterns::drawGrayChart(renderer, title, footer);
      renderer.copyGrayscaleLsbBuffers();
      renderer.clearScreen(0x00);
      selectPlane(renderer, false);
      patterns::drawGrayChart(renderer, title, footer);
      renderer.copyGrayscaleMsbBuffers();
      run.renderMs = since(start);
      run.gray = grayTimed(renderer);
      renderer.setRenderMode(GfxRenderer::BW);
      start = millis();
      renderer.restoreBwBuffer();
      run.cleanupMs = since(start);
      noteHeap(run);
      return;
  }

  noteHeap(run);
  renderer.setRenderMode(GfxRenderer::BW);
  run.gray = grayTimed(renderer);
  start = millis();
  renderer.cleanupGrayscaleWithFrameBuffer();
  run.cleanupMs = since(start);
  noteHeap(run);
}

}  // namespace testsuite
#endif

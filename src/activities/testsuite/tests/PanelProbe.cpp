#if CROSSPOINT_TEST_SUITE
#include "PanelProbe.h"

#include <BoardConfig.h>
#include <HalDisplay.h>
#include <XteinkDetect.h>

#include <cstdio>

#include "TextBuffer.h"
#include "activities/testsuite/TestScreen.h"

namespace testsuite {
namespace {

using freeink::DisplayControllerSource;
using freeink::DisplayControllerVerdict;

const char* sourceKey(const DisplayControllerSource source) {
  switch (source) {
    case DisplayControllerSource::Probed:
      return "probed";
    case DisplayControllerSource::Cached:
      return "cached";
    case DisplayControllerSource::Forced:
#if FREEINK_SILKSCREEN_368
      return "fixed";  // the profile's controller: this build never probes
#else
      return "forced";
#endif
    case DisplayControllerSource::NotResolved:
      break;
  }
  return "unresolved";
}

const char* sourceText(const DisplayControllerSource source) {
  switch (source) {
    case DisplayControllerSource::Probed:
      return "probed this boot";
    case DisplayControllerSource::Cached:
      return "kept from the probe before the sleep";
    case DisplayControllerSource::Forced:
#if FREEINK_SILKSCREEN_368
      return "fixed in this build, no VER probe";
#else
      return "forced by the build";
#endif
    case DisplayControllerSource::NotResolved:
      break;
  }
  return "NOT resolved";
}

// The probe's verdict, from this boot's probe or the one kept across the sleep.
// Nothing was probed when the build forced the controller.
const freeink::XteinkDisplayProbeDiag* probeRecord() {
  const auto& diag = freeink::getXteinkDisplayProbeDiag();
  return diag.valid ? &diag : nullptr;
}

#if !FREEINK_SILKSCREEN_368  // the variant has no probe record to describe
DisplayControllerVerdict verdictOf(const freeink::XteinkDisplayProbeDiag& diag) {
  return static_cast<DisplayControllerVerdict>(diag.verdict);
}

const char* verdictKey(const freeink::XteinkDisplayProbeDiag* diag) {
  if (diag == nullptr) return "-";
  switch (verdictOf(*diag)) {
    case DisplayControllerVerdict::Uc81xxConfirmed:
      return "ultrachip";
    case DisplayControllerVerdict::PrimaryAssumed:
      return "no_ultrachip";
    case DisplayControllerVerdict::Inconclusive:
      return "inconclusive";
  }
  return "?";
}

const char* verdictText(const freeink::XteinkDisplayProbeDiag* diag) {
  if (diag == nullptr) return "not run";
  switch (verdictOf(*diag)) {
    case DisplayControllerVerdict::Uc81xxConfirmed:
      return "UltraChip answered";
    case DisplayControllerVerdict::PrimaryAssumed:
      return "no UltraChip answer: SSD1677";
    case DisplayControllerVerdict::Inconclusive:
      return "INCONCLUSIVE: runs as SSD1677";
  }
  return "?";
}

bool ultraChipAnswered(const freeink::XteinkDisplayProbeDiag* diag) {
  return diag != nullptr && verdictOf(*diag) == DisplayControllerVerdict::Uc81xxConfirmed;
}
#endif

const char* controllerName() { return BoardConfig::controllerPartName(BoardConfig::ACTIVE.displayController); }

#if FREEINK_SILKSCREEN_368
// The driver FreeInkDisplay::selectDriver() routes the profile's controller to in X3 mode. The UC8279d has the
// image's one X3 driver; no SDK call names it, so this is the facade's rule.
const char* driverName() {
  return BoardConfig::ACTIVE.displayController == BoardConfig::DisplayController::UC8279 ? "Uc8279Driver"
                                                                                         : "none linked";
}

// What really ran, where driverName() only repeats the facade's rule: Uc8279Driver reports the CDI byte of the
// bank a refresh used as its sequence (0x97 the first refresh after begin(), 0xD7 after; EpdBus.h). With no
// refresh on record there is nothing to compare, which is no mismatch.
bool driverReportMatches() {
  const auto bus = freeink::EpdBus::diagnostics();
  if (bus.refreshes == 0) return true;
  return bus.lastRefreshSequence == 0x97 || bus.lastRefreshSequence == 0xD7;
}
#endif

}  // namespace

void formatPanelProbe(char* buf, const size_t size) {
#if FREEINK_SILKSCREEN_368
  // The controller is fixed in this build's profile and nothing probes it: there is no ver= and no verdict=,
  // and no BUSY_N reading either. The row says what the build knows, not a reading of the panel. driver= is the
  // facade's rule and geom= the configured geometry (nothing reads the glass back); seq= is what the last
  // refresh reported, the CDI byte of the bank it ran, which names the driver family.
  snprintf(buf, size, "controller=%s source=%s probe=none variant=%02X driver=%s x3_mode=%d geom=%ux%u",
           controllerName(), sourceKey(freeink::displayControllerSource()),
           BoardConfig::ACTIVE.displayControllerVariant, driverName(), display.isX3Panel() ? 1 : 0,
           static_cast<unsigned>(display.getDisplayWidth()), static_cast<unsigned>(display.getDisplayHeight()));
  const auto bus = freeink::EpdBus::diagnostics();
  if (bus.refreshes != 0) {
    appendf(buf, size, " seq=%02X", bus.lastRefreshSequence);
  } else {
    appendf(buf, size, " seq=-");
  }
#else
  const auto* diag = probeRecord();
  snprintf(buf, size, "controller=%s source=%s verdict=%s", controllerName(),
           sourceKey(freeink::displayControllerSource()), verdictKey(diag));
  if (diag != nullptr) {
    appendf(buf, size, " ver=%02X-%02X-%02X-%02X-%02X flg=%02X", diag->ver[0], diag->ver[1], diag->ver[2], diag->ver[3],
            diag->ver[4], diag->flg);
    // VER byte 2 is the UltraChip LUT_VER; an SSD1677 leaves the line floating.
    if (ultraChipAnswered(diag)) appendf(buf, size, " lut_ver=%02X", diag->ver[2]);
  }
  appendf(buf, size, " variant=%02X", BoardConfig::ACTIVE.displayControllerVariant);
  if (diag != nullptr && diag->mtpValid) {
    appendf(buf, size, " mtp=");
    for (const uint8_t byte : diag->mtp) appendf(buf, size, "%02X", byte);
  }
#endif
}

void formatPanelToken(char* buf, const size_t size) {
  const auto* diag = probeRecord();
  snprintf(buf, size, "%s:%s", controllerName(), sourceKey(freeink::displayControllerSource()));
  if (diag == nullptr) {
    appendf(buf, size, ":-:-");
    return;
  }
  appendf(buf, size, ":%02X%02X%02X%02X%02X:%02X", diag->ver[0], diag->ver[1], diag->ver[2], diag->ver[3], diag->ver[4],
          diag->flg);
}

Verdict panelProbeVerdict(char* fails, const size_t size) {
  snprintf(fails, size, "none");
#if FREEINK_SILKSCREEN_368
  // Nothing is probed, so there is no probe to resolve or doubt: INFO, as for any build that fixes its
  // controller. What can still be wrong is the build's own consistency: X3 mode on, the facade's 792x528 (what
  // it seeded from the driver at begin()) equal to the X3 constants and to the profile, the profile's
  // controller the UC8279d, and the driver family that ran the last refresh the one that controller calls for.
  // Each can fire only if the facade, the profile or the driver selection is edited, and none reads the panel:
  // an INFO here says the build is consistent, not that a panel is there (T4's BUSY time says that).
  if (!display.isX3Panel()) {
    snprintf(fails, size, "x3_mode");
    return Verdict::Fail;
  }
  if (display.getDisplayWidth() != BoardConfig::ACTIVE.displayWidth ||
      display.getDisplayHeight() != BoardConfig::ACTIVE.displayHeight ||
      display.getDisplayWidth() != EInkDisplay::X3_DISPLAY_WIDTH ||
      display.getDisplayHeight() != EInkDisplay::X3_DISPLAY_HEIGHT) {
    snprintf(fails, size, "geometry");
    return Verdict::Fail;
  }
  if (BoardConfig::ACTIVE.displayController != BoardConfig::DisplayController::UC8279) {
    snprintf(fails, size, "controller");
    return Verdict::Fail;
  }
  if (!driverReportMatches()) {
    snprintf(fails, size, "driver");
    return Verdict::Fail;
  }
  return Verdict::Info;
#else
  const auto source = freeink::displayControllerSource();
  if (source == DisplayControllerSource::Forced) return Verdict::Info;
  const auto* diag = probeRecord();
  if (source == DisplayControllerSource::NotResolved || diag == nullptr) {
    snprintf(fails, size, "not_resolved");
    return Verdict::Fail;
  }
  if (verdictOf(*diag) == DisplayControllerVerdict::Inconclusive) {
    snprintf(fails, size, "probe_inconclusive");
    return Verdict::Fail;
  }
  return Verdict::Pass;
#endif
}

void drawPanelProbe(TestPage& page, const bool brief) {
#if FREEINK_SILKSCREEN_368
  // No probe: the controller is the profile's and the page says so. The driver is the facade's rule.
  (void)brief;
  page.rowf("Panel controller", "%s, %s", controllerName(), sourceText(freeink::displayControllerSource()));
  page.rowf("Driver / geometry", "%s / %ux%u%s", driverName(), static_cast<unsigned>(display.getDisplayWidth()),
            static_cast<unsigned>(display.getDisplayHeight()), display.isX3Panel() ? "" : " (X3 mode OFF)");
#else
  const auto* diag = probeRecord();
  page.rowf("Panel controller", "%s, %s", controllerName(), sourceText(freeink::displayControllerSource()));
  if (diag == nullptr) {
    page.row("Controller probe", verdictText(diag));
    return;
  }
  page.rowf("Probe: VER / FLG", "%s, %02X %02X %02X %02X %02X / %02X", verdictText(diag), diag->ver[0], diag->ver[1],
            diag->ver[2], diag->ver[3], diag->ver[4], diag->flg);
  if (brief) return;
  if (ultraChipAnswered(diag)) {
    page.rowf("LUT_VER / driver variant", "%02X / %02X", diag->ver[2], BoardConfig::ACTIVE.displayControllerVariant);
  }
  if (diag->mtpValid) {
    page.rowf("MTP header (0x000-0x007)", "%02X %02X %02X %02X %02X %02X %02X %02X", diag->mtp[0], diag->mtp[1],
              diag->mtp[2], diag->mtp[3], diag->mtp[4], diag->mtp[5], diag->mtp[6], diag->mtp[7]);
  }
#endif
}

}  // namespace testsuite
#endif

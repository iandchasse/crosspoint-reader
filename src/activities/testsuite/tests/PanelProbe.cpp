#if CROSSPOINT_TEST_SUITE
#include "PanelProbe.h"

#include <BoardConfig.h>
#include <HalDisplay.h>
#include <XteinkDetect.h>

#include <cstdio>

#include "PanelFamily.h"
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
      return "forced";
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
      return "forced by the build";
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

DisplayControllerVerdict verdictOf(const freeink::XteinkDisplayProbeDiag& diag) {
  return static_cast<DisplayControllerVerdict>(diag.verdict);
}

const char* verdictKey(const freeink::XteinkDisplayProbeDiag* diag) {
  if (diag == nullptr) return "-";
  switch (verdictOf(*diag)) {
    case DisplayControllerVerdict::Uc81xxConfirmed:
      return X3_PANEL_BUILD ? "uc8279d" : "ultrachip";
    case DisplayControllerVerdict::PrimaryAssumed:
      return X3_PANEL_BUILD ? "uc8253_assumed" : "no_ultrachip";
    case DisplayControllerVerdict::Inconclusive:
      return "inconclusive";
  }
  return "?";
}

const char* verdictText(const freeink::XteinkDisplayProbeDiag* diag) {
  if (diag == nullptr) return "not run";
  switch (verdictOf(*diag)) {
    case DisplayControllerVerdict::Uc81xxConfirmed:
      return X3_PANEL_BUILD ? "VER 0x66: UC8279d" : "UltraChip answered";
    case DisplayControllerVerdict::PrimaryAssumed:
      return X3_PANEL_BUILD ? "VER 0xFF: UC8253 assumed" : "no UltraChip answer: SSD1677";
    case DisplayControllerVerdict::Inconclusive:
      return X3_PANEL_BUILD ? "INCONCLUSIVE: runs as UC8253" : "INCONCLUSIVE: runs as SSD1677";
  }
  return "?";
}

bool ultraChipAnswered(const freeink::XteinkDisplayProbeDiag* diag) {
  return diag != nullptr && verdictOf(*diag) == DisplayControllerVerdict::Uc81xxConfirmed;
}

const char* controllerName() { return BoardConfig::controllerPartName(BoardConfig::ACTIVE.displayController); }

// The driver FreeInkDisplay::selectDriver() routes the controller to in X3 mode: the UC8279d has its own,
// a UC8253 (the profile's default) the X3 driver. No SDK call names it, so this is the facade's rule.
const char* driverName() {
  return BoardConfig::ACTIVE.displayController == BoardConfig::DisplayController::UC8279 ? "Uc8279Driver"
                                                                                         : "Uc8253X3Driver";
}

}  // namespace

void formatPanelProbe(char* buf, const size_t size) {
  const auto* diag = probeRecord();
  snprintf(buf, size, "controller=%s source=%s verdict=%s", controllerName(),
           sourceKey(freeink::displayControllerSource()), verdictKey(diag));
  if (diag != nullptr) {
    if (X3_PANEL_BUILD) {
      // The stock X3 protocol reads three VER bytes and no FLG; byte 2 decides (0x66 a UC8279d, 0xFF a UC8253).
      appendf(buf, size, " ver=%02X-%02X-%02X ver_bytes=%u flg=n/a busy_timeout=%d", diag->ver[0], diag->ver[1],
              diag->ver[2], static_cast<unsigned>(diag->verBytesRead), diag->busyTimedOut ? 1 : 0);
    } else {
      appendf(buf, size, " ver=%02X-%02X-%02X-%02X-%02X flg=%02X", diag->ver[0], diag->ver[1], diag->ver[2],
              diag->ver[3], diag->ver[4], diag->flg);
    }
    // VER byte 2 is the UltraChip LUT_VER; an SSD1677 leaves the line floating.
    if (ultraChipAnswered(diag)) appendf(buf, size, " lut_ver=%02X", diag->ver[2]);
  }
  appendf(buf, size, " variant=%02X", BoardConfig::ACTIVE.displayControllerVariant);
  if (X3_PANEL_BUILD) {
    appendf(buf, size, " driver=%s x3_mode=%d geom=%ux%u", driverName(), display.isX3Panel() ? 1 : 0,
            static_cast<unsigned>(display.getDisplayWidth()), static_cast<unsigned>(display.getDisplayHeight()));
  }
  if (diag != nullptr && diag->mtpValid) {
    appendf(buf, size, " mtp=");
    for (const uint8_t byte : diag->mtp) appendf(buf, size, "%02X", byte);
  }
}

void formatPanelToken(char* buf, const size_t size) {
  const auto* diag = probeRecord();
  snprintf(buf, size, "%s:%s", controllerName(), sourceKey(freeink::displayControllerSource()));
  if (diag == nullptr) {
    appendf(buf, size, ":-:-");
    return;
  }
  if (X3_PANEL_BUILD) {
    appendf(buf, size, ":%02X%02X%02X:-", diag->ver[0], diag->ver[1], diag->ver[2]);  // three VER bytes, no FLG
    return;
  }
  appendf(buf, size, ":%02X%02X%02X%02X%02X:%02X", diag->ver[0], diag->ver[1], diag->ver[2], diag->ver[3], diag->ver[4],
          diag->flg);
}

Verdict panelProbeVerdict(char* fails, const size_t size) {
  snprintf(fails, size, "none");
#if FREEINK_SILKSCREEN_368
  // The variant's panel is the X3's from construction (the SDK facade starts in X3 mode there): X3 mode on
  // and the profile's 792x528. Anything else is the X4 route driving the wrong geometry. (A preprocessor
  // block, not X3_PANEL_BUILD: the 4.26" build's function stays as the compiler had it, byte for byte.)
  if (!display.isX3Panel()) {
    snprintf(fails, size, "x3_mode");
    return Verdict::Fail;
  }
  if (display.getDisplayWidth() != BoardConfig::ACTIVE.displayWidth ||
      display.getDisplayHeight() != BoardConfig::ACTIVE.displayHeight) {
    snprintf(fails, size, "geometry");
    return Verdict::Fail;
  }
#endif
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
}

void drawPanelProbe(TestPage& page, const bool brief) {
  const auto* diag = probeRecord();
  page.rowf("Panel controller", "%s, %s", controllerName(), sourceText(freeink::displayControllerSource()));
  if (X3_PANEL_BUILD) {
    // The stock X3 probe: three VER bytes, no FLG and no MTP, byte 2 decides. The driver is the facade's rule.
    page.rowf("Driver / geometry", "%s / %ux%u%s", driverName(), static_cast<unsigned>(display.getDisplayWidth()),
              static_cast<unsigned>(display.getDisplayHeight()), display.isX3Panel() ? "" : " (X3 mode OFF)");
    if (diag == nullptr) {
      page.row("Controller probe", verdictText(diag));
      return;
    }
    page.rowf("Probe: VER (3 bytes)", "%s, %02X %02X %02X", verdictText(diag), diag->ver[0], diag->ver[1],
              diag->ver[2]);
    if (!brief && diag->busyTimedOut) page.row("BUSY_N at the probe", "no answer in 300 ms");
    return;
  }
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
}

}  // namespace testsuite
#endif

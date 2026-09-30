#if CROSSPOINT_TEST_SUITE
#include "PanelProbe.h"

#include <BoardConfig.h>
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

const char* controllerName() { return BoardConfig::controllerPartName(BoardConfig::ACTIVE.displayController); }

}  // namespace

void formatPanelProbe(char* buf, const size_t size) {
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

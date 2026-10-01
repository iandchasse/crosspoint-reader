#if CROSSPOINT_TEST_SUITE
#include "BootProbes.h"

#include <BoardConfig.h>
#include <HalGPIO.h>
#include <InputManager.h>
#include <PowerManager.h>

#include <cstdio>

#include "RtcAccess.h"
#include "TextBuffer.h"
#include "activities/testsuite/TestScreen.h"
#include "activities/testsuite/TestSuiteAccess.h"

namespace testsuite {
namespace {

using PowerPinIdle = freeink::PowerManager::PowerPinIdle;

const char* powerIdleKey(const PowerPinIdle idle) {
  switch (idle) {
    case PowerPinIdle::Low:
      return "low";
    case PowerPinIdle::High:
      return "high";
    case PowerPinIdle::Undefined:
      return "undefined";
    case PowerPinIdle::NotChecked:
      break;
  }
  return "not_checked";
}

}  // namespace

void formatLadderToken(char* buf, const size_t size) {
  buf[0] = '\0';
  if (!BoardConfig::powerPolicy().ladderPresenceProbe) {
    snprintf(buf, size, "not_probed");
    return;
  }
  const InputManager::LadderPresence p = InputManager::ladderPresence();
  for (int i = 0; i < 2; ++i) {
    if (p.pin[i] < 0) continue;
    appendf(buf, size, "%sIO%d:%s", buf[0] == '\0' ? "" : ",", p.pin[i],
            (p.absentMask & (1u << i)) != 0 ? "off" : "fitted");
  }
  appendf(buf, size, "@%s", p.probed ? "probed" : p.cached ? "cached" : "none");
}

bool ladderOff(const int index) {
  if (index < 0 || index > 1 || !BoardConfig::powerPolicy().ladderPresenceProbe) return false;
  return (InputManager::ladderPresence().absentMask & (1u << index)) != 0;
}

void formatPowerIdleToken(char* buf, const size_t size) {
  const auto check = freeink::PowerManager::powerPinCheck();
  if (check.idle == PowerPinIdle::NotChecked) {
    snprintf(buf, size, "not_checked");
    return;
  }
  snprintf(buf, size, "%s:%umV", powerIdleKey(check.idle), static_cast<unsigned>(check.mv));
}

bool powerIdleFault() { return freeink::PowerManager::powerPinCheck().idle == PowerPinIdle::Undefined; }

void formatTouchIdToken(char* buf, const size_t size) {
  if (!BoardConfig::touchIdentityCheck()) {
    snprintf(buf, size, "off");
    return;
  }
  const auto d = TestSuiteAccess::input(gpio).touchDiagnostics();
  if (d.idRejected) {
    if (d.idRead) {
      snprintf(buf, size, "rejected:%02x", d.id[0]);
    } else {
      snprintf(buf, size, "rejected:cached");
    }
  } else if (d.active) {
    if (d.idRead) {
      snprintf(buf, size, "ok:%02x", d.id[0]);
    } else {
      snprintf(buf, size, "ok");
    }
  } else {
    snprintf(buf, size, "none");
  }
}

bool touchIdRejected() {
  return BoardConfig::touchIdentityCheck() && TestSuiteAccess::input(gpio).touchDiagnostics().idRejected;
}

void formatRtcIdToken(char* buf, const size_t size) {
  if (!BoardConfig::rtcIdentityCheck()) {
    snprintf(buf, size, "off");
  } else if (rtc::driver().rejectedAddress() != 0) {
    snprintf(buf, size, "rejected@0x%02x", rtc::driver().rejectedAddress());
  } else if (rtc::present() && rtc::driver().type() == BoardConfig::RtcType::Ds3231) {
    snprintf(buf, size, "ok");
  } else {
    snprintf(buf, size, "none");
  }
}

unsigned rtcRejectedAddress() { return rtc::driver().rejectedAddress(); }

void drawBootProbes(TestPage& page) {
  char value[96] = "";
  if (!BoardConfig::powerPolicy().ladderPresenceProbe) {
    snprintf(value, sizeof(value), "not tested by this profile");
  } else {
    const InputManager::LadderPresence p = InputManager::ladderPresence();
    for (int i = 0; i < 2; ++i) {
      if (p.pin[i] < 0) continue;
      const bool off = (p.absentMask & (1u << i)) != 0;
      appendf(value, sizeof(value), "%sIO%d %s", value[0] == '\0' ? "" : ", ", p.pin[i],
              off ? "OFF (no pull-up)" : "fitted");
      if (p.probed)
        appendf(value, sizeof(value), " %u/%u", static_cast<unsigned>(p.pullDownMv[i]),
                static_cast<unsigned>(p.pullUpMv[i]));
    }
    if (p.cached) appendf(value, sizeof(value), " (before the sleep)");
  }
  page.row("Key ladders (mV pulled down/up)", value);

  const auto check = freeink::PowerManager::powerPinCheck();
  switch (check.idle) {
    case PowerPinIdle::Undefined:
      page.rowf("Power button idle", "IO%d %u mV: BETWEEN LEVELS, wake off", check.pin,
                static_cast<unsigned>(check.mv));
      break;
    case PowerPinIdle::Low:
      page.rowf("Power button idle", "IO%d %u mV (low)", check.pin, static_cast<unsigned>(check.mv));
      break;
    case PowerPinIdle::High:
      page.rowf("Power button idle", "IO%d %u mV (held at boot)", check.pin, static_cast<unsigned>(check.mv));
      break;
    case PowerPinIdle::NotChecked:
      page.row("Power button idle", "not checked (deep-sleep wake)");
      break;
  }

  char touchId[24];
  char rtcId[24];
  formatTouchIdToken(touchId, sizeof(touchId));
  formatRtcIdToken(rtcId, sizeof(rtcId));
  page.rowf("Touch chip id / RTC pattern", "%s / %s", touchId, rtcId);
}

}  // namespace testsuite
#endif

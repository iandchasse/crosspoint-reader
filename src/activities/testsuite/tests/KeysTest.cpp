#if CROSSPOINT_TEST_SUITE
// T3: the eight ladder keys and the power button (bring-up B5). The keys are the
// test's input, so the logical button events come straight from HalGPIO
// (BTN_* indices, as in the button remap screen) and each press is matched to
// its physical switch by sampling the ladder the way InputManager does (one raw
// read plus AdcLadderConfig::oversample calibrated reads under the shared ADC
// lock). No pin mode or pull is ever set on the ladders or the power button.
//
// Three modes: Keys (every switch, its levels and press count, the power
// button's clicks and presses caught by the loop's idle slices), Chords
// (each two-key chord of §9.1, judged from the level the ladder held), and
// Hands off (any key event is a phantom), each with the radio on or off. A
// ladder the boot test found with no pull-up is off: its keys are not expected.
#include <AdcLadderDecode.h>
#include <Arduino.h>
#include <BatteryMonitor.h>
#include <BoardConfig.h>
#include <HalGPIO.h>
#include <HalPowerManager.h>
#include <Logging.h>
#include <Memory.h>
#include <SharedAdc.h>
#include <WiFi.h>

#include <climits>
#include <cstdio>
#include <cstring>

#include "BootProbes.h"
#include "CrossPointSettings.h"
#include "Supply.h"
#include "TestFactories.h"
#include "TestNames.h"
#include "TextBuffer.h"
#include "activities/testsuite/TestCatalog.h"
#include "activities/testsuite/TestScreen.h"
#include "activities/testsuite/TestSuite.h"

namespace testsuite {
namespace {

namespace ld = freeink::input_detail;
namespace lb = BoardConfig::LadderButton;

constexpr int LADDERS = 2;
constexpr int WINDOWS = 6;        // BoardConfig::AdcLadder::keys
constexpr int UNCLEAN = WINDOWS;  // chord bookkeeping slot for readings outside every window
constexpr int LOGICAL = 7;        // BTN_BACK .. BTN_POWER
constexpr uint8_t POWER = HalGPIO::BTN_POWER;
constexpr uint8_t LADDER_BUTTONS = (1U << POWER) - 1U;
constexpr int NO_WINDOW = -1;

constexpr int MARGIN_MIN_MV = 25;              // closer to a window edge is marginal (proposal)
constexpr unsigned long IDLE_SAMPLE_MS = 500;  // ladder readout while no key is down
constexpr unsigned long CONTACT_END_MS = 150;  // chords: released this long ends a contact
constexpr unsigned long CONTACT_MIN_MS = 150;  // chords: a shorter contact is a bounce
constexpr unsigned long HANDS_OFF_MIN_MS = 30000;
// Hands off: idle readings in no window and not released (a released level under
// AdcLadderConfig::releasedMinMv, btn-idle-topcode). One is a key's release edge.
constexpr unsigned UNCLEAN_IDLE_ALLOWED = 1;
constexpr unsigned long HANDS_OFF_REDRAW_MS = 10000;

constexpr uint8_t keyBit(const int b) { return static_cast<uint8_t>(1U << b); }

constexpr const char* BUTTON_NAMES[LOGICAL] = {"BACK", "OK", "LEFT", "RIGHT", "UP", "DOWN", "POWER"};

// The switches behind the profile's windows, in window order (HARDWARE.md
// §9.1). A window whose button does not match falls back to the logical name.
struct KeyLabel {
  uint8_t button;
  const char* name;
  const char* code;  // results.csv
};
constexpr KeyLabel BOARD_KEYS[LADDERS][4] = {{{lb::Right, "RIGHT  SW2", "R"},
                                              {lb::Left, "LEFT  SW3", "L"},
                                              {lb::Confirm, "OK  SW8", "OK"},
                                              {lb::Back, "BACK  SW9", "BK"}},
                                             {{lb::Down, "DOWN(1)  SW1", "D1"},
                                              {lb::Up, "UP(1)  SW4", "U1"},
                                              {lb::Down, "DOWN(2)  SW5", "D2"},
                                              {lb::Up, "UP(2)  SW7", "U2"}}};

// Chords of §9.1 and what the ladder must make of them: a key (bit) or no key
// (REJECTED). LEFT+BACK can land under LEFT's window after ADC error, so it may
// also be rejected; the 100-ohm keys (RIGHT, DOWN(1)) win any chord.
constexpr uint8_t REJECTED = 1U << 7;
struct Chord {
  const char* keys;
  const char* code;
  uint8_t ladder;
  uint8_t accept;
  const char* expect;
};
constexpr Chord CHORDS[] = {
    {"LEFT + BACK", "L+BK", 0, static_cast<uint8_t>(keyBit(lb::Left) | REJECTED), "LEFT or none"},
    {"UP(1) + UP(2)", "U1+U2", 1, static_cast<uint8_t>(keyBit(lb::Up) | REJECTED), "UP or none"},
    {"RIGHT + OK", "R+OK", 0, keyBit(lb::Right), "RIGHT"},
    {"DOWN(1) + UP(2)", "D1+U2", 1, keyBit(lb::Down), "DOWN"},
    {"LEFT + OK", "L+OK", 0, REJECTED, "none"},
    {"OK + BACK", "OK+BK", 0, REJECTED, "none"},
};
constexpr int CHORD_COUNT = static_cast<int>(sizeof(CHORDS) / sizeof(CHORDS[0]));

constexpr char KEYS_INTRO[] =
    "Press each key the same number of times (50 to count). POWER: click; a 0.4 s hold sleeps. Idle path: wait 3 s, "
    "tap.";
constexpr char CHORDS_INTRO[] =
    "Press the two keys of the marked chord together, hold about half a second, release. Each contact is judged, "
    "then the next chord comes up.";
constexpr char HANDS_OFF_INTRO[] =
    "Leave every key alone for at least 30 s: any key event now is a phantom. Repeat with the radio on.";

struct Sample {
  int mv = 0;
  int raw = 0;
  int window = NO_WINDOW;  // index into the ladder's keys, or NO_WINDOW
  bool released = true;
};

// InputManager's sampling and decode (AdcLadderDecode.h), plus which window
// matched: the first one holding the reading, as decodeAdcLadder() picks.
Sample sampleLadder(const BoardConfig::AdcLadder& ladder, const BoardConfig::AdcLadderConfig& cfg) {
  Sample s;
  if (ladder.pin < 0) return s;
  const auto pin = static_cast<uint8_t>(ladder.pin);
  const uint8_t n = cfg.oversample == 0 ? 1 : cfg.oversample;
  int32_t sum = 0;
  {
    const freeink::adc::Lock lock;
    s.raw = analogRead(pin);
    for (uint8_t i = 0; i < n; ++i) sum += static_cast<int32_t>(analogReadMilliVolts(pin));
  }
  s.mv = static_cast<int>(sum / n);
  const int decoded = ld::decodeAdcLadder(ladder.keys, ladder.count, s.mv, s.raw, cfg.releasedMinMv);
  s.released = decoded == ld::ADC_LADDER_RELEASED;
  if (decoded >= 0) {
    for (uint8_t i = 0; i < ladder.count; ++i) {
      const int lo = ladder.keys[i].minMv == 0 ? INT_MIN : ladder.keys[i].minMv;
      if (s.mv >= lo && s.mv <= ladder.keys[i].maxMv) {
        s.window = i;
        break;
      }
    }
  }
  return s;
}

// Distance from the readings of a press to the nearer edge of its window. The
// bottom window (minMv 0) has no lower edge.
int windowMargin(const BoardConfig::AdcLadderKey& key, const int minMv, const int maxMv) {
  const int top = key.maxMv - maxMv;
  if (key.minMv == 0) return top;
  const int bottom = minMv - key.minMv;
  return bottom < top ? bottom : top;
}

class KeysTest final : public TestScreen {
 public:
  KeysTest(const TestInfo& info, GfxRenderer& renderer, MappedInputManager& mappedInput)
      : TestScreen(info, renderer, mappedInput) {}

 private:
  enum class Mode : uint8_t { Keys, Chords, HandsOff };

  struct KeyStats {
    uint16_t presses;
    int minMv;
    int maxMv;
    int worstMargin;
  };

  // One logical key from its press edge to its release edge.
  struct Press {
    bool active;
    unsigned long at;
    int8_t ladder;
    int8_t window;
    int minMv;
    int maxMv;
    bool afterIdle;  // came after IDLE_POWER_SAVING_MS without input
    bool lowClock;   // the loop ran at the idle clock just before it
  };

  struct Contact {
    bool active;
    unsigned long start;
    unsigned long lastAt;
    unsigned long ms[WINDOWS + 1];
    int32_t mvSum[WINDOWS + 1];
    uint16_t samples[WINDOWS + 1];
    uint8_t sdkMask;  // logical keys HalGPIO reported during the contact
  };

  struct ChordResult {
    bool done;
    bool pass;
    int8_t outcome;  // BTN_* or -1: no key
    int mv;
    unsigned long steadyMs;
  };

  bool keysAreTestInput() const override { return true; }

  void onStart() override {
    const unsigned long now = millis();
    powerHoldLimitMs = SETTINGS.getPowerButtonDuration();
    quietSince = now;
    tickMhz = getCpuFrequencyMhz();
    usbHost = usbHostAttached();
    radioOn = WiFi.getMode() != WIFI_MODE_NULL;
    resetKeys();
    resetChords();
    resetWatch(now);
    sampleBoth(now, 0);
  }

  const char* slotLabel(const Slot slot) const override {
    switch (slot) {
      case Slot::Confirm:
        return "Record";
      case Slot::Left:
        return "Mode";
      case Slot::Right:
        return radioOn ? "Radio off" : "Radio on";
      default:
        return nullptr;
    }
  }

  void onSlot(const Slot slot) override {
    // A key held for an action is not a chord: drop the contact it opened.
    if (mode == Mode::Chords && contact.active) {
      contact = {};
      skipContact = true;
    }
    const unsigned long now = millis();
    switch (slot) {
      case Slot::Confirm:
        recordRun(now);
        break;
      case Slot::Left:
        mode = mode == Mode::Keys ? Mode::Chords : mode == Mode::Chords ? Mode::HandsOff : Mode::Keys;
        if (mode == Mode::Chords) resetChords();
        if (mode == Mode::HandsOff) resetWatch(now);
        LOG_INF("TEST", "T3 mode %s", modeName());
        break;
      case Slot::Right:
        setRadio(!radioOn);
        break;
      default:
        return;
    }
    requestUpdate();
  }

  void setRadio(const bool on) {
    if (on) {
      // bat-wifi-gate, as T15: no radio on the cell below the cutoff.
      const Supply supply = readSupply();
      const auto& policy = BoardConfig::powerPolicy();
      if (policy.lowBatteryCutoffMv != 0 && supply.onBattery() && supply.pPlusMv < policy.lowBatteryCutoffMv) {
        LOG_INF("TEST", "T3: radio refused on the cell at %u mV", supply.pPlusMv);
        record(Verdict::Skip, "mode=radio reason=battery_gate pplus=%u gate=%u", supply.pPlusMv,
               policy.lowBatteryCutoffMv);
        return;
      }
      // An idle station: the radio is up, no network is joined.
      WiFi.mode(WIFI_STA);
    } else {
      WiFi.disconnect(true);
      WiFi.mode(WIFI_OFF);
    }
    radioOn = WiFi.getMode() != WIFI_MODE_NULL;
    LOG_INF("TEST", "T3 radio %s", radioOn ? "on (idle station)" : "off");
  }

  void onTick() override {
    const unsigned long now = millis();
    usbHost = usbHostAttached();
    uint8_t pressed = 0;
    uint8_t released = 0;
    uint8_t down = 0;
    for (uint8_t b = 0; b < LOGICAL; ++b) {
      if (gpio.wasPressed(b)) pressed |= keyBit(b);
      if (gpio.wasReleased(b)) released |= keyBit(b);
      if (gpio.isPressed(b)) down |= keyBit(b);
    }
    // A hold that ran a footer action ends without a release reaching the test.
    for (uint8_t b = 0; b < LOGICAL; ++b) {
      if (press[b].active && (down & keyBit(b)) == 0 && (released & keyBit(b)) == 0) press[b].active = false;
    }
    for (uint8_t b = 0; b < LOGICAL; ++b) {
      if ((pressed & keyBit(b)) != 0) beginPress(b, now);
    }
    if (mode == Mode::Chords) {
      sampleChord(now);
    } else if (((down | pressed | released) & LADDER_BUTTONS) != 0 || now - sampledAt >= IDLE_SAMPLE_MS) {
      sampleBoth(now, down);
    }
    bool redraw = false;
    for (uint8_t b = 0; b < LOGICAL; ++b) {
      if ((released & keyBit(b)) != 0) redraw = endPress(b, now) || redraw;
    }
    powerLevelHigh = BoardConfig::ACTIVE.input.power >= 0 && digitalRead(BoardConfig::ACTIVE.input.power) == HIGH;
    if (mode == Mode::HandsOff && now - drawnAt >= HANDS_OFF_REDRAW_MS) redraw = true;
    if (redraw) {
      drawnAt = now;
      requestUpdate();
    }
    // Checked by the next tick's presses: the last key or touch input before them.
    quietSince = now - msSinceInput();
    tickMhz = getCpuFrequencyMhz();
  }

  void beginPress(const uint8_t b, const unsigned long now) {
    Press& p = press[b];
    p.active = true;
    p.at = now;
    p.ladder = -1;
    p.window = NO_WINDOW;
    p.minMv = INT_MAX;
    p.maxMv = INT_MIN;
    p.afterIdle = now - quietSince >= HalPowerManager::IDLE_POWER_SAVING_MS;
    p.lowClock = tickMhz <= static_cast<uint32_t>(HalPowerManager::LOW_POWER_FREQ);
    if (b == POWER) ++powerPressEdges;
    if (mode == Mode::Chords && contact.active) contact.sdkMask |= keyBit(b);
  }

  // Returns true when the screen should repaint.
  bool endPress(const uint8_t b, const unsigned long now) {
    if (b == POWER) {
      ++powerReleaseEdges;
      lastPowerHeldMs = gpio.getPowerButtonHeldTime();
    }
    Press& p = press[b];
    if (!p.active) return b == POWER;
    p.active = false;
    const unsigned long held = now - p.at;
    if (mode == Mode::HandsOff) {
      ++phantoms;
      phantomMask |= keyBit(b);
      LOG_ERR("TEST", "T3 phantom %s event (held %lu ms)", BUTTON_NAMES[b], held);
      return true;
    }
    if (mode == Mode::Chords) return false;
    if (b == POWER) {
      if (lastPowerHeldMs < powerHoldLimitMs) ++powerClicks;
    } else if (p.window != NO_WINDOW) {
      KeyStats& k = stats[p.ladder][p.window];
      ++k.presses;
      if (p.minMv < k.minMv) k.minMv = p.minMv;
      if (p.maxMv > k.maxMv) k.maxMv = p.maxMv;
      const auto& key = BoardConfig::ACTIVE.adcLadders.ladders[p.ladder].keys[p.window];
      const int margin = windowMargin(key, p.minMv, p.maxMv);
      if (margin < k.worstMargin) k.worstMargin = margin;
    } else {
      ++unattributed;
      LOG_ERR("TEST", "T3 %s press (%lu ms) with no ladder reading in its window", BUTTON_NAMES[b], held);
    }
    if (p.afterIdle) {
      ++idlePresses;
      if (p.lowClock) ++idleLowClock;
      if (held < shortestIdleMs) shortestIdleMs = held;
    }
    return true;
  }

  // Keys and hands off: both ladders, attributing every open press to the
  // window its ladder reads.
  void sampleBoth(const unsigned long now, const uint8_t down) {
    const auto& cfg = BoardConfig::ACTIVE.adcLadders;
    sampledAt = now;
    for (int l = 0; l < LADDERS; ++l) {
      const auto& ladder = cfg.ladders[l];
      const Sample s = sampleLadder(ladder, cfg);
      live[l] = s;
      if (ladder.pin < 0) continue;
      if (s.released) {
        if (s.mv < releasedMinMv[l]) releasedMinMv[l] = s.mv;
        if (s.mv > releasedMaxMv[l]) releasedMaxMv[l] = s.mv;
        topCoded[l] = s.raw >= ld::ADC_LADDER_RAW_SATURATED;
        continue;
      }
      if (s.window == NO_WINDOW) {
        if ((down & LADDER_BUTTONS) == 0) ++uncleanIdle;
        continue;
      }
      const uint8_t button = ladder.keys[s.window].button;
      if (button >= POWER) continue;
      Press& p = press[button];
      if (!p.active) continue;
      if (p.window == NO_WINDOW) {
        p.ladder = static_cast<int8_t>(l);
        p.window = static_cast<int8_t>(s.window);
      }
      if (p.ladder == l && p.window == s.window) {
        if (s.mv < p.minMv) p.minMv = s.mv;
        if (s.mv > p.maxMv) p.maxMv = s.mv;
      }
    }
  }

  // Chords: the prompted chord's ladder, every pass (every IDLE_SAMPLE_MS once
  // all are done).
  void sampleChord(const unsigned long now) {
    if (chordIndex >= CHORD_COUNT && now - sampledAt < IDLE_SAMPLE_MS) return;
    const auto& cfg = BoardConfig::ACTIVE.adcLadders;
    const int l = chordIndex < CHORD_COUNT ? CHORDS[chordIndex].ladder : 0;
    const Sample s = sampleLadder(cfg.ladders[l], cfg);
    live[l] = s;
    sampledAt = now;
    if (s.released) {
      if (releasedSince == 0) releasedSince = now;
      if (now - releasedSince >= CONTACT_END_MS) {
        skipContact = false;
        if (contact.active) endContact(l);
      }
      return;
    }
    releasedSince = 0;
    if (skipContact || chordIndex >= CHORD_COUNT) return;
    if (!contact.active) {
      contact = {};
      contact.active = true;
      contact.start = now;
      contact.lastAt = now;
    }
    const int slot = s.window != NO_WINDOW ? s.window : UNCLEAN;
    contact.ms[slot] += now - contact.lastAt;
    contact.mvSum[slot] += s.mv;
    ++contact.samples[slot];
    contact.lastAt = now;
  }

  void endContact(const int ladderIndex) {
    contact.active = false;
    if (contact.lastAt - contact.start < CONTACT_MIN_MS || chordIndex >= CHORD_COUNT) return;
    // The level the ladder held longest is the chord's decode.
    int steady = 0;
    for (int i = 1; i <= UNCLEAN; ++i) {
      if (contact.ms[i] > contact.ms[steady]) steady = i;
    }
    const auto& ladder = BoardConfig::ACTIVE.adcLadders.ladders[ladderIndex];
    ChordResult& r = chords[chordIndex];
    r.done = true;
    r.steadyMs = contact.ms[steady];
    r.mv = contact.samples[steady] > 0 ? static_cast<int>(contact.mvSum[steady] / contact.samples[steady]) : 0;
    r.outcome = steady < UNCLEAN && steady < ladder.count ? static_cast<int8_t>(ladder.keys[steady].button) : -1;
    r.pass = (CHORDS[chordIndex].accept & (r.outcome >= 0 ? keyBit(r.outcome) : REJECTED)) != 0;
    // The keys HalGPIO committed on the way in and out show the press order.
    char reported[40] = "";
    for (uint8_t b = 0; b < LOGICAL; ++b) {
      if ((contact.sdkMask & keyBit(b)) != 0) {
        appendf(reported, sizeof(reported), "%s%s", reported[0] == '\0' ? "" : ",", BUTTON_NAMES[b]);
      }
    }
    LOG_INF("TEST", "T3 chord %s: %s at %d mV for %lu ms (%s); keys reported: %s", CHORDS[chordIndex].keys,
            r.outcome >= 0 ? BUTTON_NAMES[r.outcome] : "no key", r.mv, r.steadyMs, r.pass ? "ok" : "wrong",
            reported[0] != '\0' ? reported : "none");
    ++chordIndex;
    skipOffLadderChords();
    drawnAt = millis();
    requestUpdate();
  }

  void resetKeys() {
    for (auto& ladder : stats) {
      for (auto& k : ladder) k = {0, INT_MAX, INT_MIN, INT_MAX};
    }
    unattributed = 0;
    powerClicks = 0;
    powerPressEdges = 0;
    powerReleaseEdges = 0;
    idlePresses = 0;
    idleLowClock = 0;
    shortestIdleMs = ULONG_MAX;
    resetLevels();
  }

  void resetLevels() {
    for (int l = 0; l < LADDERS; ++l) {
      releasedMinMv[l] = INT_MAX;
      releasedMaxMv[l] = INT_MIN;
      topCoded[l] = false;
    }
    uncleanIdle = 0;
  }

  // Starts the chords again. The ladder counts only once it has read released:
  // a key still held from the hold that got here (Mode, Record) is no chord.
  void resetChords() {
    chordIndex = 0;
    for (auto& r : chords) r = {};
    contact = {};
    skipContact = true;
    releasedSince = 0;
    skipOffLadderChords();
  }

  // A chord on a ladder the boot test switched off (no pull-up) cannot be made.
  void skipOffLadderChords() {
    while (chordIndex < CHORD_COUNT && ladderOff(CHORDS[chordIndex].ladder)) ++chordIndex;
  }

  static int chordsExpected() {
    int expected = 0;
    for (const Chord& c : CHORDS) {
      if (!ladderOff(c.ladder)) ++expected;
    }
    return expected;
  }

  void resetWatch(const unsigned long now) {
    watchStart = now;
    phantoms = 0;
    phantomMask = 0;
    resetLevels();
  }

  const char* modeName() const {
    switch (mode) {
      case Mode::Chords:
        return "chords";
      case Mode::HandsOff:
        return "hands_off";
      case Mode::Keys:
        break;
    }
    return "keys";
  }

  // The switch name behind a window (board label when the profile agrees).
  static const KeyLabel* boardKey(const int l, const int w) {
    const auto& ladder = BoardConfig::ACTIVE.adcLadders.ladders[l];
    if (w >= 4 || w >= ladder.count || BOARD_KEYS[l][w].button != ladder.keys[w].button) return nullptr;
    return &BOARD_KEYS[l][w];
  }
  static const char* keyName(const int l, const int w) {
    const KeyLabel* k = boardKey(l, w);
    return k != nullptr ? k->name : BUTTON_NAMES[BoardConfig::ACTIVE.adcLadders.ladders[l].keys[w].button];
  }
  static const char* keyCode(const int l, const int w) {
    const KeyLabel* k = boardKey(l, w);
    return k != nullptr ? k->code : BUTTON_NAMES[BoardConfig::ACTIVE.adcLadders.ladders[l].keys[w].button];
  }

  void recordRun(const unsigned long now) {
    switch (mode) {
      case Mode::Keys:
        recordKeys();
        resetKeys();
        break;
      case Mode::Chords:
        recordChords();
        resetChords();
        break;
      case Mode::HandsOff:
        recordWatch(now);
        resetWatch(now);
        break;
    }
  }

  void recordKeys() {
    const auto& cfg = BoardConfig::ACTIVE.adcLadders;
    char counts[64] = "";
    char missing[32] = "";
    char marginal[56] = "";
    int worst = INT_MAX;
    const char* worstCode = "-";
    bool countsDiffer = false;
    int firstCount = -1;
    for (int l = 0; l < LADDERS; ++l) {
      if (ladderOff(l)) continue;
      for (int w = 0; w < cfg.ladders[l].count && w < WINDOWS; ++w) {
        const KeyStats& k = stats[l][w];
        appendf(counts, sizeof(counts), "%s%s=%u", counts[0] == '\0' ? "" : ",", keyCode(l, w), k.presses);
        if (k.presses == 0) appendf(missing, sizeof(missing), "%s%s", missing[0] == '\0' ? "" : ",", keyCode(l, w));
        if (k.presses > 0 && k.worstMargin < MARGIN_MIN_MV) {
          appendf(marginal, sizeof(marginal), "%s%s(%d)", marginal[0] == '\0' ? "" : ",", keyCode(l, w), k.worstMargin);
        }
        if (k.presses > 0 && k.worstMargin < worst) {
          worst = k.worstMargin;
          worstCode = keyCode(l, w);
        }
        if (firstCount < 0) firstCount = k.presses;
        if (k.presses != firstCount) countsDiffer = true;
      }
    }
    appendf(counts, sizeof(counts), ",PWR=%u", powerClicks);
    if (powerClicks == 0) appendf(missing, sizeof(missing), "%sPWR", missing[0] == '\0' ? "" : ",");
    if (static_cast<int>(powerClicks) != firstCount) countsDiffer = true;

    char fails[64] = "";
    const auto fail = [&fails](const char* what) { addFailure(fails, sizeof(fails), what); };
    if (missing[0] != '\0') fail("missing");
    if (marginal[0] != '\0') fail("marginal");
    if (countsDiffer) fail("counts_differ");
    if (unattributed > 0) fail("unattributed");
    if (powerPressEdges != powerReleaseEdges) fail("power_edges");
    if (powerIdleFault()) fail("power_idle");
    char ladders[40];
    char powerIdle[24];
    formatLadderToken(ladders, sizeof(ladders));
    formatPowerIdleToken(powerIdle, sizeof(powerIdle));
    record(fails[0] == '\0' ? Verdict::Pass : Verdict::Fail,
           "mode=keys radio=%d host=%d usbstat=%s ladders=%s pwr_idle=%s counts=%s worst=%d(%s) marginal=%s missing=%s "
           "released=%d,%d idle=%u lowclk=%u shortest=%lums noread=%u pwr_edges=%u/%u fails=%s",
           radioOn ? 1 : 0, usbHost ? 1 : 0, chargeStateName(BatteryMonitor::chargeStatus()), ladders, powerIdle,
           counts, worst == INT_MAX ? 0 : worst, worstCode, marginal[0] != '\0' ? marginal : "none",
           missing[0] != '\0' ? missing : "none", releasedMinMv[0] == INT_MAX ? 0 : releasedMinMv[0],
           releasedMinMv[1] == INT_MAX ? 0 : releasedMinMv[1], idlePresses, idleLowClock,
           shortestIdleMs == ULONG_MAX ? 0UL : shortestIdleMs, unattributed, powerPressEdges, powerReleaseEdges,
           fails[0] == '\0' ? "none" : fails);
  }

  void recordChords() {
    char results[128] = "";
    int done = 0;
    bool anyWrong = false;
    for (int i = 0; i < CHORD_COUNT; ++i) {
      const ChordResult& r = chords[i];
      if (!r.done) continue;
      ++done;
      anyWrong = anyWrong || !r.pass;
      appendf(results, sizeof(results), "%s%s=%s@%d:%s", results[0] == '\0' ? "" : ",", CHORDS[i].code,
              r.outcome >= 0 ? BUTTON_NAMES[r.outcome] : "none", r.mv, r.pass ? "ok" : "wrong");
    }
    const int expected = chordsExpected();
    const Verdict verdict = anyWrong ? Verdict::Fail : done == expected ? Verdict::Pass : Verdict::Info;
    record(verdict, "mode=chords radio=%d done=%d/%d results=%s fails=%s", radioOn ? 1 : 0, done, expected,
           results[0] != '\0' ? results : "none",
           anyWrong           ? "wrong_decode"
           : done == expected ? "none"
                              : "incomplete");
  }

  void recordWatch(const unsigned long now) {
    const unsigned long watched = now - watchStart;
    char keys[48] = "";
    for (uint8_t b = 0; b < LOGICAL; ++b) {
      if ((phantomMask & keyBit(b)) != 0)
        appendf(keys, sizeof(keys), "%s%s", keys[0] == '\0' ? "" : ",", BUTTON_NAMES[b]);
    }
    char fails[48] = "";
    if (phantoms > 0) addFailure(fails, sizeof(fails), "phantom");
    // The released level (btn-idle-topcode): idle readings between windows, or a
    // ladder that never read released through a full watch.
    if (uncleanIdle > UNCLEAN_IDLE_ALLOWED) addFailure(fails, sizeof(fails), "unclean_idle");
    const auto& cfg = BoardConfig::ACTIVE.adcLadders;
    char released[48] = "";
    for (int l = 0; l < LADDERS; ++l) {
      if (cfg.ladders[l].pin < 0) continue;
      const bool seen = releasedMinMv[l] != INT_MAX;
      appendf(released, sizeof(released), "%sIO%d:%d-%d%s", released[0] == '\0' ? "" : ",", cfg.ladders[l].pin,
              seen ? releasedMinMv[l] : 0, seen ? releasedMaxMv[l] : 0, topCoded[l] ? ":top" : "");
      if (!seen && watched >= HANDS_OFF_MIN_MS && strstr(fails, "never_released") == nullptr) {
        addFailure(fails, sizeof(fails), "never_released");
      }
    }
    const Verdict verdict = fails[0] != '\0'             ? Verdict::Fail
                            : watched < HANDS_OFF_MIN_MS ? Verdict::Info
                                                         : Verdict::Pass;
    char ladders[40];
    formatLadderToken(ladders, sizeof(ladders));
    record(verdict,
           "mode=hands_off radio=%d host=%d ladders=%s watch_s=%lu phantoms=%u keys=%s unclean=%u released_floor=%u "
           "released=%s fails=%s",
           radioOn ? 1 : 0, usbHost ? 1 : 0, ladders, watched / 1000UL, phantoms, keys[0] != '\0' ? keys : "none",
           uncleanIdle, cfg.releasedMinMv, released[0] != '\0' ? released : "none",
           fails[0] != '\0'             ? fails
           : watched < HANDS_OFF_MIN_MS ? "too_short"
                                        : "none");
  }

  // Both ladders now, with the lowest released reading in brackets.
  void drawLadders(TestPage& page) const {
    const auto& cfg = BoardConfig::ACTIVE.adcLadders;
    char value[64] = "";
    for (int l = 0; l < LADDERS; ++l) {
      if (cfg.ladders[l].pin < 0) continue;
      appendf(value, sizeof(value), "%sIO%d %d", value[0] == '\0' ? "" : ", ", cfg.ladders[l].pin, live[l].mv);
      if (releasedMinMv[l] != INT_MAX) appendf(value, sizeof(value), " (%d)", releasedMinMv[l]);
    }
    page.row("Ladders mV (min released)", value);
  }

  void drawKeys(TestPage& page) const {
    const auto& cfg = BoardConfig::ACTIVE.adcLadders;
    page.paragraph(KEYS_INTRO, 3);
    page.setCompact(true);
    char ladders[40];
    char powerIdle[24];
    formatLadderToken(ladders, sizeof(ladders));
    formatPowerIdleToken(powerIdle, sizeof(powerIdle));
    page.rowf("Boot checks", "%s, power %s", ladders, powerIdle);
    for (int l = 0; l < LADDERS; ++l) {
      if (ladderOff(l)) {
        page.row(l == 0 ? "Bottom ladder" : "Side ladder", "off: no pull-up fitted (boot test)");
        continue;
      }
      for (int w = 0; w < cfg.ladders[l].count && w < WINDOWS; ++w) {
        const KeyStats& k = stats[l][w];
        if (k.presses == 0) {
          page.rowf(keyName(l, w), "- (%u-%u mV)", cfg.ladders[l].keys[w].minMv, cfg.ladders[l].keys[w].maxMv);
        } else {
          page.rowf(keyName(l, w), "%ux, %d-%d mV, margin %d%s", k.presses, k.minMv, k.maxMv, k.worstMargin,
                    k.worstMargin < MARGIN_MIN_MV ? " LOW" : "");
        }
      }
    }
    char powerLabel[24];
    snprintf(powerLabel, sizeof(powerLabel), "POWER  IO%d", BoardConfig::ACTIVE.input.power);
    page.rowf(powerLabel, "%u clicks, edges %u/%u, last %lu ms, %s", powerClicks, powerPressEdges, powerReleaseEdges,
              lastPowerHeldMs, powerLevelHigh ? "HIGH" : "LOW");
    drawLadders(page);
    if (usbHost) {
      page.row("Idle-slice presses", "none: a USB host keeps the loop awake");
    } else if (idlePresses == 0) {
      page.row("Idle-slice presses", "none yet: wait 3 s, tap a key");
    } else {
      page.rowf("Idle-slice presses", "%u, %u at %d MHz, shortest %lu ms", idlePresses, idleLowClock,
                HalPowerManager::LOW_POWER_FREQ, shortestIdleMs);
    }
    if (unattributed > 0) page.rowf("Presses with no reading", "%u", unattributed);
    page.setCompact(false);
  }

  void drawChords(TestPage& page) const {
    page.paragraph(CHORDS_INTRO, 4);
    page.setCompact(true);
    for (int i = 0; i < CHORD_COUNT; ++i) {
      const ChordResult& r = chords[i];
      char label[32];
      snprintf(label, sizeof(label), "%s%s", i == chordIndex ? "> " : "", CHORDS[i].keys);
      if (ladderOff(CHORDS[i].ladder)) {
        page.row(label, "ladder off");
      } else if (!r.done) {
        page.rowf(label, "%s%s", i == chordIndex ? "now, expect " : "expect ", CHORDS[i].expect);
      } else {
        page.rowf(label, "%s at %d mV: %s", r.outcome >= 0 ? BUTTON_NAMES[r.outcome] : "none", r.mv,
                  r.pass ? "ok" : "WRONG");
      }
    }
    if (chordIndex >= CHORD_COUNT) page.row("All chords done", "Record");
    drawLadders(page);
    page.setCompact(false);
  }

  void drawHandsOff(TestPage& page) const {
    page.paragraph(HANDS_OFF_INTRO, 3);
    page.setCompact(true);
    const unsigned long t = (millis() - watchStart) / 1000UL;
    page.rowf("Watching", "%lu:%02lu%s", t / 60UL, t % 60UL, t * 1000UL < HANDS_OFF_MIN_MS ? " (30 s minimum)" : "");
    page.rowf("Phantom key events", "%u%s", phantoms, phantoms > 0 ? " (FAIL)" : "");
    if (phantomMask != 0) {
      char keys[48] = "";
      for (uint8_t b = 0; b < LOGICAL; ++b) {
        if ((phantomMask & keyBit(b)) != 0) appendf(keys, sizeof(keys), "%s ", BUTTON_NAMES[b]);
      }
      page.row("Keys", keys);
    }
    page.rowf("Readings off every level, no key", "%u", uncleanIdle);
    drawLadders(page);
    // How close the idle level comes to the released floor, and whether the raw
    // reading tops out (btn-idle-topcode).
    const auto& cfg = BoardConfig::ACTIVE.adcLadders;
    char label[40];
    snprintf(label, sizeof(label), "Released (floor %u)", cfg.releasedMinMv);
    char value[64] = "";
    for (int l = 0; l < LADDERS; ++l) {
      if (cfg.ladders[l].pin < 0 || releasedMinMv[l] == INT_MAX) continue;
      appendf(value, sizeof(value), "%sIO%d %d-%d%s", value[0] == '\0' ? "" : ", ", cfg.ladders[l].pin,
              releasedMinMv[l], releasedMaxMv[l], topCoded[l] ? " top" : "");
    }
    page.row(label, value[0] != '\0' ? value : "no reading yet");
    page.setCompact(false);
  }

  void drawBody(TestPage& page) const override {
    char heading[40];
    snprintf(heading, sizeof(heading), "%s    radio %s",
             mode == Mode::Keys     ? "Keys"
             : mode == Mode::Chords ? "Chords"
                                    : "Hands off",
             radioOn ? "ON" : "off");
    page.heading(heading);
    switch (mode) {
      case Mode::Keys:
        drawKeys(page);
        break;
      case Mode::Chords:
        drawChords(page);
        break;
      case Mode::HandsOff:
        drawHandsOff(page);
        break;
    }
  }

  Mode mode = Mode::Keys;
  bool radioOn = false;
  bool usbHost = false;
  uint16_t powerHoldLimitMs = 400;
  uint32_t tickMhz = 0;
  unsigned long quietSince = 0;  // last key or touch input, as of the previous tick
  unsigned long sampledAt = 0;
  unsigned long drawnAt = 0;

  Press press[LOGICAL] = {};
  Sample live[LADDERS];
  int releasedMinMv[LADDERS] = {};
  int releasedMaxMv[LADDERS] = {};
  bool topCoded[LADDERS] = {};
  unsigned uncleanIdle = 0;

  KeyStats stats[LADDERS][WINDOWS] = {};
  unsigned unattributed = 0;
  unsigned powerClicks = 0;
  unsigned powerPressEdges = 0;
  unsigned powerReleaseEdges = 0;
  unsigned long lastPowerHeldMs = 0;
  bool powerLevelHigh = false;
  unsigned idlePresses = 0;
  unsigned idleLowClock = 0;
  unsigned long shortestIdleMs = ULONG_MAX;

  int chordIndex = 0;
  ChordResult chords[CHORD_COUNT] = {};
  Contact contact = {};
  unsigned long releasedSince = 0;  // the prompted ladder has read released since
  bool skipContact = false;         // ignore the ladder until it is released again

  unsigned long watchStart = 0;
  unsigned phantoms = 0;
  uint8_t phantomMask = 0;
};

}  // namespace

std::unique_ptr<Activity> createKeysTest(const TestInfo& info, GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<KeysTest>(info, renderer, mappedInput);
}

}  // namespace testsuite
#endif

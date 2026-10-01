#pragma once

// What the operator says about the board under test, for the checks the firmware
// cannot make itself (CROSSPOINT_TEST_SUITE builds only). Kept on the card in
// /.silkscreen-test/options.cfg, never under /.crosspoint, so it holds across
// boots and flashes; without a card it lasts until the next restart.
#if CROSSPOINT_TEST_SUITE
#include <cstddef>

namespace testsuite::options {

inline constexpr char OPTIONS_FILE[] = "/.silkscreen-test/options.cfg";

// The board was shortened at its cut line (HARDWARE.md §16): J6 and SW10 are
// gone, so P2's J6 scan and T9's J6 strip are N/A.
bool cutDown();
// UP(2) is the power button (HARDWARE.md §9.2, R72/R74 fitted, R36/R73 off): SW7
// drives IO18, and ladder 2's UP(2) window must never fire (T3).
bool up2IsPower();

// Each saves the file at once; false when it could not be written (the value
// still holds until the next restart). Turning cut-down on turns UP(2)-is-POWER
// on with it, its default there; either can be changed afterwards.
bool setCutDown(bool on);
bool setUp2IsPower(bool on);

// Reads the file. setup(), once the card is mounted and before the scratch
// sweep, which would remove an options.tmp a cut-short save left in its place;
// no file: both off.
void load();
// Whether the values came from, or went to, the card this boot.
bool onCard();
// "cut=1 up2pwr=1", for results.
void formatToken(char* buf, size_t size);

}  // namespace testsuite::options
#endif

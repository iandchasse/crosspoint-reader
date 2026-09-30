#pragma once

// UI text of the hardware test firmware (CROSSPOINT_TEST_SUITE builds only).
// English only, as a documented exception to the tr() rule (AGENTS.md rule 5,
// docs/silkscreen/TEST-SUITE.md): this is a bench tool whose screens mirror the
// serial log, and gen_i18n.py collects STR_ keys from every source file whatever
// its #if, so test-only keys would ship in every reader image. Screens the test
// firmware shares with the reader (popups, Wi-Fi, keyboard) keep tr(), with the
// language pinned to English.
#if CROSSPOINT_TEST_SUITE
namespace testsuite::text {
inline constexpr char MENU_TITLE[] = "Silkscreen hardware test";
inline constexpr char RESULTS_ON_CARD[] = "Results: /.silkscreen-test/results.csv";
inline constexpr char SD_NOT_MOUNTED[] = "SD not mounted: results on serial only";
inline constexpr char NOT_IMPLEMENTED[] = "Not implemented yet";
inline constexpr char NOT_IMPLEMENTED_BODY[] =
    "This test is not implemented yet in this build of the test firmware. Nothing was run and nothing was logged.";

inline constexpr char EXIT[] = "Exit";
inline constexpr char OPEN[] = "Open";
inline constexpr char UP[] = "Up";
inline constexpr char DOWN[] = "Down";
inline constexpr char HOLD_BACK_TO_EXIT[] = "Keys are part of this test: hold Back 1.5 s or tap Exit to leave.";

inline constexpr char RESULT[] = "Result";
inline constexpr char SAVED[] = "Saved to /.silkscreen-test/results.csv and the serial log";
inline constexpr char SERIAL_ONLY[] = "SD not mounted: in the serial log only";
inline constexpr char WRITE_FAILED[] = "Could not write results.csv: in the serial log only";

inline constexpr char LIGHT_LOAD_TITLE[] = "LED load connected?";
inline constexpr char LIGHT_LOAD_BODY[] =
    "This test drives the front-light boost. Confirm only if the J3 panel light or a J6 LED strip is connected: "
    "with no load the boost output rises to its over-voltage limit.";

inline constexpr char ASLEEP[] = "Test firmware asleep";
inline constexpr char WAKE_HINT[] = "Hold POWER to wake";
inline constexpr char SLEEP_FROM_TIMEOUT[] = "Sleep: timeout";
inline constexpr char SLEEP_FROM_REQUEST[] = "Sleep: power button or battery cutoff";
inline constexpr char BOARD_TAG[] = "Board tag";
}  // namespace testsuite::text
#endif

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
inline constexpr char HOLD_BACK_TO_EXIT[] =
    "Keys are part of this test. Hold a front key 1.5 s for the button above it (Back: Exit), or tap the button.";

inline constexpr char RESULT[] = "Result";
inline constexpr char SAVED[] = "Saved to /.silkscreen-test/results.csv and the serial log";
inline constexpr char SERIAL_ONLY[] = "SD not mounted: in the serial log only";
inline constexpr char WRITE_FAILED[] = "Could not write results.csv: in the serial log only";

inline constexpr char LIGHT_LOAD_TITLE[] = "LED load connected?";
inline constexpr char LIGHT_LOAD_BODY[] =
    "This test drives the front-light boost. Pick the load that is connected. With no load the boost output rises "
    "to its over-voltage limit, so pick No load if none is.";
inline constexpr char LIGHT_LOAD_NONE[] = "No load: cancel";
inline constexpr char LIGHT_LOAD_J3[] = "J3 panel light";
inline constexpr char LIGHT_LOAD_J6[] = "J6 LED strip";

inline constexpr char ASLEEP[] = "Test firmware asleep";
inline constexpr char WAKE_HINT[] = "Hold POWER to wake";
inline constexpr char SLEEP_FROM_TIMEOUT[] = "Sleep: timeout";
inline constexpr char SLEEP_FROM_REQUEST[] = "Sleep: power button or battery cutoff";
inline constexpr char BOARD_TAG[] = "Board tag";
inline constexpr char SLEEP_P5_PATH[] = "P5 path";
// MEAS-01 targets and the pin levels to probe while asleep (POWER.md §2, §4),
// left on the retained sleep screen. Lines fit a 480 px portrait width.
inline constexpr char SLEEP_CURRENT_HEADING[] = "Sleep current at J5, 3.8 V, USB and UART off";
inline constexpr const char* SLEEP_CURRENT_LINES[] = {
    "No touch tail: 58-73 uA",
    "FT6336U tail hibernated: 113-130 uA",
    "Wait 60 s, then average 60 s",
};
inline constexpr char SLEEP_PINS_HEADING[] = "Pins in sleep";
inline constexpr const char* SLEEP_PIN_LINES[] = {
    "3.3 V: IO47 IO14 IO48 IO11 IO38 IO39 IO10", "0 V: IO12 IO13 IO21 IO40 IO5-7 IO15-17",
    "IO42 < 0.385 V, SD_VDD (C36) < 0.5 V",      "Unheld, probe and note: IO1-4 IO8 IO9",
    "IO41: unheld (FT6336U), 0 V (GT911)",
};
}  // namespace testsuite::text
#endif

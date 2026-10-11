#if CROSSPOINT_TEST_SUITE
#include "TestCatalog.h"

#include <BoardConfig.h>
#include <Memory.h>

#include <cstring>
#include <iterator>

#include "PlaceholderTest.h"
#include "tests/TestFactories.h"

namespace testsuite {
namespace {

// The board options first, then the tests grouped by subsystem, in bring-up order
// within each group (PORTING.md B1-B10). The ids are stable: results.csv and the
// serial log use them.
constexpr TestInfo TESTS[] = {
    {"OPT", "Board options", "Cut-down board, UP(2) as POWER (kept on the card)", createBoardOptionsTest},
    {"P1", "System info", "Chip, memory, panel controller, reset records and boot checks", createSystemInfoTest},
    {"P2", "I2C bus scan", "Devices on the sensor bus (RTC, touch, J6)", createI2cScanTest},
    {"T1", "Battery", "BAT_MONIT (IO8) against a meter, cell detection", createBatteryTest},
    {"T2", "Charge status", "USB_STAT (IO9) ladder states and USB presence", createChargeTest},
    {"P4", "USB", "USB host and ladder presence, USB Drive", createUsbTest},
    {"T3", "Keys", "Eight ladder keys (mV and decode) and the power button", createKeysTest},
    {"T4", "Refresh modes", "Full, half and fast refreshes as run, and scope loops", createRefreshTest},
    {"T5", "Grayscale", "Anti-aliasing passes, every route the reader takes", createGrayscaleTest},
    {"T6", "Inversion", "Night-mode output polarity", createInversionTest},
    {"T7", "Orientation", "Four orientations and a bezel ruler", createOrientationTest},
    {"T8", "Panel SPI clock", "40 MHz display SPI trial", createSpiClockTest},
    {"T9", "Front light", "On/off, warm/cool, brightness, LED_MONIT, dVf, colour run", createFrontlightTest},
    {"T10", "Light trip", "Over-voltage trip at a lowered level; step 0: open load", createLightTripTest},
#if FREEINK_CAP_TOUCH
    {"T11", "Touch mapping", "Probe, raw coordinates, calibration and gestures", createTouchMappingTest},
    {"T12", "Touch sleep", "Controller sleep command (TP-01, TP-02)", createTouchSleepTest},
#else
    // The 3.68" variant has no touch: both screens say so and record a SKIP (NoTouchTest.h).
    {"T11", "Touch mapping", "No touch in this build", createTouchMappingTest},
    {"T12", "Touch sleep", "No touch in this build", createTouchSleepTest},
#endif
    {"T13", "RTC", "Presence, registers and time", createRtcTest},
    {"T14", "RTC across sleep", "Time kept through a deep sleep", createRtcSleepTest},
    {"P3", "SD card", "Card identity, bus, speed, a verified write, power cycles", createSdCardTest},
    {"T15", "Wi-Fi", "Connect and sync the clock over NTP", createWifiTest},
    {"U1", "Wi-Fi TX bursts", "Bounded TX bursts for the supply's brown-out sweep", createTxBurstTest},
    {"U2", "Sustained load", "Wi-Fi, card and refreshes on USB, for the LDO's temperature", createSustainedLoadTest},
    {"P5", "Sleep and wake", "Sleep paths, wake, sleep current procedure", createSleepWakeTest},
    {"P8", "Reader data", "Reader files on the card left untouched", createReaderDataTest},
};
static_assert(std::size(TESTS) <= MAX_TESTS, "raise MAX_TESTS");

}  // namespace

size_t testCount() { return std::size(TESTS); }

const TestInfo& testAt(const size_t index) { return TESTS[index < std::size(TESTS) ? index : 0]; }

size_t testIndex(const TestInfo& info) { return static_cast<size_t>(&info - TESTS); }

const TestInfo* findTest(const char* id) {
  for (const auto& test : TESTS) {
    if (strcmp(test.id, id) == 0) return &test;
  }
  return nullptr;
}

std::unique_ptr<Activity> createTest(const TestInfo& info, GfxRenderer& renderer, MappedInputManager& mappedInput) {
  if (info.create) return info.create(info, renderer, mappedInput);
  return makeUniqueNoThrow<PlaceholderTest>(info, renderer, mappedInput);
}

}  // namespace testsuite
#endif

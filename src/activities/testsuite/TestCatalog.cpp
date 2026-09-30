#if CROSSPOINT_TEST_SUITE
#include "TestCatalog.h"

#include <Memory.h>

#include <iterator>

#include "PlaceholderTest.h"
#include "tests/TestFactories.h"

namespace testsuite {
namespace {

// Grouped by subsystem, in bring-up order within each group (PORTING.md B1-B10).
// The ids are stable: results.csv and the serial log use them.
constexpr TestInfo TESTS[] = {
    {"P1", "System info", "Chip, memory, reset records, profile and boot checks", createSystemInfoTest},
    {"P2", "I2C bus scan", "Devices on the sensor bus (RTC, touch, J6)", createI2cScanTest},
    {"T1", "Battery", "BAT_MONIT (IO8) against a meter, cell detection", createBatteryTest},
    {"T2", "Charge status", "USB_STAT (IO9) ladder states and USB presence", createChargeTest},
    {"P4", "USB", "USB host and ladder presence, USB Drive", nullptr},
    {"T3", "Keys", "Eight ladder keys (mV and decode) and the power button", createKeysTest},
    {"T4", "Refresh modes", "Full, half and fast refreshes, as the panel ran them", createRefreshTest},
    {"T5", "Grayscale", "Anti-aliasing passes, every route the reader takes", createGrayscaleTest},
    {"T6", "Inversion", "Night-mode output polarity", createInversionTest},
    {"T7", "Orientation", "Four orientations and a bezel ruler", createOrientationTest},
    {"T8", "Panel SPI clock", "40 MHz display SPI trial", createSpiClockTest},
    {"T9", "Front light", "On/off, warm/cool, brightness and LED_MONIT", nullptr},
    {"T10", "Light trip", "LED over-voltage trip at a lowered level", nullptr},
    {"T11", "Touch mapping", "Probe, raw coordinates, corners and gestures", nullptr},
    {"T12", "Touch sleep", "Controller sleep command (TP-01, TP-02)", nullptr},
    {"T13", "RTC", "Presence, registers and time", nullptr},
    {"T14", "RTC across sleep", "Time kept through a deep sleep", nullptr},
    {"P3", "SD card", "Card identity, bus, speed and a verified write", nullptr},
    {"T15", "Wi-Fi", "Connect and sync the clock over NTP", nullptr},
    {"P5", "Sleep and wake", "Sleep paths, wake, sleep current procedure", nullptr},
    {"P8", "Reader data", "Reader files on the card left untouched", nullptr},
};
static_assert(std::size(TESTS) <= MAX_TESTS, "raise MAX_TESTS");

}  // namespace

size_t testCount() { return std::size(TESTS); }

const TestInfo& testAt(const size_t index) { return TESTS[index < std::size(TESTS) ? index : 0]; }

size_t testIndex(const TestInfo& info) { return static_cast<size_t>(&info - TESTS); }

std::unique_ptr<Activity> createTest(const TestInfo& info, GfxRenderer& renderer, MappedInputManager& mappedInput) {
  if (info.create) return info.create(info, renderer, mappedInput);
  return makeUniqueNoThrow<PlaceholderTest>(info, renderer, mappedInput);
}

}  // namespace testsuite
#endif

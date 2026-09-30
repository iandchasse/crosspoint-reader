#pragma once

// Screen factories of the implemented tests, for TestCatalog (CROSSPOINT_TEST_SUITE
// builds only). One source file per test in this folder.
#if CROSSPOINT_TEST_SUITE
#include <memory>

class Activity;
class GfxRenderer;
class MappedInputManager;

namespace testsuite {

struct TestInfo;

std::unique_ptr<Activity> createSystemInfoTest(const TestInfo& info, GfxRenderer& renderer,
                                               MappedInputManager& mappedInput);  // P1
std::unique_ptr<Activity> createI2cScanTest(const TestInfo& info, GfxRenderer& renderer,
                                            MappedInputManager& mappedInput);  // P2
std::unique_ptr<Activity> createBatteryTest(const TestInfo& info, GfxRenderer& renderer,
                                            MappedInputManager& mappedInput);  // T1
std::unique_ptr<Activity> createChargeTest(const TestInfo& info, GfxRenderer& renderer,
                                           MappedInputManager& mappedInput);  // T2
std::unique_ptr<Activity> createKeysTest(const TestInfo& info, GfxRenderer& renderer,
                                         MappedInputManager& mappedInput);  // T3
std::unique_ptr<Activity> createRefreshTest(const TestInfo& info, GfxRenderer& renderer,
                                            MappedInputManager& mappedInput);  // T4
std::unique_ptr<Activity> createGrayscaleTest(const TestInfo& info, GfxRenderer& renderer,
                                              MappedInputManager& mappedInput);  // T5
std::unique_ptr<Activity> createInversionTest(const TestInfo& info, GfxRenderer& renderer,
                                              MappedInputManager& mappedInput);  // T6
std::unique_ptr<Activity> createOrientationTest(const TestInfo& info, GfxRenderer& renderer,
                                                MappedInputManager& mappedInput);  // T7
std::unique_ptr<Activity> createSpiClockTest(const TestInfo& info, GfxRenderer& renderer,
                                             MappedInputManager& mappedInput);  // T8
std::unique_ptr<Activity> createFrontlightTest(const TestInfo& info, GfxRenderer& renderer,
                                               MappedInputManager& mappedInput);  // T9
std::unique_ptr<Activity> createLightTripTest(const TestInfo& info, GfxRenderer& renderer,
                                              MappedInputManager& mappedInput);  // T10
std::unique_ptr<Activity> createTouchMappingTest(const TestInfo& info, GfxRenderer& renderer,
                                                 MappedInputManager& mappedInput);  // T11
std::unique_ptr<Activity> createTouchSleepTest(const TestInfo& info, GfxRenderer& renderer,
                                               MappedInputManager& mappedInput);  // T12

}  // namespace testsuite
#endif

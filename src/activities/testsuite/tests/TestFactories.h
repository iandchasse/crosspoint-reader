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

}  // namespace testsuite
#endif

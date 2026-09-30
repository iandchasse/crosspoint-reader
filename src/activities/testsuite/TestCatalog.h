#pragma once

// The tests the menu offers, in menu order (CROSSPOINT_TEST_SUITE builds only).
#if CROSSPOINT_TEST_SUITE
#include <cstddef>
#include <memory>

class Activity;
class GfxRenderer;
class MappedInputManager;

namespace testsuite {

struct TestInfo;
// Builds the test's screen; nullptr on OOM (the menu logs it and stays).
using TestFactory = std::unique_ptr<Activity> (*)(const TestInfo& info, GfxRenderer& renderer,
                                                  MappedInputManager& mappedInput);

struct TestInfo {
  const char* id;       // stable id in results.csv and the serial log: "T1".."T15", "P1"..
  const char* title;    // menu label
  const char* summary;  // what it checks, one line
  TestFactory create;   // nullptr: not implemented yet (the menu opens a placeholder)
};

inline constexpr size_t MAX_TESTS = 32;

size_t testCount();
const TestInfo& testAt(size_t index);
// Index of an entry of this catalog.
size_t testIndex(const TestInfo& info);
// The entry with this id, or nullptr.
const TestInfo* findTest(const char* id);
// The test's screen, or the "not implemented yet" placeholder.
std::unique_ptr<Activity> createTest(const TestInfo& info, GfxRenderer& renderer, MappedInputManager& mappedInput);

}  // namespace testsuite
#endif

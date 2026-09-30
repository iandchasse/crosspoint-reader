#pragma once

// Result text for the tests' records (CROSSPOINT_TEST_SUITE builds only).
#if CROSSPOINT_TEST_SUITE
#include <cstddef>

namespace testsuite {

// Appends to a NUL-terminated buffer, truncating at its end.
void appendf(char* buffer, size_t size, const char* format, ...) __attribute__((format(printf, 3, 4)));
// Adds `what` to a "+"-separated list of failed checks.
void addFailure(char* buffer, size_t size, const char* what);

}  // namespace testsuite
#endif

#if CROSSPOINT_TEST_SUITE
#include "TextBuffer.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace testsuite {

void appendf(char* buffer, const size_t size, const char* format, ...) {
  const size_t used = strnlen(buffer, size);
  if (used + 1 >= size) return;
  va_list args;
  va_start(args, format);
  vsnprintf(buffer + used, size - used, format, args);
  va_end(args);
}

void addFailure(char* buffer, const size_t size, const char* what) {
  appendf(buffer, size, "%s%s", buffer[0] != '\0' ? "+" : "", what);
}

}  // namespace testsuite
#endif

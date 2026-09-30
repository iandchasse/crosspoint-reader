#pragma once

// Test results: one serial line and one row of /.silkscreen-test/results.csv per
// result (CROSSPOINT_TEST_SUITE builds only). The test firmware writes nothing
// else on the card except scratch files of its own, which end in ".tmp" and are
// removed at the next boot.
#if CROSSPOINT_TEST_SUITE
#include <cstddef>
#include <cstdint>

namespace testsuite {

struct TestInfo;

enum class Verdict : uint8_t { Pass, Fail, Skip, Info };
const char* verdictName(Verdict verdict);  // "PASS", "FAIL", "SKIP", "INFO"

// Where a result went. The serial line is always written.
enum class LogOutcome : uint8_t { Saved, SerialOnly, WriteFailed };

inline constexpr char RESULTS_DIR[] = "/.silkscreen-test";
inline constexpr char RESULTS_CSV[] = "/.silkscreen-test/results.csv";
inline constexpr char SCRATCH_SUFFIX[] = ".tmp";

// Logs "TEST,<id>,<verdict>,<details>" at INFO and appends
//   timestamp,time_source,session,board,firmware,test,verdict,details
// to results.csv (the header goes in first when the file is new). The timestamp
// is the RTC's local time when it holds a valid time (time_source "rtc"),
// otherwise seconds since boot ("uptime"). session is random per boot; board is
// the end of the Wi-Fi MAC. details is free text, "key=value" pairs by
// convention; it is quoted in the CSV. Loop task only.
LogOutcome recordResult(const TestInfo& test, Verdict verdict, const char* details);

// The last verdict recorded this boot for a catalog entry, for the menu.
bool lastVerdict(const TestInfo& test, Verdict& verdict);

// Removes scratch files ("*.tmp") an interrupted test left in RESULTS_DIR.
void removeStaleScratchFiles();

}  // namespace testsuite
#endif

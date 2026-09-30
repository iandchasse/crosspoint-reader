#pragma once

// P8: the reader's own files on the card, left untouched by the test firmware
// (CROSSPOINT_TEST_SUITE builds only). The first boot of the test firmware with
// a card keeps a baseline of /.crosspoint in /.silkscreen-test/crosspoint.sha:
// the SHA-256 and size of every file directly in /.crosspoint (settings, state,
// Wi-Fi, recent books, OPDS, KOReader, the Quick Resume frame) and the names of
// its folders (one per book cache). Every later boot and P8 compare the card
// with it. Read-only on /.crosspoint; loop task only.
#if CROSSPOINT_TEST_SUITE
#include <cstddef>
#include <cstdint>

namespace testsuite::readerdata {

inline constexpr char READER_DIR[] = "/.crosspoint";
inline constexpr char BASELINE_FILE[] = "/.silkscreen-test/crosspoint.sha";
inline constexpr int MAX_FILES = 32;
inline constexpr int NAME_LEN = 48;

enum class FileState : uint8_t { Same, Changed, Missing, Added };
const char* fileStateName(FileState state);  // "same", "changed", "missing", "added"

struct FileCheck {
  char name[NAME_LEN];
  uint32_t size;  // now (Missing: in the baseline)
  FileState state;
};

struct Report {
  bool baselineFound;    // a baseline was on the card
  bool baselineCreated;  // none was, and this check wrote one
  bool ioError;          // reading /.crosspoint or the baseline, or writing it, failed
  bool dirNow;           // /.crosspoint exists now
  bool dirBaseline;      // ... and did when the baseline was taken
  bool foldersSame;      // the folder names in /.crosspoint, in directory order
  bool truncated;        // more than MAX_FILES files: the rest are not compared
  uint16_t foldersNow;
  uint16_t foldersBaseline;
  int same;
  int changed;
  int missing;
  int added;
  int count;         // entries in checks
  char takenAt[32];  // when the baseline was taken, and the time source
  char takenBy[48];  // firmware that took the baseline
  FileCheck checks[MAX_FILES + MAX_FILES];

  bool clean() const {
    return !ioError && changed == 0 && missing == 0 && added == 0 && foldersSame && dirNow == dirBaseline;
  }
};

// Compares /.crosspoint with the baseline, writing the baseline first when the
// card has none. `report` is large (heap). False when the card is not mounted.
bool check(Report& report);
// Replaces the baseline with the card as it is now, then checks against it.
bool rebaseline(Report& report);
// One line of "key=value" pairs for results.csv.
void describe(const Report& report, char* out, size_t size);
// The check once per boot (the menu's first opening): logs and records a P8 result.
void checkAtBoot();

}  // namespace testsuite::readerdata
#endif

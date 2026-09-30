#if CROSSPOINT_TEST_SUITE
#include "ReaderData.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "Sha256.h"
#include "TextBuffer.h"
#include "activities/testsuite/TestCatalog.h"
#include "activities/testsuite/TestResultLog.h"

namespace testsuite::readerdata {
namespace {

constexpr char BASELINE_TMP[] = "/.silkscreen-test/crosspoint.sha.tmp";
constexpr char HEADER[] = "# silkscreen-test /.crosspoint baseline v1";
constexpr size_t CHUNK = 4096;
// The baseline text: a header and one line of about 130 bytes per file.
constexpr size_t TEXT_BYTES = 8192;
static_assert(TEXT_BYTES >= CHUNK, "the text buffer doubles as the read chunk");
static_assert(TEXT_BYTES > 256 + MAX_FILES * (NAME_LEN + 2 * Sha256::SIZE + 20), "baseline text buffer too small");

struct FileHash {
  char name[NAME_LEN];
  uint32_t size;
  uint8_t sha[Sha256::SIZE];
};

// /.crosspoint as scanned now, or as the baseline recorded it. Plain data: it
// is cleared with memset and lives on the heap (about 2.8 KB).
struct Snapshot {
  bool exists;
  bool truncated;
  uint16_t folders;
  uint8_t folderSha[Sha256::SIZE];  // folder names in directory order
  int count;
  FileHash files[MAX_FILES];
  char takenAt[32];
  char takenBy[48];
};

// One file's SHA-256. Out of line, so its hash context and the folder-name
// context of scanReaderDir() never share a stack frame (about 116 B each).
__attribute__((noinline)) bool hashFile(HalFile& entry, uint8_t* chunk, uint8_t (&out)[Sha256::SIZE]) {
  Sha256 sha;
  bool ok = true;
  for (;;) {
    const int got = entry.read(chunk, CHUNK);
    if (got < 0) {
      ok = false;
      break;
    }
    if (got == 0) break;
    sha.update(chunk, static_cast<size_t>(got));
  }
  sha.finish(out);
  return ok;
}

bool scanReaderDir(Snapshot& snap, uint8_t* chunk) {
  memset(&snap, 0, sizeof(snap));
  HalFile dir = Storage.open(READER_DIR);
  if (!dir || !dir.isDirectory()) return true;
  snap.exists = true;
  Sha256 folders;
  bool ok = true;
  char name[64];
  for (HalFile entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
    size_t len = entry.getName(name, sizeof(name));
    if (len == 0 || len >= sizeof(name)) len = static_cast<size_t>(snprintf(name, sizeof(name), "?"));
    if (entry.isDirectory()) {
      ++snap.folders;
      folders.update(name, len);
      folders.update("\n", 1);
      continue;
    }
    if (snap.count >= MAX_FILES) {
      snap.truncated = true;
      continue;
    }
    FileHash& file = snap.files[snap.count++];
    snprintf(file.name, sizeof(file.name), "%s", name);
    file.size = static_cast<uint32_t>(entry.fileSize());
    if (!hashFile(entry, chunk, file.sha)) ok = false;
  }
  folders.finish(snap.folderSha);
  return ok;
}

bool fromHex(const char* hex, uint8_t (&out)[Sha256::SIZE]) {
  for (size_t i = 0; i < Sha256::SIZE; ++i) {
    unsigned value = 0;
    if (sscanf(hex + 2 * i, "%2x", &value) != 1) return false;
    out[i] = static_cast<uint8_t>(value);
  }
  return true;
}

// One line of the baseline; unknown lines are skipped.
void parseLine(Snapshot& snap, char* line) {
  const size_t len = strlen(line);
  if (len > 0 && line[len - 1] == '\r') line[len - 1] = '\0';
  if (strncmp(line, "taken ", 6) == 0) {
    snprintf(snap.takenAt, sizeof(snap.takenAt), "%s", line + 6);
  } else if (strncmp(line, "firmware ", 9) == 0) {
    snprintf(snap.takenBy, sizeof(snap.takenBy), "%s", line + 9);
  } else if (strncmp(line, "dir ", 4) == 0) {
    snap.exists = atoi(line + 4) != 0;
  } else if (strncmp(line, "folders ", 8) == 0) {
    char* end = nullptr;
    snap.folders = static_cast<uint16_t>(strtoul(line + 8, &end, 10));
    if (end != nullptr && *end == ' ') fromHex(end + 1, snap.folderSha);
  } else if (strncmp(line, "file ", 5) == 0 && snap.count < MAX_FILES) {
    // file <sha256 hex> <size> <name>
    const char* hex = line + 5;
    if (strlen(hex) < 2 * Sha256::SIZE + 3 || hex[2 * Sha256::SIZE] != ' ') return;
    FileHash& file = snap.files[snap.count];
    if (!fromHex(hex, file.sha)) return;
    char* end = nullptr;
    file.size = static_cast<uint32_t>(strtoul(hex + 2 * Sha256::SIZE + 1, &end, 10));
    if (end == nullptr || *end != ' ') return;
    snprintf(file.name, sizeof(file.name), "%s", end + 1);
    ++snap.count;
  } else if (strcmp(line, "truncated") == 0) {
    snap.truncated = true;
  }
}

bool readBaseline(Snapshot& snap, char* text, bool& found) {
  memset(&snap, 0, sizeof(snap));
  found = false;
  if (!Storage.exists(BASELINE_FILE)) return true;
  HalFile file = Storage.open(BASELINE_FILE);
  if (!file) return false;
  found = true;
  const int got = file.read(text, TEXT_BYTES - 1);
  if (got < 0) return false;
  text[got] = '\0';
  if (strncmp(text, HEADER, sizeof(HEADER) - 1) != 0) return false;
  for (char* line = text; line != nullptr && *line != '\0';) {
    char* next = strchr(line, '\n');
    if (next != nullptr) *next++ = '\0';
    parseLine(snap, line);
    line = next;
  }
  return true;
}

bool writeBaseline(Snapshot& snap, char* text) {
  char stamp[24];
  const char* source = formatTimestamp(stamp, sizeof(stamp));
  snprintf(snap.takenAt, sizeof(snap.takenAt), "%s (%s)", stamp, source);
  snprintf(snap.takenBy, sizeof(snap.takenBy), "%s", CROSSPOINT_VERSION);
  char hex[2 * Sha256::SIZE + 1];
  Sha256::toHex(snap.folderSha, hex);
  text[0] = '\0';
  appendf(text, TEXT_BYTES, "%s\ntaken %s\nfirmware %s\ndir %d\nfolders %u %s\n", HEADER, snap.takenAt, snap.takenBy,
          snap.exists ? 1 : 0, snap.folders, hex);
  for (int i = 0; i < snap.count; ++i) {
    Sha256::toHex(snap.files[i].sha, hex);
    appendf(text, TEXT_BYTES, "file %s %lu %s\n", hex, static_cast<unsigned long>(snap.files[i].size),
            snap.files[i].name);
  }
  if (snap.truncated) appendf(text, TEXT_BYTES, "truncated\n");

  if (!Storage.exists(RESULTS_DIR) && !Storage.mkdir(RESULTS_DIR)) return false;
  // Written beside it and renamed over it: an interrupted write leaves a
  // ".tmp" that the next boot removes, never half a baseline.
  if (Storage.exists(BASELINE_TMP)) Storage.remove(BASELINE_TMP);
  HalFile file = Storage.open(BASELINE_TMP, O_WRONLY | O_CREAT | O_TRUNC);
  if (!file) return false;
  const size_t len = strlen(text);
  bool ok = file.write(text, len) == len;
  ok = file.close() && ok;
  if (!ok) {
    Storage.remove(BASELINE_TMP);
    return false;
  }
  if (Storage.exists(BASELINE_FILE) && !Storage.remove(BASELINE_FILE)) return false;
  return Storage.rename(BASELINE_TMP, BASELINE_FILE);
}

void compare(const Snapshot& base, const Snapshot& now, Report& report) {
  bool matched[MAX_FILES] = {};
  for (int i = 0; i < base.count; ++i) {
    FileCheck& check = report.checks[report.count++];
    snprintf(check.name, sizeof(check.name), "%s", base.files[i].name);
    check.size = base.files[i].size;
    check.state = FileState::Missing;
    for (int j = 0; j < now.count; ++j) {
      if (matched[j] || strcmp(base.files[i].name, now.files[j].name) != 0) continue;
      matched[j] = true;
      check.size = now.files[j].size;
      const bool same =
          base.files[i].size == now.files[j].size && memcmp(base.files[i].sha, now.files[j].sha, Sha256::SIZE) == 0;
      check.state = same ? FileState::Same : FileState::Changed;
      break;
    }
  }
  for (int j = 0; j < now.count; ++j) {
    if (matched[j]) continue;
    FileCheck& check = report.checks[report.count++];
    snprintf(check.name, sizeof(check.name), "%s", now.files[j].name);
    check.size = now.files[j].size;
    check.state = FileState::Added;
  }
  for (int i = 0; i < report.count; ++i) {
    switch (report.checks[i].state) {
      case FileState::Same:
        ++report.same;
        break;
      case FileState::Changed:
        ++report.changed;
        break;
      case FileState::Missing:
        ++report.missing;
        break;
      case FileState::Added:
        ++report.added;
        break;
    }
  }
  report.dirNow = now.exists;
  report.dirBaseline = base.exists;
  report.foldersNow = now.folders;
  report.foldersBaseline = base.folders;
  report.foldersSame = base.folders == now.folders && memcmp(base.folderSha, now.folderSha, Sha256::SIZE) == 0;
  report.truncated = base.truncated || now.truncated;
  snprintf(report.takenAt, sizeof(report.takenAt), "%s", base.takenAt);
  snprintf(report.takenBy, sizeof(report.takenBy), "%s", base.takenBy);
}

bool run(Report& report, const bool replaceBaseline) {
  memset(&report, 0, sizeof(report));
  if (!Storage.ready()) return false;
  auto now = makeUniqueNoThrow<Snapshot>();
  auto base = makeUniqueNoThrow<Snapshot>();
  auto text = makeUniqueNoThrow<char[]>(TEXT_BYTES);
  if (!now || !base || !text) {
    LOG_ERR("TEST", "OOM: reader-data check");
    report.ioError = true;
    return true;
  }
  if (!scanReaderDir(*now, reinterpret_cast<uint8_t*>(text.get()))) report.ioError = true;
  bool found = false;
  if (!replaceBaseline && !readBaseline(*base, text.get(), found)) report.ioError = true;
  if (replaceBaseline || !found) {
    if (writeBaseline(*now, text.get())) {
      report.baselineCreated = true;
    } else {
      LOG_ERR("TEST", "Could not write %s", BASELINE_FILE);
      report.ioError = true;
    }
    memcpy(base.get(), now.get(), sizeof(Snapshot));
  }
  const bool ioError = report.ioError;
  const bool created = report.baselineCreated;
  compare(*base, *now, report);
  report.ioError = ioError;
  report.baselineCreated = created;
  report.baselineFound = found;
  return true;
}

}  // namespace

const char* fileStateName(const FileState state) {
  switch (state) {
    case FileState::Same:
      return "same";
    case FileState::Changed:
      return "changed";
    case FileState::Missing:
      return "missing";
    case FileState::Added:
      return "added";
  }
  return "?";
}

bool check(Report& report) { return run(report, /*replaceBaseline=*/false); }

bool rebaseline(Report& report) { return run(report, /*replaceBaseline=*/true); }

void describe(const Report& report, char* out, const size_t size) {
  appendf(out, size,
          "baseline=%s taken=\"%s\" dir=%d files=%d same=%d changed=%d missing=%d added=%d folders=%u/%u%s "
          "truncated=%d io_error=%d",
          report.baselineCreated ? "created"
          : report.baselineFound ? "found"
                                 : "none",
          report.takenAt, report.dirNow ? 1 : 0, report.count, report.same, report.changed, report.missing,
          report.added, report.foldersNow, report.foldersBaseline, report.foldersSame ? "" : "(names_changed)",
          report.truncated ? 1 : 0, report.ioError ? 1 : 0);
  if (report.dirNow != report.dirBaseline) appendf(out, size, " dir_was=%d", report.dirBaseline ? 1 : 0);
  int listed = 0;
  for (int i = 0; i < report.count && listed < 4; ++i) {
    if (report.checks[i].state == FileState::Same) continue;
    appendf(out, size, "%s%s:%s", listed == 0 ? " diff=" : ",", report.checks[i].name,
            fileStateName(report.checks[i].state));
    ++listed;
  }
}

void checkAtBoot() {
  if (!Storage.ready()) return;
  auto report = makeUniqueNoThrow<Report>();
  if (!report) {
    LOG_ERR("TEST", "OOM: reader-data boot check");
    return;
  }
  if (!check(*report)) return;
  char details[240] = "when=boot ";
  describe(*report, details, sizeof(details));
  Verdict verdict = Verdict::Pass;
  if (report->ioError || (!report->baselineCreated && !report->clean())) {
    verdict = Verdict::Fail;
    LOG_ERR("TEST", "Reader data in %s differs from the baseline: %s", READER_DIR, details);
  } else if (report->baselineCreated) {
    verdict = Verdict::Info;
    LOG_INF("TEST", "Reader-data baseline written to %s", BASELINE_FILE);
  }
  if (const TestInfo* test = findTest("P8")) recordResult(*test, verdict, details);
}

}  // namespace testsuite::readerdata
#endif

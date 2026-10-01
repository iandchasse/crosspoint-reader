#if CROSSPOINT_TEST_SUITE
#include "TestResultLog.h"

#include <Arduino.h>
#include <HalClock.h>
#include <HalStorage.h>
#include <Logging.h>
#include <esp_mac.h>
#include <esp_random.h>

#include <cstdio>
#include <cstring>
#include <ctime>

#include "TestCatalog.h"

namespace testsuite {
namespace {

constexpr char CSV_HEADER[] = "timestamp,time_source,session,board,firmware,test,verdict,details\n";
constexpr size_t SCRATCH_SUFFIX_LEN = sizeof(SCRATCH_SUFFIX) - 1;
// A crash that repeats inside a test must not turn the boot cleanup into a long scan.
constexpr int MAX_SCRATCH_REMOVALS = 16;

struct SessionVerdict {
  bool recorded;
  Verdict verdict;
};
SessionVerdict sessionVerdicts[MAX_TESTS] = {};

// One CSV row: the prefix (about 90 B), up to TestScreen's 383 B of details and
// the quotes. Static rather than on the stack (it is larger than a stack frame
// should hold); results are recorded from the loop task only.
char rowBuffer[512];

// A log line holds 256 B with its "[ms] [INF] [TEST] " prefix (Logging.cpp
// MAX_ENTRY_LEN): longer details continue on "TEST+,<id>," lines.
constexpr int SERIAL_CHUNK = 200;

const char* sessionId() {
  static char id[9] = "";
  if (id[0] == '\0') snprintf(id, sizeof(id), "%08lx", static_cast<unsigned long>(esp_random()));
  return id;
}

const char* boardId() {
  static char id[7] = "";
  if (id[0] == '\0') {
    uint8_t mac[6] = {};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(id, sizeof(id), "%02X%02X%02X", mac[3], mac[4], mac[5]);
  }
  return id;
}

// Builds the CSV row for a result in rowBuffer; returns its length.
size_t formatRow(const TestInfo& test, const Verdict verdict, const char* details) {
  char timestamp[24];
  const char* source = formatTimestamp(timestamp, sizeof(timestamp));
  const int n = snprintf(rowBuffer, sizeof(rowBuffer), "%s,%s,%s,%s,%s,%s,%s,\"", timestamp, source, sessionId(),
                         boardId(), CROSSPOINT_VERSION, test.id, verdictName(verdict));
  // Always room for the closing quote, the newline and the terminator.
  size_t pos = n < 0 ? 0 : static_cast<size_t>(n);
  if (pos > sizeof(rowBuffer) - 3) pos = sizeof(rowBuffer) - 3;
  // Room for a doubled quote as well.
  for (const char* p = details; *p != '\0' && pos + 5 < sizeof(rowBuffer); ++p) {
    char c = *p;
    if (c == '\r' || c == '\n') c = ' ';
    if (c == '"') rowBuffer[pos++] = '"';
    rowBuffer[pos++] = c;
  }
  rowBuffer[pos++] = '"';
  rowBuffer[pos++] = '\n';
  rowBuffer[pos] = '\0';
  return pos;
}

// Finds one scratch file in RESULTS_DIR and writes its path. The directory and
// the entry are closed on return, so the caller may remove the file.
bool findScratchFile(char* path, const size_t size) {
  HalFile dir = Storage.open(RESULTS_DIR);
  if (!dir || !dir.isDirectory()) return false;
  char name[64];
  for (HalFile entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
    if (entry.isDirectory()) continue;
    const size_t len = entry.getName(name, sizeof(name));
    if (len > SCRATCH_SUFFIX_LEN && len < sizeof(name) - 1 &&
        strcmp(name + len - SCRATCH_SUFFIX_LEN, SCRATCH_SUFFIX) == 0) {
      snprintf(path, size, "%s/%s", RESULTS_DIR, name);
      return true;
    }
  }
  return false;
}

}  // namespace

const char* formatTimestamp(char* buf, const size_t size) {
  struct tm now{};
  if (halClock.isAvailable() && halClock.hasValidTime() && halClock.localTime(now) &&
      strftime(buf, size, "%Y-%m-%d %H:%M:%S", &now) > 0) {
    return "rtc";
  }
  const unsigned long ms = millis();
  snprintf(buf, size, "%lu.%03lu", ms / 1000UL, ms % 1000UL);
  return "uptime";
}

const char* verdictName(const Verdict verdict) {
  switch (verdict) {
    case Verdict::Pass:
      return "PASS";
    case Verdict::Fail:
      return "FAIL";
    case Verdict::Skip:
      return "SKIP";
    case Verdict::Info:
      return "INFO";
    case Verdict::Warn:
      return "WARN";
  }
  return "?";
}

LogOutcome recordResult(const TestInfo& test, const Verdict verdict, const char* details) {
  if (details == nullptr) details = "";
  const size_t index = testIndex(test);
  if (index < MAX_TESTS) sessionVerdicts[index] = {true, verdict};
  const size_t detailsLen = strlen(details);
  LOG_INF("TEST", "TEST,%s,%s,%.*s", test.id, verdictName(verdict), SERIAL_CHUNK, details);
  for (size_t off = SERIAL_CHUNK; off < detailsLen; off += SERIAL_CHUNK) {
    LOG_INF("TEST", "TEST+,%s,%.*s", test.id, SERIAL_CHUNK, details + off);
  }

  if (!Storage.ready()) return LogOutcome::SerialOnly;
  if (!Storage.exists(RESULTS_DIR) && !Storage.mkdir(RESULTS_DIR)) {
    LOG_ERR("TEST", "Could not create %s", RESULTS_DIR);
    return LogOutcome::WriteFailed;
  }
  HalFile file = Storage.open(RESULTS_CSV, O_WRONLY | O_CREAT | O_APPEND);
  if (!file) {
    LOG_ERR("TEST", "Could not open %s", RESULTS_CSV);
    return LogOutcome::WriteFailed;
  }
  bool ok = true;
  if (file.fileSize() == 0) ok = file.write(CSV_HEADER, sizeof(CSV_HEADER) - 1) == sizeof(CSV_HEADER) - 1;
  const size_t len = formatRow(test, verdict, details);
  ok = file.write(rowBuffer, len) == len && ok;
  ok = file.close() && ok;
  if (!ok) {
    LOG_ERR("TEST", "Short write to %s", RESULTS_CSV);
    return LogOutcome::WriteFailed;
  }
  return LogOutcome::Saved;
}

bool lastVerdict(const TestInfo& test, Verdict& verdict) {
  const size_t index = testIndex(test);
  if (index >= MAX_TESTS || !sessionVerdicts[index].recorded) return false;
  verdict = sessionVerdicts[index].verdict;
  return true;
}

void removeStaleScratchFiles() {
  if (!Storage.ready() || !Storage.exists(RESULTS_DIR)) return;
  char path[96];
  for (int removed = 0; removed < MAX_SCRATCH_REMOVALS && findScratchFile(path, sizeof(path)); ++removed) {
    if (!Storage.remove(path)) {
      LOG_ERR("TEST", "Could not remove the stale scratch file %s", path);
      return;
    }
    LOG_INF("TEST", "Removed the stale scratch file %s", path);
  }
}

}  // namespace testsuite
#endif

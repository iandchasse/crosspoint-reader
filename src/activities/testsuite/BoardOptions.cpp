#if CROSSPOINT_TEST_SUITE
#include "BoardOptions.h"

#include <HalStorage.h>
#include <Logging.h>

#include <cstdio>
#include <cstring>

#include "TestResultLog.h"

namespace testsuite::options {
namespace {

// Written beside the file and renamed over it, never half a file. SdFat's rename
// does not overwrite, so the old file is removed first: a reset between the
// remove and the rename leaves only a complete ".tmp", which load() takes up
// before the boot's scratch sweep. A ".tmp" from an interrupted write is
// incomplete, and the sweep removes it.
constexpr char OPTIONS_TMP[] = "/.silkscreen-test/options.tmp";
constexpr char KEY_CUT_DOWN[] = "cut_down=";
constexpr char KEY_UP2_POWER[] = "up2_power=";

bool cutDownOn = false;
bool up2PowerOn = false;
bool fromCard = false;

// The line starting "key=" anywhere in the text, or nullptr.
const char* findKey(const char* text, const char* key) {
  const size_t keyLen = strlen(key);
  for (const char* line = text; line != nullptr && *line != '\0';) {
    if (strncmp(line, key, keyLen) == 0) return line;
    line = strchr(line, '\n');
    if (line != nullptr) ++line;
  }
  return nullptr;
}

// "key=1" anywhere in the text, at the start of a line.
bool readFlag(const char* text, const char* key) {
  const char* line = findKey(text, key);
  return line != nullptr && line[strlen(key)] == '1';
}

// options.tmp left whole by a save cut short between its remove and its rename:
// both lines and the final newline. Renamed into place; false otherwise.
bool recoverTmp() {
  if (Storage.exists(OPTIONS_FILE) || !Storage.exists(OPTIONS_TMP)) return false;
  char text[64] = "";
  const size_t len = Storage.readFileToBuffer(OPTIONS_TMP, text, sizeof(text));
  if (len == 0 || text[len - 1] != '\n' || findKey(text, KEY_CUT_DOWN) == nullptr ||
      findKey(text, KEY_UP2_POWER) == nullptr) {
    return false;
  }
  if (!Storage.rename(OPTIONS_TMP, OPTIONS_FILE)) {
    LOG_ERR("TEST", "Board options: could not rename %s", OPTIONS_TMP);
    return false;
  }
  LOG_INF("TEST", "Board options: recovered %s from an interrupted save", OPTIONS_FILE);
  return true;
}

bool save() {
  if (!Storage.ready()) {
    LOG_INF("TEST", "Board options: no card, kept until the next restart");
    return false;
  }
  char text[64];
  snprintf(text, sizeof(text), "%s%d\n%s%d\n", KEY_CUT_DOWN, cutDownOn ? 1 : 0, KEY_UP2_POWER, up2PowerOn ? 1 : 0);
  if (!Storage.exists(RESULTS_DIR) && !Storage.mkdir(RESULTS_DIR)) {
    LOG_ERR("TEST", "Board options: could not create %s", RESULTS_DIR);
    return false;
  }
  if (Storage.exists(OPTIONS_TMP)) Storage.remove(OPTIONS_TMP);
  bool ok = false;
  {
    HalFile file = Storage.open(OPTIONS_TMP, O_WRONLY | O_CREAT | O_TRUNC);
    if (file) {
      const size_t len = strlen(text);
      ok = file.write(text, len) == len;
      ok = file.close() && ok;
    }
  }
  if (ok && Storage.exists(OPTIONS_FILE)) ok = Storage.remove(OPTIONS_FILE);
  if (ok) ok = Storage.rename(OPTIONS_TMP, OPTIONS_FILE);
  if (!ok) {
    LOG_ERR("TEST", "Board options: could not write %s", OPTIONS_FILE);
    return false;
  }
  fromCard = true;
  LOG_INF("TEST", "Board options saved: cut_down=%d up2_power=%d", cutDownOn ? 1 : 0, up2PowerOn ? 1 : 0);
  return true;
}

}  // namespace

bool cutDown() { return cutDownOn; }
bool up2IsPower() { return up2PowerOn; }
bool onCard() { return fromCard; }

bool setCutDown(const bool on) {
  cutDownOn = on;
  if (on) up2PowerOn = true;
  return save();
}

bool setUp2IsPower(const bool on) {
  up2PowerOn = on;
  return save();
}

void load() {
  cutDownOn = false;
  up2PowerOn = false;
  fromCard = false;
  if (!Storage.ready()) return;
  recoverTmp();
  if (!Storage.exists(OPTIONS_FILE)) return;
  char text[64] = "";
  if (Storage.readFileToBuffer(OPTIONS_FILE, text, sizeof(text)) == 0) {
    LOG_ERR("TEST", "Board options: could not read %s", OPTIONS_FILE);
    return;
  }
  cutDownOn = readFlag(text, KEY_CUT_DOWN);
  up2PowerOn = readFlag(text, KEY_UP2_POWER);
  fromCard = true;
  LOG_INF("TEST", "Board options: cut_down=%d up2_power=%d", cutDownOn ? 1 : 0, up2PowerOn ? 1 : 0);
}

void formatToken(char* buf, const size_t size) {
  snprintf(buf, size, "cut=%d up2pwr=%d", cutDownOn ? 1 : 0, up2PowerOn ? 1 : 0);
}

}  // namespace testsuite::options
#endif

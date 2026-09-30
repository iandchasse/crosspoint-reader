#if CROSSPOINT_TEST_SUITE
// ActivityManager's reader entry points in the hardware test firmware.
// ActivityManager.cpp compiles its own definitions only without
// CROSSPOINT_TEST_SUITE; here each one lands on the test menu, so nothing
// reaches the reader, the library, the file browser, OPDS, Settings or the web
// server (their sources are left out of the build or dropped by the linker).
#include <Logging.h>
#include <Memory.h>

#include <utility>

#include "SleepRecord.h"
#include "TestSleepActivity.h"
#include "TestSuiteActivity.h"
#include "activities/ActivityManager.h"

void ActivityManager::goHome(HomeMenuItem /*initialMenuItem*/, bool /*cleanInitialRefresh*/) {
  auto menu = makeUniqueNoThrow<TestSuiteActivity>(renderer, mappedInput);
  if (!menu) {
    LOG_ERR("TEST", "OOM: test menu");
    return;
  }
  replaceActivity(std::move(menu));
}

void ActivityManager::goToReader(std::string path, bool /*allowFastInitialRefresh*/) {
  LOG_ERR("TEST", "No reader in the test firmware (%s)", path.c_str());
  goHome();
}

void ActivityManager::goToSettings() { goHome(); }
void ActivityManager::goToFileBrowser(std::string /*path*/) { goHome(); }
void ActivityManager::goToLibrary() { goHome(); }
void ActivityManager::goToBrowser() { goHome(); }
void ActivityManager::goToFileTransfer() { goHome(); }

void ActivityManager::goToSleep(const bool fromTimeout) {
  // Before the current screen closes: P5's armed path is still set.
  testsuite::beginSleepEntry(fromTimeout);
  auto screen = makeUniqueNoThrow<TestSleepActivity>(renderer, mappedInput, fromTimeout);
  if (!screen) {
    LOG_ERR("TEST", "OOM: sleep screen");
    return;
  }
  replaceActivity(std::move(screen));
  loop();  // the sleep screen is drawn before enterDeepSleep() continues
}
#endif

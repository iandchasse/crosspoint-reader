#pragma once

// Hooks the shared reader code calls in the hardware test firmware
// (CROSSPOINT_TEST_SUITE builds only; docs/silkscreen/TEST-SUITE.md).
#if CROSSPOINT_TEST_SUITE
namespace testsuite {

// setup(), right after SETTINGS.loadFromFile(). Pins the values the tests rely on
// in RAM only (the test firmware never saves: PersistableStore writes are no-ops),
// removes scratch files an interrupted test left in /.silkscreen-test/, and logs
// the firmware and board tag.
void beginAfterSettings();

// True while a USB data host is attached (USB Serial/JTAG sees its frames, with
// a 2 s hold so a brief gap does not end it) and the USB_STAT ladder reports USB
// power. ActivityManager::preventAutoSleep() keeps a bench session awake with it;
// a charger alone, a cell, or a VBUS-less data link on the cell never counts.
// Loop task only.
bool usbHostAttached();

}  // namespace testsuite
#endif

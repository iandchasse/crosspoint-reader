#if SILKSCREEN_BATLOG

#include "BatteryLogWriter.h"

#include <Arduino.h>
#include <BatteryLog.h>
#include <BatteryMonitor.h>
#include <HalGPIO.h>
#include <HalStorage.h>
#include <Logging.h>
#include <WiFi.h>

namespace bl = freeink::batterylog;

namespace {

constexpr char DIR_PATH[] = "/silkscreen-battery";
constexpr char FILE_PATH[] = "/silkscreen-battery/battery.csv";
// The charge ladder decodes 1.5 s after boot at the earliest, so an earlier flush would record an unknown USB state.
constexpr unsigned long FIRST_FLUSH_MS = 2500;

// The "# fw" line says which env wrote the rows that follow it: the USB-serial wait and the serial log shift the
// wake sample's timing.
#if defined(ENABLE_SERIAL_LOG) && defined(CROSSPOINT_WAIT_FOR_USB_SERIAL)
constexpr uint16_t BOOT_WAIT_MS = 250;
#else
constexpr uint16_t BOOT_WAIT_MS = 0;
#endif
#ifdef ENABLE_SERIAL_LOG
constexpr bool SERIAL_LOG = true;
#else
constexpr bool SERIAL_LOG = false;
#endif

// The header, the "# fw" line and the row are formatted into this one buffer, one after the other.
char lineBuf[bl::ROW_BUFFER_BYTES];
bool dirChecked = false;  // the folder exists, as far as this boot knows

char chargeChar(const BoardConfig::ChargeState state) {
  switch (state) {
    case BoardConfig::ChargeState::OnBattery:
      return 'b';
    case BoardConfig::ChargeState::Charging:
      return 'c';
    case BoardConfig::ChargeState::Complete:
      return 'f';
    case BoardConfig::ChargeState::ExternalIdle:
      return 'x';
    case BoardConfig::ChargeState::NoBattery:
      return 'n';
    case BoardConfig::ChargeState::Unknown:
      break;
  }
  return '?';
}

bool putLine(HalFile& file, const size_t len, size_t& written) {
  if (len == 0 || file.write(lineBuf, len) != len) return false;
  written += len;
  return true;
}

// One open, the lines, one close. Every line starts with its own LF, so a torn one never glues to the next. False when
// anything failed.
bool append(const bool orphan, const bool earlierBad) {
  if (!dirChecked) {
    if (!Storage.ensureDirectoryExists(DIR_PATH)) {
      LOG_ERR("BATLOG", "Could not create %s", DIR_PATH);
      return false;
    }
    dirChecked = true;
  }
  HalFile file = Storage.open(FILE_PATH, O_WRONLY | O_CREAT | O_APPEND);
  if (!file) {
    LOG_ERR("BATLOG", "Could not open %s", FILE_PATH);
    return false;
  }
  const size_t size = file.fileSize();
  if (size >= bl::FILE_CAP_BYTES) {
    file.close();
    LOG_ERR("BATLOG", "%s is at its cap; move or delete it to log again", FILE_PATH);
    return false;
  }

  size_t written = 0;
  bool ok = true;
  if (size == 0) ok = putLine(file, bl::formatHeader(lineBuf, sizeof(lineBuf)), written);
  // After a new file, an orphan row (a new image or a lost record) or an earlier loss, which may have cost the line.
  if (ok && (size == 0 || orphan || earlierBad)) {
    ok = putLine(file, bl::formatFwLine(lineBuf, sizeof(lineBuf), CROSSPOINT_VERSION, BOOT_WAIT_MS, SERIAL_LOG),
                 written);
  }
  if (ok) {
    bl::FlushCtx ctx;
    ctx.usb = BatteryLogWriter::usbPresent();
    ctx.chg = BatteryLogWriter::chargeState();
    ctx.chargeUndecoded = ctx.chg == '?';
    size_t len = bl::formatRow(lineBuf, sizeof(lineBuf), ctx);
    if (len != 0 && size + written + len >= bl::FILE_CAP_BYTES) {
      ctx.fileCapReached = true;  // this row is the last the file takes
      len = bl::formatRow(lineBuf, sizeof(lineBuf), ctx);
    }
    ok = putLine(file, len, written);
  }
  return file.close() && ok;  // close() syncs
}

// The one SD attempt of a wake, timed. The result goes to the SDK, which advances the row and keeps the back-off.
void flush() {
  if (!Storage.ready()) return;
  const bl::Pending pending = bl::pending();
  // A bad attempt, or a row lost since the last one that reached the card (an orphan row that never reached it
  // included): the "# fw" line may be missing from the file.
  const bool earlierBad = bl::sdBadCount() != 0 || bl::status().drop != 0;
  const unsigned long startMs = millis();
  const bool ok = append(pending.orphan, earlierBad);
  const uint32_t ms = millis() - startMs;
  bl::flushed(ok, ms);
  LOG_INF("BATLOG", "Row %s in %lu ms", ok ? "written" : "not written", static_cast<unsigned long>(ms));
}

}  // namespace

namespace BatteryLogWriter {

void flushIfDue() {
  if (!bl::pending().ready || millis() < FIRST_FLUSH_MS || bl::sdBackoff()) return;
  flush();
}

void flushAtEntry() {
  if (!bl::pending().ready || WiFi.getMode() != WIFI_MODE_NULL || bl::sdBadCount() != 0 || bl::sdBackoff()) return;
  flush();
}

uint8_t usbPresent() {
  if (BatteryMonitor::chargeStatus() == BoardConfig::ChargeState::Unknown) return bl::core::ABSENT8;
  return gpio.isUsbConnected() ? 1 : 0;
}

char chargeState() { return chargeChar(BatteryMonitor::chargeStatus()); }

}  // namespace BatteryLogWriter

#endif  // SILKSCREEN_BATLOG

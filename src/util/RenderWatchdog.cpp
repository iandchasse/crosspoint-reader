#include "RenderWatchdog.h"

#include <BoardConfig.h>

#if FREEINK_CAP_POWER_POLICY

#include <Arduino.h>
#include <ChargeLadderDecoder.h>
#include <HalGPIO.h>
#include <SharedAdc.h>
#include <driver/gpio.h>
#include <esp_private/system_internal.h>
#include <esp_rom_sys.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstring>

// What may run once the watchdog has decided (docs/silkscreen/POWER.md, AW-04). The task that is stuck can hold any
// lock the rest of the firmware uses: the SPI bus and its transaction lock (the 3.68" panel's first freeze read as
// that, from the code), the I2C bus, the ADC, the storage and log mutexes, the front-light gate. Nothing below the
// decision may wait on one: no project logger, no Serial, no Arduino pin call (the pin-bus manager takes the SPI lock
// for an SPI pin), no manager of the SDK, and no esp_restart(), whose shutdown handlers stop Wi-Fi and darken the light
// through the manager. What it uses: the ROM's printf, the IDF's GPIO register calls (spinlocks), a bounded ADC wait to
// read the USB pin, and esp_restart_noos(), which needs no scheduler. scripts/silkscreen/test_render_watchdog.py reads
// this file for anything else.
namespace renderwatchdog {
namespace {

constexpr uint32_t TASK_STACK_BYTES = 4096;  // internal RAM (xTaskCreatePinnedToCore); the task needs far less
constexpr UBaseType_t TASK_PRIORITY = 1;     // the render task's and the loop task's; it runs for microseconds
constexpr BaseType_t TASK_CORE = 0;          // not the core of the render task and the loop task
// As the battery log's burst: a miss is skipped, not waited out.
constexpr TickType_t ADC_WAIT_TICKS = pdMS_TO_TICKS(20);

Stamp g_stamp;
char g_activity[sizeof(Event::activity)];  // written before the stamp, read after it
std::atomic<uint32_t> g_loopPassMs{0};
TaskHandle_t g_task = nullptr;
#if CROSSPOINT_TEST_SUITE
std::atomic<uint32_t> g_boundOverrideMs{0};  // P9: a shorter bound, in RAM
Event g_reported;                            // takeReport()'s record, for P9
bool g_haveReported = false;
// The task's stack headroom at the restart, kept beside the record (RTC_NOINIT: it survives the restart and the sleep),
// for the report of the next boot. The word and its complement say that the value was written.
struct StackNote {
  uint32_t freeBytes;
  uint32_t inverse;
};
RTC_NOINIT_ATTR StackNote g_stackNote;
uint32_t g_reportedStackFree = 0;
bool g_haveReportedStackFree = false;
#endif

// The bound in force: BOUND_MS, or, in the hardware test firmware, what P9 set.
uint32_t currentBoundMs() {
#if CROSSPOINT_TEST_SUITE
  const uint32_t shorter = g_boundOverrideMs.load(std::memory_order_relaxed);
  if (shorter != 0) return shorter;
#endif
  return BOUND_MS;
}

// The record the restart leaves in RTC memory (RTC_NOINIT: it survives a software restart and deep sleep, and is
// garbage after a power loss, which the magic word and the check reject).
struct Record {
  uint32_t magic;
  uint32_t phase;
  uint32_t stuckMs;
  uint32_t uptimeMs;
  char activity[sizeof(Event::activity)];
  uint32_t check;
};
constexpr uint32_t RECORD_MAGIC = 0x52574454;  // "RWDT"
constexpr uint32_t PHASE_RESTARTING = 1;       // the restart is under way: the boot after it sleeps at once
constexpr uint32_t PHASE_SLEPT = 2;            // that boot slept: the next one that stays up reports it
RTC_NOINIT_ATTR Record g_record;

uint32_t checksum(const Record& r) {
  uint32_t sum = r.magic ^ (r.phase * 2654435761u) ^ (r.stuckMs * 40503u) ^ (r.uptimeMs * 69069u);
  for (const char c : r.activity) sum = sum * 31u + static_cast<uint8_t>(c);
  return sum;
}

void store(const uint32_t phase, const Event& e) {
  Record r{};
  r.magic = RECORD_MAGIC;
  r.phase = phase;
  r.stuckMs = e.stuckMs;
  r.uptimeMs = e.uptimeMs;
  std::memcpy(r.activity, e.activity, sizeof(r.activity));
  r.check = checksum(r);
  g_record = r;
}

// Reads the record when it is in `phase`; `next` is the phase it moves to (0 = forgotten).
bool take(const uint32_t phase, const uint32_t next, Event& out) {
  const Record r = g_record;
  if (r.magic != RECORD_MAGIC || r.phase != phase || r.check != checksum(r)) return false;
  out.stuckMs = r.stuckMs;
  out.uptimeMs = r.uptimeMs;
  std::memcpy(out.activity, r.activity, sizeof(out.activity));
  out.activity[sizeof(out.activity) - 1] = '\0';
  if (next == 0) {
    g_record.magic = 0;
  } else {
    store(next, out);
  }
  return true;
}

// A fresh reading of USB_STAT (IO9), for the question HalGPIO::isUsbConnected() cannot answer while the loop task is
// stuck. The window test is the charge ladder's own (ChargeLadderDecoder::classify()), without its debounce: one
// reading, 16 samples averaged, and external power only when it lands in the idle, charging or complete window. A
// reading in the on-battery window, in a gap, or none at all (the ADC lock is held for longer than the wait) is the
// cell: the watchdog errs towards sleeping.
bool usbPresentNow() {
  if (!BoardConfig::hasChargeStatusLadder()) return false;
  const BoardConfig::ChargeStatusLadder& ladder = BoardConfig::ACTIVE.chargeLadder;
  const freeink::adc::Lock lock(ADC_WAIT_TICKS);
  if (!lock.held()) return false;
  const uint8_t samples = ladder.oversample == 0 ? 1 : ladder.oversample;
  uint32_t sum = 0;
  for (uint8_t i = 0; i < samples; ++i) sum += analogReadMilliVolts(ladder.pin);
  using Decoder = freeink::battery_detail::ChargeLadderDecoder<BoardConfig::ChargeLadderWindow>;
  const Decoder::Params params = {ladder.windows,         ladder.count,
                                  ladder.debounceSamples, ladder.completeStableSamples,
                                  ladder.blinkDeltaMv,    ladder.unknownHoldSamples};
  switch (Decoder::classify(params, static_cast<int32_t>(sum / samples))) {
    case BoardConfig::ChargeState::ExternalIdle:
    case BoardConfig::ChargeState::Charging:
    case BoardConfig::ChargeState::Complete:
      return true;
    default:
      return false;
  }
}

// The same predicate the awake ceiling uses (!gpio.isUsbConnected(), which reads the ladder state the loop task
// polls and is lock-free) while the loop task is passing; a fresh reading when it is not.
bool onBatteryNow(const uint32_t nowMs) {
  const uint32_t loopAgeMs = nowMs - g_loopPassMs.load(std::memory_order_relaxed);
  if (!loopIsStale(loopAgeMs, LOOP_STALE_MS)) return !gpio.isUsbConnected();
  return !usbPresentNow();
}

// The front light dark, as FrontlightManager::prepareForSleep() leaves it, but through the GPIO registers: the manager
// takes its gate lock first, and the stuck task may be holding it. ADIM (the driver's enable) goes LOW and leaves
// its LEDC carrier, the driver is given its dark dwell, and only then does the colour select rest at cool; the pads
// keep these levels through the restart, and holdPowerRails() parks them LOW at the boot after it. A light that
// tripped on an open string within the last few seconds has C9 still charged; the manager would wait up to 4 s for
// the rail to fall before it moved the colour select, and this does not (POWER.md AW-04 lists that as the one
// state it does not cover).
void darkenLight() {
#if FREEINK_CAP_FRONTLIGHT
  const BoardConfig::FrontlightConfig& fl = BoardConfig::ACTIVE.frontlight;
  if (fl.gpio == BoardConfig::PIN_UNASSIGNED) return;
  const auto driver = static_cast<gpio_num_t>(fl.gpio);
  gpio_set_level(driver, fl.activeHigh ? 0 : 1);
  gpio_set_direction(driver, GPIO_MODE_OUTPUT);
  if (fl.colorSelectGpio == BoardConfig::PIN_UNASSIGNED) return;
  esp_rom_delay_us(static_cast<uint32_t>(fl.offDwellMs) * 1000U + 500U);
  const auto select = static_cast<gpio_num_t>(fl.colorSelectGpio);
  gpio_set_level(select, fl.colorSelectWarmHigh ? 0 : 1);
  gpio_set_direction(select, GPIO_MODE_OUTPUT);
#endif
}

[[noreturn]] void restartIntoSleep(const Event& event) {
#if CROSSPOINT_TEST_SUITE
  // The deepest the task's stack has been, the decision and its ADC reading included: P9 reports it, so that the
  // 4096 bytes are a measurement and not a reading of the call graph.
  const uint32_t freeBytes = uxTaskGetStackHighWaterMark(nullptr);
  g_stackNote = {freeBytes, ~freeBytes};
#endif
  store(PHASE_RESTARTING, event);
  esp_rom_printf("[RWD] render() has run for %lu ms in %s on battery (bound %lu ms): restarting into deep sleep\n",
                 static_cast<unsigned long>(event.stuckMs), event.activity,
                 static_cast<unsigned long>(currentBoundMs()));
  darkenLight();
  vTaskSuspendAll();  // as esp_restart() does just before it; interrupts are off from the first line of the call
  esp_restart_noos();
}

void taskMain(void*) {
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(TICK_MS));
    const uint32_t stamp = g_stamp.get();  // the stamp first, then the clock: the stamp is never later than `now`
    if (stamp == 0) continue;
    const uint32_t now = millis();
    const uint32_t bound = currentBoundMs();
    if (!renderStuck(now, stamp, bound, true)) continue;               // not yet: the clock alone decides
    if (!renderStuck(now, stamp, bound, onBatteryNow(now))) continue;  // then the question that reads a pin
    Event event;
    event.stuckMs = now - stamp;
    event.uptimeMs = now;
    std::memcpy(event.activity, g_activity, sizeof(event.activity));
    event.activity[sizeof(event.activity) - 1] = '\0';
    if (g_stamp.get() != stamp) continue;  // the render ended, or another began, while this looked
    restartIntoSleep(event);
  }
}

}  // namespace

void start() {
  if (g_task != nullptr) return;
  xTaskCreatePinnedToCore(&taskMain, "RenderWatchdog", TASK_STACK_BYTES, nullptr, TASK_PRIORITY, &g_task, TASK_CORE);
  if (g_task == nullptr) esp_rom_printf("[RWD] could not start the render watchdog task\n");
}

void beginRender(const char* activityName) {
  std::strncpy(g_activity, activityName != nullptr ? activityName : "", sizeof(g_activity) - 1);
  g_stamp.begin(millis());
}

void endRender() { g_stamp.end(); }

void noteLoopPass() { g_loopPassMs.store(millis(), std::memory_order_relaxed); }

bool takeSleepRequest(Event& out) { return take(PHASE_RESTARTING, PHASE_SLEPT, out); }

bool takeReport(Event& out) {
  if (!take(PHASE_SLEPT, 0, out)) return false;
#if CROSSPOINT_TEST_SUITE
  g_reported = out;
  g_haveReported = true;
  g_haveReportedStackFree = g_stackNote.freeBytes == ~g_stackNote.inverse;
  g_reportedStackFree = g_haveReportedStackFree ? g_stackNote.freeBytes : 0;
#endif
  return true;
}

#if CROSSPOINT_TEST_SUITE
void setBoundForTest(const uint32_t boundMs) { g_boundOverrideMs.store(boundMs, std::memory_order_relaxed); }

bool running() { return g_task != nullptr; }

bool reportedThisBoot(Event& out) {
  if (!g_haveReported) return false;
  out = g_reported;
  return true;
}

bool reportedStackFree(uint32_t& freeBytes) {
  if (!g_haveReported || !g_haveReportedStackFree) return false;
  freeBytes = g_reportedStackFree;
  return true;
}
#endif

}  // namespace renderwatchdog

#endif  // FREEINK_CAP_POWER_POLICY

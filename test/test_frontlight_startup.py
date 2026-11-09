"""Run the production PWM routing and both firmware HALs over recording host adapters."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SDK = ROOT / "freeink-sdk/libs/hardware/FrontlightManager"


def function(source, marker):
    start = source.index(marker)
    opening = source.index("{", start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def run(source, directory, name, includes=()):
    cpp = directory / (name + ".cpp")
    exe = directory / (name + ".exe")
    cpp.write_text(source, encoding="utf-8")
    command = ["c++", "-std=c++17", "-Wall", "-Wextra", str(cpp), "-o", str(exe)]
    command += ["-I" + str(path) for path in includes]
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode:
        print(result.stdout + result.stderr, flush=True)
        result.check_returncode()
    subprocess.run([str(exe)], check=True)
    print(name + ": PASS")


source = (SDK / "src/FrontlightManager.cpp").read_text(encoding="utf-8")
with tempfile.TemporaryDirectory(prefix="frontlight-host-") as scratch:
    temp = Path(scratch)
    run((SDK / "test/host/test_brightness_startup.cpp").read_text(), temp, "pure-policy", [SDK / "include"])
    run((SDK / "test/host/test_load_policy.cpp").read_text(), temp, "existing-load-policy", [SDK / "include"])
    harness = r'''
#include <FrontlightBrightness.h>
#include <FrontlightStartup.h>
#include <atomic>
#include <cassert>
#include <cstdint>
#define FREEINK_CAP_FRONTLIGHT 1
#define FREEINK_CAP_FRONTLIGHT_PROBE 1
#define FREEINK_DEVICE_EEGO_A4 0
#define FREEINK_DEVICE_PICCO 0
#define LOG_ERR(...) ((void)0)
#define LOG_INF(...) ((void)0)
namespace BoardConfig {
constexpr int PIN_UNASSIGNED = -1;
struct Config { int gpio=42, gpioWarm=-1, pwmResolutionBits=10, maximumLegacyPercent=50, monitorAdc=2;
 bool viaPm1Pwm=false, activeHigh=true; uint16_t monitorScaleX1000=9333, monitorTripMv=23000;
 uint16_t minStartPermille=0,minHoldPermille=0;
 uint16_t startupIdleMinMv=1000, startupIdleMaxMv=6000, startupRiseMinMv=6000; };
struct { Config frontlight; } ACTIVE;
}
bool gated=true;
uint32_t written[2]={};
void delay(unsigned long) {}
uint32_t maxDuty(uint8_t bits) { return (1u<<bits)-1; }
bool gatedPath(const BoardConfig::Config&) { return gated; }
void pm1FrontlightWrite(uint32_t) {}
uint32_t physicalDuty(uint32_t duty,uint32_t,bool) { return duty; }
void writeChannel(int,int channel,uint32_t duty) { written[channel]=duty; }
void padToGpio(int,int) { written[0]=0; }
int darkLevel(const BoardConfig::Config&){return 0;}
constexpr int LEDC_CH_COOL=0, LEDC_CH_WARM=1, MONITOR_ADC_WAIT=0;
struct GateGuard { explicit GateGuard(void*) {} };
bool adcAvailable=true;
namespace freeink::adc { struct Lock { explicit Lock(int){} bool held() const { return adcAvailable; } }; }
uint32_t adcMv=321; int64_t nowUs=0;
uint32_t analogReadMilliVolts(int) { return adcMv; }
int64_t esp_timer_get_time() { return nowUs; }
class FrontlightManager {
public:
 bool _begun=true, _useLevel=false, _fault=false, _startupChecked=false, _startupRetryUsed=false, _gatedLit=false;
 int64_t _darkSinceUs=0;
 uint32_t _lastTotalDuty=0;
 uint8_t _brightness=0,_brightnessLevel=0,_lastBrightness=0,_warmPercent=50;
 void* _gateLock=nullptr;
 std::atomic<freeink::frontlight::StartupFault> _startupFault{freeink::frontlight::StartupFault::None};
 struct { uint32_t samples=0,peakLedMv=0; int64_t startUs=0; uint16_t windowMs=500; } _probe;
 int passed=0,tripped=0,dark=0;
 static uint8_t normalizeBrightness(uint8_t p) { return freeink::frontlight::normalizeBrightness(p); }
 bool startupBlocked() const { return _startupFault.load()!=freeink::frontlight::StartupFault::None; }
 void apply(); void setBrightness(uint8_t); void setBrightnessLevel(uint8_t); void clearFault();
 void probeTick(); bool checkStartupIdle(); void failStartup(freeink::frontlight::StartupFault);
 void applyGated(uint32_t duty,uint32_t) { written[0]=duty; }
 void probePassed() { ++passed; } void probeTripped(int64_t) { ++tripped; }
 void abandonProbe() {} void goDark(uint32_t) { ++dark; written[0]=0; }
};
'''
    for marker in ["void FrontlightManager::apply()", "void FrontlightManager::setBrightness(uint8_t percent)",
                   "void FrontlightManager::setBrightnessLevel(uint8_t level)", "void FrontlightManager::clearFault()",
                   "bool FrontlightManager::checkStartupIdle()", "void FrontlightManager::failStartup(",
                   "void FrontlightManager::probeTick()"]:
        harness += function(source, marker) + "\n"
    harness += r'''
int main() {
 FrontlightManager m;
 for(unsigned raw=0;raw<256;++raw) {
   m.setBrightness(static_cast<uint8_t>(raw)); assert(written[0]<=325);
   assert(m._brightness==(raw>100?100:raw));
   m.setBrightnessLevel(static_cast<uint8_t>(raw)); assert(written[0]<=325);
 }
 m.setBrightness(1); assert(written[0]==1);
 m.setBrightnessLevel(1); assert(written[0]==1);
 m.setBrightnessLevel(255); assert(written[0]==325);
 gated=false;
 BoardConfig::ACTIVE.frontlight.gpioWarm=43;
 for(unsigned p=1;p<=100;++p) for(unsigned w=0;w<=100;w+=25) {
  m._warmPercent=w; m.setBrightness(p);
  assert(written[0]+written[1]==freeink::frontlight::mappedDuty(p,1023,50));
  assert(written[0]+written[1]<=325);
 }
 BoardConfig::ACTIVE.frontlight.maximumLegacyPercent=0;
 for(unsigned p=0;p<=100;++p) {
  m.setBrightness(p); assert(written[0]+written[1]==freeink::frontlight::legacyDuty(p,1023));
 }
 m.setBrightnessLevel(255); assert(written[0]+written[1]==1023);
 BoardConfig::ACTIVE.frontlight.gpioWarm=-1; gated=true;
 BoardConfig::ACTIVE.frontlight.maximumLegacyPercent=50;
 assert(m.checkStartupIdle());
 m._startupChecked=false; adcMv=0;
 assert(!m.checkStartupIdle()); assert(m.startupBlocked() && m.dark==1);
 m.clearFault(); assert(!m.startupBlocked() && m._startupRetryUsed);
 assert(!m.checkStartupIdle()); m.clearFault(); assert(m.startupBlocked());
 FrontlightManager unavailable;
 unavailable._begun=false;
 unavailable._startupFault.store(freeink::frontlight::StartupFault::NoSamples);
 unavailable.clearFault(); assert(unavailable.startupBlocked() && !unavailable._startupRetryUsed);
 FrontlightManager probe;
 nowUs=500000; adcAvailable=false;
 probe.probeTick(); assert(probe.startupBlocked() && probe.passed==0 && probe.dark==1);
 FrontlightManager low;
 adcAvailable=true; adcMv=400;
 low.probeTick(); assert(low.startupBlocked() && low.passed==0 && low.tripped==0);
 FrontlightManager strip;
 adcMv=700; strip.probeTick(); assert(strip.passed==1 && !strip.startupBlocked());
 FrontlightManager good;
 adcMv=1500; good.probeTick(); assert(good.passed==1 && !good.startupBlocked());
 FrontlightManager open;
 adcMv=2600; open.probeTick(); assert(open.tripped==1 && open.passed==0);
}
'''
    run(harness, temp, "production-duty-and-probe", [SDK / "include"])

    stubs = temp / "stubs"
    stubs.mkdir()
    (stubs / "BoardConfig.h").write_text(r'''
#pragma once
#include <cstdint>
#define FREEINK_CAP_FRONTLIGHT_PROBE 1
#define CROSSPOINT_TEST_SUITE 0
namespace BoardConfig {
constexpr int PIN_UNASSIGNED=-1;
struct { struct { int colorSelectGpio=40,enablePulseUs=150,monitorAdc=2; uint16_t restoreHoldoffMs=250; } frontlight; } ACTIVE;
}
''')
    (stubs / "Logging.h").write_text("#define LOG_INF(...) ((void)0)\n#define LOG_ERR(...) ((void)0)\n")
    (stubs / "esp_system.h").write_text("#define ESP_OK 0\ninline int esp_register_shutdown_handler(void(*)()) { return 0; }\n")
    (stubs / "FrontlightManager.h").write_text(r'''
#pragma once
#include <BoardConfig.h>
#include <FrontlightStartup.h>
#include <FrontlightBrightness.h>
#include <FrontlightLoad.h>
inline uint32_t clockMs=0;
inline uint32_t millis(){return clockMs;}
inline int attempts=0;
inline bool failEnable=false;
class FrontlightManager {
public:
 bool failed=false,lit=false; uint8_t warm=50;
 void begin(){} bool present()const{return true;} bool hasColorTemperature()const{return true;}
 bool blendsColour()const{return true;} bool blending()const{return true;}
 static uint8_t normalizeBrightness(uint8_t p){return freeink::frontlight::normalizeBrightness(p);}
 static uint8_t brightnessStep(){return 1;} static uint8_t migrateLegacyBrightness(uint8_t p,bool stepped=false){return freeink::frontlight::migrateBrightnessDown(p,1023,50,stepped);}
 void setBrightness(uint8_t b){lit=b!=0;if(lit){++attempts;if(failEnable)failed=true;}}
 void setColorTemperature(uint8_t w){warm=w;} uint8_t colorTemperature()const{return warm;}
 bool restoreAllowed()const{return true;} bool fault()const{return failed;}
 bool startupBlocked()const{return failed;} void clearFault(){failed=false;}
 bool probing()const{return false;} bool stringMissing(bool)const{return false;}
 freeink::frontlight::Load load()const{return freeink::frontlight::Load::Unknown;}
 void prepareForSleep(){lit=false;}
};
''')
    for app in [ROOT, ROOT.parent / "crossink"]:
        header = (app / "lib/hal/HalFrontlight.h").read_text(encoding="utf-8")
        cpp = (app / "lib/hal/HalFrontlight.cpp").read_text(encoding="utf-8")
        (stubs / "HalFrontlight.h").write_text(header, encoding="utf-8")
        main = r'''
#include <cassert>
int main() {
 clockMs=0; Frontlight.begin(100,50,true); assert(attempts==0 && !Frontlight.isOn());
 clockMs=10000; Frontlight.updateRestore(0); assert(attempts==0);
 clockMs=1249; Frontlight.updateRestore(1000); assert(attempts==0);
 clockMs=1250; Frontlight.updateRestore(1000); assert(attempts==1 && Frontlight.isOn());
 Frontlight.setOn(false); Frontlight.begin(100,50,true); Frontlight.setOn(false);
 clockMs=20000; Frontlight.updateRestore(1000); assert(attempts==1);
 Frontlight.begin(100,50,true); Frontlight.setOn(true); assert(attempts==2 && Frontlight.isOn());
 Frontlight.setOn(false); Frontlight.begin(100,50,true); failEnable=true;
 Frontlight.updateRestore(1000); assert(!Frontlight.isOn()); assert(Frontlight.takeStartupNotice());
 assert(!Frontlight.takeStartupNotice());
}
'''
        run(cpp + main, temp, app.name + "-hal", [stubs, SDK / "include"])

# Verify wiring at the boot/persistence boundaries alongside executable policy checks.
for app in [ROOT, ROOT.parent / "crossink"]:
    settings = (app / "src/CrossPointSettings.cpp").read_text(encoding="utf-8")
    main = (app / "src/main.cpp").read_text(encoding="utf-8")
    assert 'doc["frontlightBrightnessScale"] = 2;' in settings
    assert "migrateLegacyBrightness(legacyBrightness, brightnessScale == 1)" in settings
    assert "Frontlight.updateRestore(display.firstPaintMs());" in main
    assert "freeink::frontlight::restoreEligible(" in main
light_up = function(source, "void FrontlightManager::lightUp(")
assert light_up.index("checkStartupIdle()") < light_up.index("gpio_set_level(")
print("Both boot/persistence integrations and pre-enable check ordering: PASS")

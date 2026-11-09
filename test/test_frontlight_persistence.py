"""Exercise the real JSON migration blocks and physical-paint completion hooks."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def function(source, marker):
    start = source.index(marker)
    opening = source.index("{", start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


source = r'''
#include <ArduinoJson.h>
#include <FrontlightBrightness.h>
#include <FrontlightStartup.h>
#include <atomic>
#include <cassert>
#include <cstdint>
#define FREEINK_DEVICE_SILKSCREEN 1
uint32_t clockMs=0;
uint32_t millis(){return clockMs;}
struct BoardConfig { static bool isSilkscreen(){return true;} };
int convertRefreshMode(int mode){return mode;}
struct HalFrontlight {
 static uint8_t migrateLegacyBrightness(uint8_t p,bool stepped=false){return freeink::frontlight::migrateBrightnessDown(p,1023,50,stepped);}
 static uint8_t normalizeBrightness(uint8_t p){return freeink::frontlight::normalizeBrightness(p);}
};
'''
for index, app in enumerate([ROOT, ROOT.parent / "crossink"]):
    settings = (app / "src/CrossPointSettings.cpp").read_text(encoding="utf-8")
    start = settings.index("#if FREEINK_DEVICE_SILKSCREEN\n  const uint8_t loadedBrightness")
    migration = settings[start:settings.index("#endif", start) + len("#endif")]
    source += f"namespace app{index} {{\n"
    source += "uint8_t load(JsonVariantConst doc,bool& needsResave) { uint8_t frontlightBrightness=doc[\"frontlightBrightness\"] | static_cast<uint8_t>(100);\n"
    source += migration + "\nreturn frontlightBrightness; }\n"
    header = (app / "lib/hal/HalDisplay.h").read_text(encoding="utf-8")
    display = (app / "lib/hal/HalDisplay.cpp").read_text(encoding="utf-8")
    source += r'''
class HalDisplay {
public:
 enum RefreshMode {FULL_REFRESH,HALF_REFRESH,FAST_REFRESH};
 struct { bool busy=false; void displayBufferAsyncNoShadow(int){busy=true;}
 void waitRefreshComplete(){busy=false;} void requestResync(int){} } einkDisplay;
 std::atomic<uint32_t> firstPaint{0}, batteryLastRefresh{0};
 std::atomic<bool> batteryRefreshInProgress{false}; bool asyncPaintPending=false;
 bool isX3Panel()const{return false;}
 void displayBufferAsync(RefreshMode); void waitRefreshComplete();
'''
    source += function(header, "uint32_t firstPaintMs() const") + "\n"
    for marker in ["bool batteryRefreshActive() const", "bool batteryRefreshRecent(", "void noteBatteryRefreshStart()", "void noteBatteryRefreshEnd()", "void notePaintComplete()"]:
        source += function(header, marker) + "\n"
    source += "};\n"
    source += function(display, "void HalDisplay::displayBufferAsync(") + "\n"
    source += function(display, "void HalDisplay::waitRefreshComplete()") + "\n"
    source += r'''
void check() {
 for(unsigned scale=0;scale<2;++scale) for(unsigned legacy=0;legacy<=100;++legacy) {
  JsonDocument doc; doc["frontlightBrightness"]=legacy; doc["frontlightOn"]=1;
  if(scale) doc["frontlightBrightnessScale"]=scale;
  bool resave=false; const auto migrated=load(doc.as<JsonVariantConst>(),resave);
  assert(resave && doc["frontlightOn"].as<int>()==1);
  const auto oldDuty=scale==1?freeink::frontlight::legacySteppedDuty(legacy):freeink::frontlight::legacyDuty(legacy,1023);
  assert(freeink::frontlight::mappedDuty(migrated,1023,50)<=oldDuty);
  doc["frontlightBrightness"]=migrated; doc["frontlightBrightnessScale"]=2;
  resave=false; assert(load(doc.as<JsonVariantConst>(),resave)==migrated && !resave);
 }
 JsonDocument imported; imported["frontlightBrightness"]=100; imported["frontlightBrightnessScale"]=1;
 bool resave=false; assert(load(imported.as<JsonVariantConst>(),resave)==69 && resave);
 for(unsigned percent=0;percent<=100;++percent) {
  imported["frontlightBrightness"]=percent; imported["frontlightBrightnessScale"]=2;
  resave=false; assert(load(imported.as<JsonVariantConst>(),resave)==percent && !resave);
 }
 HalDisplay display; clockMs=1000;
 display.displayBufferAsync(HalDisplay::FAST_REFRESH);
 assert(display.einkDisplay.busy && display.firstPaintMs()==0 && display.batteryRefreshActive());
 clockMs=1500; display.waitRefreshComplete();
 assert(!display.einkDisplay.busy && display.firstPaintMs()==1500 && !display.batteryRefreshActive());
 assert(display.batteryRefreshRecent(3499,2000) && !display.batteryRefreshRecent(3500,2000));
 clockMs=1749; assert(!freeink::frontlight::restoreDue(clockMs,display.firstPaintMs(),250));
 clockMs=1750; assert(freeink::frontlight::restoreDue(clockMs,display.firstPaintMs(),250));
 display.notePaintComplete(); assert(display.firstPaintMs()==1500);
}
}
'''
source += "int main(){app0::check();app1::check();}\n"
with tempfile.TemporaryDirectory(prefix="frontlight-persistence-") as directory:
    cpp = Path(directory) / "test.cpp"
    exe = Path(directory) / "test.exe"
    cpp.write_text(source, encoding="utf-8")
    result = subprocess.run(["c++", "-std=c++17", str(cpp), "-o", str(exe),
                             "-I" + str(ROOT / ".pio/libdeps/silkscreen/ArduinoJson/src"),
                             "-I" + str(ROOT / "freeink-sdk/libs/hardware/FrontlightManager/include")],
                            text=True, capture_output=True)
    if result.returncode:
        print(result.stdout + result.stderr, flush=True)
        result.check_returncode()
    subprocess.run([str(exe)], check=True)
print("Both apps: output migration, repeat loads, saved-on preservation and async paint completion PASS")

"""Regression cases for startup failures while panels and repeating gestures own input."""
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
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <SwipeAdjustment.h>
#define FREEINK_CAP_FRONTLIGHT_PROBE 1
bool silk=true;
namespace BoardConfig { bool isSilkscreen(){return silk;} }
struct CrossPointSettings { enum TWO_FINGER_SWIPE_ACTION {TWO_FINGER_SWIPE_INCREASE_BRIGHTNESS,TWO_FINGER_SWIPE_DECREASE_BRIGHTNESS,TWO_FINGER_SWIPE_INCREASE_WARMTH,TWO_FINGER_SWIPE_DECREASE_WARMTH}; };
struct HalFrontlight { static uint8_t brightnessStep(){return 1;} };
struct { uint8_t frontlightBrightness=100,frontlightWarmth=50,frontlightOn=1;
 bool screenInverted=false,disableReaderTouchscreen=false;
 void saveToFile(){} } SETTINGS;
struct {
 bool blocked=true,on=false,pending=false,failDuringBrightness=false; unsigned onCalls=0; uint8_t level=100;
 bool isOn()const{return on;} bool startupBlocked()const{return blocked;}
 bool switchOnPending()const{return pending;} bool takeNoLightNotice(){return false;}
 bool stringMissing(bool)const{return false;} bool present()const{return true;}
 void setOn(bool requested){if(requested)++onCalls;on=requested&&!blocked;}
 uint8_t brightness()const{return level;} uint8_t warmth()const{return 50;}
 void setBrightness(uint8_t value){level=value;if(failDuringBrightness){blocked=true;on=false;}}
} Frontlight;
struct Activity { static void onExit(){} void onExternalFrontlightChange(){} };
struct ActivityManager { void persistGlobalSettings(){} } activityManager;
struct { void setReaderTouchscreenOverride(bool){} void setTouchscreenRecoveryOverride(bool){} } mappedInput;
'''
for index, app in enumerate([ROOT, ROOT.parent / "crossink"]):
    text = (app / "src/activities/util/FrontlightPanelActivity.cpp").read_text(encoding="utf-8")
    source += f"namespace app{index} {{\n"
    panel_class = r'''
class FrontlightPanelActivity: public Activity {
public:
 uint8_t brightness=100,warmth=50;
 bool lightOn=false,lightOnChanged=false,awaitingSwitchOnResult=false,noLightNote=false;
 bool initialInversion=false,initialTouchscreenDisabled=false,pendingTouchscreenDisabled=false;
 bool previousTouchscreenRecoveryOverride=false;
 struct {bool showReaderDetails=false;} context;
 void requestUpdate(){} bool warmthLocked()const{return false;}
 void toggleLight(); void noteSwitchOnResult(); void syncLightState();
 void persistLightSettings(); void onExit();
};
'''
    source += panel_class if index == 0 else panel_class.replace("bool lightOn=", "bool observedLightOn=")
    for marker in ["void FrontlightPanelActivity::toggleLight()", "void FrontlightPanelActivity::noteSwitchOnResult()",
                   "void FrontlightPanelActivity::syncLightState()",
                   "void FrontlightPanelActivity::persistLightSettings()" if index == 0
                   else "void FrontlightPanelActivity::onExit()"]:
        source += function(text, marker) + "\n"
    source += "void check(){\n"
    source += r'''
 silk=true; Frontlight.blocked=true; Frontlight.on=false; Frontlight.pending=false;
 SETTINGS.frontlightOn=1; SETTINGS.frontlightBrightness=100; SETTINGS.frontlightWarmth=50;
 FrontlightPanelActivity panel;
 panel.toggleLight(); panel.syncLightState(); panel.syncLightState();
'''
    source += "assert(!panel.lightOn);\n" if index == 0 else "assert(!panel.observedLightOn);\n"
    source += "panel.persistLightSettings();\n" if index == 0 else "panel.onExit();\n"
    source += r'''
 assert(SETTINGS.frontlightOn==1);
 panel.warmth=60;
'''
    source += "panel.persistLightSettings();\n" if index == 0 else "panel.onExit();\n"
    source += r'''
 assert(SETTINGS.frontlightOn==1 && SETTINGS.frontlightWarmth==60);
 Frontlight.blocked=false; Frontlight.on=true; panel.toggleLight();
'''
    source += "panel.persistLightSettings();\n" if index == 0 else "panel.onExit();\n"
    source += "assert(SETTINGS.frontlightOn==0);\n}\n}\n"

activity = (ROOT.parent / "crossink/src/activities/ActivityManager.cpp").read_text(encoding="utf-8")
case = function(activity, "case CrossPointSettings::TWO_FINGER_SWIPE_DECREASE_BRIGHTNESS:")
body = case[case.index("{") + 1:-1]
source += "bool swipe(Activity& activity,ActivityManager& activityManager,uint8_t action,int lightAmount,bool persist){\n"
source += body + "\n}\n"
source += "bool applyConfiguredSwipeAction(Activity& a,ActivityManager& m,uint8_t action,int amount,bool persist){return swipe(a,m,action,amount,persist); }\n"
header = (ROOT.parent / "crossink/src/activities/ActivityManager.h").read_text(encoding="utf-8")
source += function(header, "struct LiveLightSwipeState") + ";\n"
source += function(activity, "void updateLiveLightSwipe(") + "\n"
source += function(activity, "void cancelLiveLightSwipe(") + "\n"

source += r'''
int main(){
 app0::check(); app1::check();
 silk=true; Frontlight.blocked=true; Frontlight.on=false; Frontlight.onCalls=0;
 Activity activity;
 for(int i=0;i<10;++i) swipe(activity,activityManager,CrossPointSettings::TWO_FINGER_SWIPE_DECREASE_BRIGHTNESS,10,false);
 assert(Frontlight.onCalls==0 && !Frontlight.on);
 Frontlight.blocked=false;
 swipe(activity,activityManager,CrossPointSettings::TWO_FINGER_SWIPE_INCREASE_BRIGHTNESS,10,true);
 assert(Frontlight.onCalls==1 && Frontlight.on);
 Frontlight.level=50;
 swipe(activity,activityManager,CrossPointSettings::TWO_FINGER_SWIPE_INCREASE_BRIGHTNESS,5,true);
 assert(Frontlight.level==51);
 silk=false; Frontlight.level=50;
 swipe(activity,activityManager,CrossPointSettings::TWO_FINGER_SWIPE_INCREASE_BRIGHTNESS,5,true);
 assert(Frontlight.level==55); silk=true;
 // Failure after capturing previousOn must not turn an adjustment into a retry.
 Frontlight.onCalls=0; Frontlight.failDuringBrightness=true;
 swipe(activity,activityManager,CrossPointSettings::TWO_FINGER_SWIPE_INCREASE_BRIGHTNESS,10,false);
 assert(Frontlight.onCalls==0 && Frontlight.blocked && !Frontlight.on);
 Frontlight.failDuringBrightness=false;
 // Return-to-origin and rotation cancellation must preserve captured saved intent.
 for(bool initialOn : {false,true}) for(bool savedOn : {false,true}) {
  LiveLightSwipeState state;
  state.initialOn=initialOn; state.initialSavedOn=savedOn; state.initialValue=50;
  state.action=CrossPointSettings::TWO_FINGER_SWIPE_INCREASE_BRIGHTNESS;
  Frontlight.blocked=true; Frontlight.on=false; Frontlight.level=60; Frontlight.onCalls=0;
  SETTINGS.frontlightOn=1;
  updateLiveLightSwipe(activity,activityManager,state,0);
  cancelLiveLightSwipe(activity,activityManager,state);
  assert(Frontlight.onCalls==0 && !Frontlight.on && SETTINGS.frontlightOn==(savedOn?1:0));
 }
}
'''
with tempfile.TemporaryDirectory(prefix="frontlight-callers-") as directory:
    cpp = Path(directory) / "test.cpp"
    exe = Path(directory) / "test.exe"
    cpp.write_text(source, encoding="utf-8")
    result = subprocess.run(["c++", "-std=c++17", str(cpp), "-o", str(exe), "-I" + str(ROOT.parent / "crossink/src/util")], text=True, capture_output=True)
    if result.returncode:
        print(result.stdout + result.stderr, flush=True)
        result.check_returncode()
    subprocess.run([str(exe)], check=True)
print("Both panels retain saved-on; gesture adjustment/race/rollback/cancel preserve retry and saved intent PASS")

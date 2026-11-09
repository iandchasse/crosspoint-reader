"""Exercise production setting visibility and JSON handling with a small host fixture."""

from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def function(source, marker):
    start = source.index(marker)
    opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


settings_header = (ROOT / "src/SettingsList.h").read_text(encoding="utf-8")
settings_source = (ROOT / "src/SettingsList.cpp").read_text(encoding="utf-8")
preferences_header = (ROOT / "src/CrossPointSettings.h").read_text(encoding="utf-8")
preferences_source = (ROOT / "src/CrossPointSettings.cpp").read_text(encoding="utf-8")
activity = (ROOT / "src/activities/settings/SettingsActivity.h").read_text(encoding="utf-8")
main_source = (ROOT / "src/main.cpp").read_text(encoding="utf-8")

# The SDK setter requires a live driver, and boot activities can paint before loop().
setup_display = function(main_source, "void setupDisplayAndFonts(bool seamless = false)")
assert (setup_display.index("display.begin(seamless);")
        < setup_display.index("display.setStandardHalfRefresh(SETTINGS.standardHalfRefresh != 0);")
        < setup_display.index("renderer.begin();")
        < setup_display.index("activityManager.begin();"))
setup = function(main_source, "void setup()")
assert setup.index("SETTINGS.loadFromFile();") < setup.index("setupDisplayAndFonts(resume != BootResume::Splash);")
print("Saved HALF preference is applied after driver initialization and before boot rendering")

visibility = (function(settings_header, "inline bool isHiddenOnDevice(") + "\n"
              + function(settings_header, "template <typename Wants, typename Visit>\nvoid forEachSetting(") + "\n"
              + function(settings_header, "inline std::vector<SettingInfo> getSettingsList("))
toggle = function(activity, "static SettingInfo Toggle(")
descriptor = re.search(
    r"#if FREEINK_DEVICE_SILKSCREEN && !FREEINK_SILKSCREEN_368\s+"
    r"emit\(SettingInfo::Toggle\(StrId::STR_STANDARD_HALF_REFRESH,.*?#endif",
    settings_source, re.S).group()
field = re.search(r"uint8_t standardHalfRefresh\s*=\s*\d+;", preferences_header).group()
numeric_start = preferences_source.index("const uint8_t fieldDefault =")
numeric_end = preferences_source.index("s.*(info.valuePtr) = v;", numeric_start) + len("s.*(info.valuePtr) = v;")
numeric_load = preferences_source[numeric_start:numeric_end]
clamp = re.search(r"auto clamp = .*?; };", preferences_source).group()
numeric_save = re.search(r"doc\[info.key\] = s\.\*\(info.valuePtr\);", preferences_source).group()
labels = sorted(set(re.findall(r"StrId::(STR_\w+)", visibility + descriptor + toggle)) | {"STR_SENTINEL"})
arduino_json = ROOT / ".pio/libdeps/silkscreen/ArduinoJson/src"
if not (arduino_json / "ArduinoJson.h").is_file():
    raise SystemExit("Build silkscreen first: its installed ArduinoJson headers are required")

harness = r"""
#include <ArduinoJson.h>
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
""" + "enum class StrId { " + ", ".join(labels) + " };\n" + r"""
enum class SettingType { TOGGLE, ENUM, ACTION, VALUE, STRING };
struct CrossPointSettings {
  enum { SLIDE_SWITCH_NIGHT_MODE, SLIDE_SWITCH_TOUCH_CONTROLS };
  uint8_t vocabularyHistoryEnabled = 0;
""" + field + r"""
} SETTINGS;
struct SettingInfo {
  StrId nameId = StrId::STR_SENTINEL;
  SettingType type = SettingType::TOGGLE;
  uint8_t CrossPointSettings::*valuePtr = nullptr;
  const char* key = nullptr;
  StrId category = StrId::STR_NONE_OPT;
  struct { uint8_t min=0, max=255; } valueRange;
  std::vector<StrId> enumLabels() const { return {}; }
""" + toggle + r"""
};
namespace BoardConfig {
enum class DisplayController { SSD1677, UC8279, UC8253 };
struct { DisplayController displayController; } ACTIVE;
bool touch = false;
bool hasTouch() { return touch; }
bool hasFrontButtons() { return true; }
}
struct { bool present() { return true; } } Frontlight;
struct { bool isAvailable() { return true; } } halTiltSensor;
struct { bool isAvailable() { return true; } } halClock;
// These profiles have no bound physical slide switch.
bool slideSwitchOwnsLight() { return false; }
bool slideSwitchOwns(int) { return false; }
struct DictionaryEntry {};
struct SdCardFontRegistry { int getFamilyCount() const { return 0; } };
SettingInfo buildFontFamilySetting(const SdCardFontRegistry*) { return {}; }
SettingInfo buildFontSizeSetting(const SdCardFontRegistry*) { return {}; }
SettingInfo buildDictionarySetting(const std::vector<DictionaryEntry>&) { return {}; }
inline constexpr char DICTIONARY_SETTING_KEY[] = "dictionaryName";
void buildFontOptions(SettingInfo&, const SdCardFontRegistry*) {}
std::vector<SettingInfo> getBaseSettingsList(size_t = 0) {
  std::vector<SettingInfo> rows;
  const auto emit = [&](SettingInfo info) { rows.push_back(info); };
""" + descriptor + r"""
  rows.push_back({});
  return rows;
}
size_t countBaseSettings() { return getBaseSettingsList().size(); }
template <typename Fn> void forEachBaseSetting(Fn& fn) {
  for (auto& row : getBaseSettingsList()) fn(row);
}
""" + visibility + r"""
bool hasToggle(const std::vector<SettingInfo>& rows) {
  return std::any_of(rows.begin(),rows.end(),[](const SettingInfo& info) {
    return info.valuePtr == &CrossPointSettings::standardHalfRefresh;
  });
}
void load(CrossPointSettings& s, JsonVariantConst doc) {
""" + clamp + r"""
  for (const auto& info : getBaseSettingsList()) {
    if (!info.key) continue;
""" + numeric_load + r"""
  }
}
int main() {
  constexpr bool offered = FREEINK_DEVICE_SILKSCREEN && !FREEINK_SILKSCREEN_368;
  using Controller = BoardConfig::DisplayController;
  for (auto controller : {Controller::SSD1677,Controller::UC8279,Controller::UC8253}) {
    BoardConfig::ACTIVE.displayController = controller;
    for (bool touch : {false,true}) {
      BoardConfig::touch = touch;
      SETTINGS.standardHalfRefresh = 1;
      assert(hasToggle(getBaseSettingsList()) == offered);
      const auto rows = getSettingsList();
      const bool visible = offered && controller == Controller::SSD1677;
      assert(hasToggle(rows) == visible);
      assert(rows.size() == (visible ? 2 : 1));
      std::vector<SettingInfo> streamed;
      forEachSetting(nullptr, nullptr, [](const char*) { return true; },
                     [&](SettingInfo& s) { streamed.push_back(s); });
      assert(streamed.size() == rows.size() && hasToggle(streamed) == visible);
      streamed.clear();
      forEachSetting(nullptr, nullptr, [](const char* key) { return key && std::string(key) == "standardHalfRefresh"; },
                     [&](SettingInfo& s) { streamed.push_back(s); });
      assert(streamed.size() == (visible ? 1 : 0) && hasToggle(streamed) == visible);
      assert(SETTINGS.standardHalfRefresh == 1);
    }
    // Persistence must work even before detection, or with another panel fitted.
    for (const char* json : {"{}", "{\"standardHalfRefresh\":0}", "{\"standardHalfRefresh\":1}",
                             "{\"standardHalfRefresh\":2}", "{\"standardHalfRefresh\":255}",
                             "{\"standardHalfRefresh\":null}", "{\"standardHalfRefresh\":\"bad\"}"}) {
      CrossPointSettings restored;
      JsonDocument input;
      assert(!deserializeJson(input,json));
      load(restored,input.as<JsonVariantConst>());
      const bool enabled = std::string(json) == "{\"standardHalfRefresh\":1}";
      assert(restored.standardHalfRefresh == (offered && enabled ? 1 : 0));
    }
    CrossPointSettings s;
    s.standardHalfRefresh = 1;
    JsonDocument doc;
    for (const auto& info : getBaseSettingsList()) {
      if (!info.key) continue;
""" + numeric_save + r"""
    }
    assert(doc["standardHalfRefresh"].is<uint8_t>() == offered);
    if (offered) assert(doc["standardHalfRefresh"].as<uint8_t>() == 1);
  }
  puts("HALF preference: board/controller visibility, touch parity, persistence and invalid JSON passed");
}
"""

with tempfile.TemporaryDirectory(prefix="half-refresh-settings-") as directory:
    cpp = Path(directory) / "settings.cpp"
    cpp.write_text(harness, encoding="utf-8")
    for silk, small in ((1, 0), (1, 1), (0, 0)):
        exe = Path(directory) / f"settings-{silk}-{small}.exe"
        subprocess.run(["c++", "-std=c++20", "-Wall", "-Wextra", "-Werror",
                        f"-DFREEINK_DEVICE_SILKSCREEN={silk}", f"-DFREEINK_SILKSCREEN_368={small}",
                        "-I" + str(arduino_json), str(cpp), "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)

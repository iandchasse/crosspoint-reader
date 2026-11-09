"""Button-menu compatibility: labels, stored values, hidden choices and web indices.

Run with python test/test_button_settings.py (host C++ compiler required).
"""

from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
settings = (ROOT / "src/SettingsList.h").read_text(encoding="utf-8")
start = settings.index("inline bool keyFrontlightAvailable()")
end = settings.index("inline std::vector<StrId> homeThemeValues()", start)
production = settings[start:end]
header = (ROOT / "src/CrossPointSettings.h").read_text(encoding="utf-8")
enums = "\n".join(re.search(r"enum " + name + r"\s*\{.*?\};", header, re.S).group()
                  for name in ("SHORT_PWRBTN", "LONG_PRESS_MENU_FUNCTION"))
labels = sorted(set(re.findall(r"StrId::(STR_\w+)", production)))
harness = r"""
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <vector>
""" + "enum class StrId : uint16_t { " + ", ".join(labels) + " };\n" + r"""
struct CrossPointSettings {
""" + enums + r"""
  uint8_t longPressMenuFunction = 0;
  uint8_t shortPwrBtn = 0;
} SETTINGS;
namespace BoardConfig {
bool silk, touch, home, keys, pwm;
bool isSilkscreen() { return silk; }
bool hasTouch() { return touch; }
bool hasHomeKey() { return home; }
bool hasFrontButtons() { return !touch || keys; }
bool hasPwmFrontlight() { return pwm; }
}
struct SettingInfo {
  std::vector<StrId> labels;
  uint8_t (*get)();
  void (*set)(uint8_t);
  static SettingInfo DynamicEnum(StrId, std::vector<StrId> labels, uint8_t (*get)(), void (*set)(uint8_t),
                                 const char*, StrId) {
    return {std::move(labels), get, set};
  }
};
""" + production + r"""
int position(const std::vector<uint8_t>& values, uint8_t value, int fallback) {
  const auto it = std::find(values.begin(), values.end(), value);
  return it == values.end() ? fallback : static_cast<int>(it - values.begin());
}
int main() {
  // These fixtures pin the existing storage contract, independently of the menu builders.
  static_assert(CrossPointSettings::LP_MENU_FRONTLIGHT_PANEL == 7);
  static_assert(CrossPointSettings::TOGGLE_FRONTLIGHT == 10);
  const std::vector<uint8_t> longMenus[] = {
      {0, 1, 2, 3, 5, 6}, {0, 1, 2, 3, 5, 6, 7}, {0, 1, 2, 3, 4, 5, 6}};
  const std::vector<uint8_t> shortMenus[] = {
      {0, 1, 2, 3, 4, 6}, {0, 1, 2, 3, 4, 6, 10},
      {0, 1, 2, 3, 4, 5}, {0, 1, 2, 3, 4, 5, 10},
      {0, 1, 2, 3, 4, 5, 7, 8, 9}, {0, 1, 2, 3, 4, 5, 7, 8, 9, 10}};
  const StrId longLabels[] = {
      StrId::STR_KOSYNC, StrId::STR_DISABLED, StrId::STR_BOOKMARK_OPTION, StrId::STR_DICTIONARY,
      StrId::STR_READER_MENU, StrId::STR_SAVE_CLIPPING, StrId::STR_FOOTNOTES, StrId::STR_FRONTLIGHT};
  const StrId shortLabels[] = {
      StrId::STR_IGNORE, StrId::STR_SLEEP, StrId::STR_PAGE_TURN, StrId::STR_FORCE_REFRESH,
      StrId::STR_FOOTNOTES, StrId::STR_CONFIRM, StrId::STR_SAVE_CLIPPING, StrId::STR_BOOKMARK_OPTION,
      StrId::STR_KOSYNC, StrId::STR_READER_MENU, StrId::STR_TOGGLE_FRONTLIGHT};
  int configurations = 0;
  for (int mask = 0; mask < 32; ++mask) {
    BoardConfig::silk = mask & 1;
    BoardConfig::touch = mask & 2;
    BoardConfig::home = mask & 4;
    BoardConfig::keys = mask & 8;
    BoardConfig::pwm = mask & 16;
    const bool light = FREEINK_CAP_FRONTLIGHT && BoardConfig::silk && BoardConfig::pwm;
    const auto& longValues = longMenus[BoardConfig::home ? 2 : light ? 1 : 0];
    const bool bareTouch = BoardConfig::touch && !BoardConfig::home && !BoardConfig::keys;
    const auto& shortValues = shortMenus[(BoardConfig::touch ? bareTouch ? 4 : 2 : 0) + light];
    const auto longSetting = buildLongPressMenuSetting();
    const auto shortSetting = buildShortPowerButtonSetting();
    assert(longSetting.labels.size() == longValues.size());
    assert(shortSetting.labels.size() == shortValues.size());
    for (size_t i = 0; i < longValues.size(); ++i) {
      assert(longSetting.labels[i] == longLabels[longValues[i]]);
      longSetting.set(static_cast<uint8_t>(i));
      assert(SETTINGS.longPressMenuFunction == longValues[i] && longSetting.get() == i);
    }
    for (size_t i = 0; i < shortValues.size(); ++i) {
      assert(shortSetting.labels[i] == shortLabels[shortValues[i]]);
      shortSetting.set(static_cast<uint8_t>(i));
      assert(SETTINGS.shortPwrBtn == shortValues[i] && shortSetting.get() == i);
    }
    assert(shortPowerButtonToggleFrontlightIndex() == position(shortValues, 10, -1));
    for (unsigned raw = 0; raw <= UINT8_MAX; ++raw) {
      SETTINGS.longPressMenuFunction = SETTINGS.shortPwrBtn = static_cast<uint8_t>(raw);
      assert(longSetting.get() == position(longValues, static_cast<uint8_t>(raw), 1));
      assert(shortSetting.get() == position(shortValues, static_cast<uint8_t>(raw), 0));
      assert(longPressMenuValueAccepted(static_cast<uint8_t>(raw)) ==
             (raw <= 6 || (raw == 7 && light && !BoardConfig::home)));
      const bool ambiguousLegacy = BoardConfig::silk && raw >= 7 && raw <= 9;
      assert(shortPowerButtonValueAccepted(static_cast<uint8_t>(raw)) ==
             ((raw <= 9 && !ambiguousLegacy) || (raw == 10 && light)));
      if (raw >= longValues.size()) {
        longSetting.set(static_cast<uint8_t>(raw));
        assert(SETTINGS.longPressMenuFunction == raw);
      }
      if (raw >= shortValues.size()) {
        shortSetting.set(static_cast<uint8_t>(raw));
        assert(SETTINGS.shortPwrBtn == CrossPointSettings::IGNORE);
      }
    }
    ++configurations;
  }
  std::printf("%d button-menu configurations passed (frontlight=%d)\n", configurations, FREEINK_CAP_FRONTLIGHT);
}
"""
with tempfile.TemporaryDirectory(prefix="button-settings-") as directory:
    cpp = Path(directory) / "settings.cpp"
    cpp.write_text(harness, encoding="utf-8")
    for frontlight in (0, 1):
        executable = Path(directory) / f"settings-{frontlight}.exe"
        subprocess.run(["c++", "-std=c++20", "-Wall", "-Wextra", "-Werror",
                        f"-DFREEINK_CAP_FRONTLIGHT={frontlight}", str(cpp), "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)

#pragma once

#include <BoardConfig.h>
#include <HalClock.h>
#include <HalFrontlight.h>
#include <HalTiltSensor.h>
#include <I18n.h>
#include <SdCardFontRegistry.h>

#include <algorithm>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>

#include "CrossPointSettings.h"
#include "HomeButtonSettings.h"
#include "KOReaderCredentialStore.h"
#include "ReaderFontSizes.h"
#include "activities/settings/SettingsActivity.h"
#include "components/UITheme.h"
#include "util/DictionaryRegistry.h"

// Build the font family setting dynamically. When registry is non-null, SD card fonts
// are appended after the built-in fonts. Otherwise only built-in fonts are listed.
inline SettingInfo buildFontFamilySetting(const SdCardFontRegistry* registry) {
  // Built-in font labels (StrId)
  std::vector<StrId> enumValues = {StrId::STR_NOTO_SANS};
  // Runtime string labels for SD card fonts
  std::vector<std::string> enumStringValues;

  // Reserve: first CrossPointSettings::BUILTIN_FONT_COUNT entries use StrId, rest use strings
  if (registry) {
    const auto& families = registry->getFamilies();
    enumStringValues.reserve(families.size());
    std::transform(families.begin(), families.end(), std::back_inserter(enumStringValues),
                   [](const SdCardFontFamilyInfo& f) { return f.name; });
  }

  // Capture the SD font count for the lambdas
  const int sdFontCount = static_cast<int>(enumStringValues.size());

  // Total option count = built-in + SD card families
  // For the combined enumStringValues: we need all entries as strings (built-in names + SD names)
  // The render code checks enumStringValues first, then enumValues. So we build enumStringValues
  // with all options when SD fonts are present.
  std::vector<std::string> allStringValues;
  if (sdFontCount > 0) {
    allStringValues.push_back(I18N.get(StrId::STR_NOTO_SANS));
    allStringValues.insert(allStringValues.end(), enumStringValues.begin(), enumStringValues.end());
  }

  SettingInfo s;
  s.nameId = StrId::STR_FONT_FAMILY;
  s.type = SettingType::ENUM;
  s.enumValues = std::move(enumValues);
  s.enumStringValues = std::move(allStringValues);
  s.key = "fontFamily";
  s.category = StrId::STR_CAT_READER;
  s.inTextSettings = true;  // matches the static font-family entry it replaces

  // Capture registry families by copy for the lambdas
  std::vector<std::string> sdFamilyNames;
  if (registry) {
    const auto& families = registry->getFamilies();
    sdFamilyNames.reserve(families.size());
    std::transform(families.begin(), families.end(), std::back_inserter(sdFamilyNames),
                   [](const SdCardFontFamilyInfo& f) { return f.name; });
  }

  s.valueGetter = [sdFamilyNames]() -> uint8_t {
    // If an SD card font is selected, find its index
    if (SETTINGS.sdFontFamilyName[0] != '\0') {
      for (int i = 0; i < static_cast<int>(sdFamilyNames.size()); i++) {
        if (sdFamilyNames[i] == SETTINGS.sdFontFamilyName) {
          return static_cast<uint8_t>(CrossPointSettings::BUILTIN_FONT_COUNT + i);
        }
      }
      // SD font name not found in registry — fall through to built-in
    }
    return SETTINGS.fontFamily < CrossPointSettings::BUILTIN_FONT_COUNT ? SETTINGS.fontFamily : 0;
  };

  s.valueSetter = [sdFamilyNames](uint8_t v) {
    if (v < CrossPointSettings::BUILTIN_FONT_COUNT) {
      SETTINGS.fontFamily = v;
      SETTINGS.sdFontFamilyName[0] = '\0';
    } else {
      int sdIdx = v - CrossPointSettings::BUILTIN_FONT_COUNT;
      if (sdIdx < static_cast<int>(sdFamilyNames.size())) {
        strncpy(SETTINGS.sdFontFamilyName, sdFamilyNames[sdIdx].c_str(), sizeof(SETTINGS.sdFontFamilyName) - 1);
        SETTINGS.sdFontFamilyName[sizeof(SETTINGS.sdFontFamilyName) - 1] = '\0';
      }
    }
  };

  return s;
}

// Build the font size setting dynamically: the options are the point sizes the
// active family actually ships, so an SD family built at 10/12/14 offers three
// sizes and a family built at 8..18 offers six. The selected point size persists
// in SETTINGS.fontPointSize (saved/loaded manually in CrossPointSettings::
// toJson/fromJson — the generic loop skips dynamic entries), while the ENUM
// contract shared with the web UI stays index-based.
inline SettingInfo buildFontSizeSetting(const SdCardFontRegistry* registry) {
  // Captured by copy: getSettingsList() returns by value and the lambdas outlive
  // this call, so they must not reference the registry.
  const std::vector<uint8_t> sizes = readerFontPointSizes(registry, SETTINGS.sdFontFamilyName);

  // "pt" is deliberately not translated — see the matching note in
  // TextSettingsActivity::rebuildSizeList().
  std::vector<std::string> labels;
  labels.reserve(sizes.size());
  for (const uint8_t pt : sizes) {
    labels.push_back(std::to_string(pt) + " pt");
  }

  SettingInfo s;
  s.nameId = StrId::STR_FONT_SIZE;
  s.type = SettingType::ENUM;
  s.enumStringValues = std::move(labels);
  s.key = "fontSize";
  s.category = StrId::STR_CAT_READER;
  s.inTextSettings = true;  // matches the static font-size entry it replaces

  s.valueGetter = [sizes]() -> uint8_t {
    const uint8_t pt = snapToNearestPointSize(sizes, SETTINGS.fontPointSize);
    for (int i = 0; i < static_cast<int>(sizes.size()); i++) {
      if (sizes[i] == pt) return static_cast<uint8_t>(i);
    }
    return 0;
  };

  s.valueSetter = [sizes](uint8_t v) {
    if (v < sizes.size()) SETTINGS.fontPointSize = sizes[v];
  };

  return s;
}

// Build the dictionary selection setting dynamically from the folders discovered
// under /dictionaries. "None" plus one option per dictionary; the selected folder
// name persists in SETTINGS.dictionaryName (saved/loaded manually in
// CrossPointSettings::toJson/fromJson — the generic loop skips dynamic entries).
inline SettingInfo buildDictionarySetting(const std::vector<DictionaryEntry>& dictionaries) {
  std::vector<std::string> folderNames;
  folderNames.reserve(dictionaries.size());
  std::transform(dictionaries.begin(), dictionaries.end(), std::back_inserter(folderNames),
                 [](const DictionaryEntry& d) { return d.name; });

  SettingInfo s;
  s.nameId = StrId::STR_DICTIONARY;
  s.key = "dictionaryName";  // web settings API; persisted by name, not by the generic loop
  s.type = SettingType::ENUM;
  s.enumStringValues.reserve(folderNames.size() + 1);
  s.enumStringValues.push_back(I18N.get(StrId::STR_NONE_OPT));
  s.enumStringValues.insert(s.enumStringValues.end(), folderNames.begin(), folderNames.end());
  s.category = StrId::STR_CAT_READER;

  s.valueGetter = [folderNames]() -> uint8_t {
    for (size_t i = 0; i < folderNames.size(); i++) {
      // Compare within the settings field capacity: an over-long folder name is
      // stored truncated, and must still match its list entry.
      if (strncmp(folderNames[i].c_str(), SETTINGS.dictionaryName, sizeof(SETTINGS.dictionaryName) - 1) == 0) {
        return static_cast<uint8_t>(i + 1);
      }
    }
    return 0;  // "None", also when the stored folder no longer exists
  };

  s.valueSetter = [folderNames](uint8_t v) {
    if (v == 0 || v > folderNames.size()) {
      SETTINGS.dictionaryName[0] = '\0';
      return;
    }
    strncpy(SETTINGS.dictionaryName, folderNames[v - 1].c_str(), sizeof(SETTINGS.dictionaryName) - 1);
    SETTINGS.dictionaryName[sizeof(SETTINGS.dictionaryName) - 1] = '\0';
  };

  return s;
}

// The key shortcuts for the front light (Short Power: Toggle Frontlight, Long-press
// Confirm: Frontlight) are Silkscreen's, so every other board keeps its option lists.
// Read from the board profile: the settings list is built before Frontlight.begin().
inline bool keyFrontlightAvailable() {
#if FREEINK_CAP_FRONTLIGHT
  return BoardConfig::isSilkscreen() && BoardConfig::hasPwmFrontlight();
#else
  return false;
#endif
}

// The long-press Confirm values this board offers, in list order: upstream's (Reader Menu
// only with a Home key, then Save Clipping and Footnotes) and, after them, Silkscreen's
// front-light panel, only with a light and no Home key (a Home-key board reaches the light
// through its Home actions). The setting maps its list positions to these values, and the
// value is what is saved, so a value is never renumbered: upstream's keep theirs (Save
// Clipping 5, Footnotes 6).
struct LongPressMenuValues {
  uint8_t value[CrossPointSettings::LONG_PRESS_MENU_FUNCTION_COUNT];
  uint8_t count;
};

inline LongPressMenuValues longPressMenuValues() {
  LongPressMenuValues v = {{CrossPointSettings::LP_MENU_KOSYNC, CrossPointSettings::LP_MENU_DISABLED,
                            CrossPointSettings::LP_MENU_BOOKMARK, CrossPointSettings::LP_MENU_DICTIONARY},
                           4};
  if (BoardConfig::hasHomeKey()) v.value[v.count++] = CrossPointSettings::LP_MENU_READER_MENU;
  v.value[v.count++] = CrossPointSettings::LP_MENU_CREATE_CLIPPING;
  v.value[v.count++] = CrossPointSettings::LP_MENU_FOOTNOTES;
  if (!BoardConfig::hasHomeKey() && keyFrontlightAvailable()) {
    v.value[v.count++] = CrossPointSettings::LP_MENU_FRONTLIGHT_PANEL;
  }
  return v;
}

// Whether a saved long-press Confirm value loads. Upstream keeps each of its own values
// whatever the board offers (one saved with a Home key survives a visit to a board without),
// and so does this; the front-light panel is Silkscreen's and loads only where it is offered.
inline bool longPressMenuValueAccepted(const uint8_t value) {
  if (value == CrossPointSettings::LP_MENU_FRONTLIGHT_PANEL) {
    return !BoardConfig::hasHomeKey() && keyFrontlightAvailable();
  }
  return value <= CrossPointSettings::LP_MENU_FOOTNOTES;
}

inline std::vector<StrId> buildLongPressMenuValues() {
  const auto v = longPressMenuValues();
  std::vector<StrId> labels;
  labels.reserve(v.count);
  for (uint8_t i = 0; i < v.count; ++i) {
    switch (v.value[i]) {
      case CrossPointSettings::LP_MENU_KOSYNC:
        labels.push_back(StrId::STR_KOSYNC);
        break;
      case CrossPointSettings::LP_MENU_BOOKMARK:
        labels.push_back(StrId::STR_BOOKMARK_OPTION);
        break;
      case CrossPointSettings::LP_MENU_DICTIONARY:
        labels.push_back(StrId::STR_DICTIONARY);
        break;
      case CrossPointSettings::LP_MENU_READER_MENU:
        labels.push_back(StrId::STR_READER_MENU);
        break;
      case CrossPointSettings::LP_MENU_CREATE_CLIPPING:
        labels.push_back(StrId::STR_SAVE_CLIPPING);
        break;
      case CrossPointSettings::LP_MENU_FOOTNOTES:
        labels.push_back(StrId::STR_FOOTNOTES);
        break;
      case CrossPointSettings::LP_MENU_FRONTLIGHT_PANEL:
        labels.push_back(StrId::STR_FRONTLIGHT);
        break;
      case CrossPointSettings::LP_MENU_DISABLED:
      default:
        labels.push_back(StrId::STR_DISABLED);
        break;
    }
  }
  return labels;
}

// The saved value's position in this board's list; one the board does not offer shows as Disabled.
inline uint8_t longPressMenuDisplayValue() {
  const auto v = longPressMenuValues();
  const auto* it = std::find(v.value, v.value + v.count, SETTINGS.longPressMenuFunction);
  if (it == v.value + v.count) {
    it = std::find(v.value, v.value + v.count, static_cast<uint8_t>(CrossPointSettings::LP_MENU_DISABLED));
  }
  return static_cast<uint8_t>(it - v.value);
}

inline void setLongPressMenuFromDisplayValue(const uint8_t displayValue) {
  const auto v = longPressMenuValues();
  if (displayValue < v.count) SETTINGS.longPressMenuFunction = v.value[displayValue];
}

inline SettingInfo buildLongPressMenuSetting() {
  return SettingInfo::DynamicEnum(StrId::STR_LONG_PRESS_MENU, buildLongPressMenuValues(), longPressMenuDisplayValue,
                                  setLongPressMenuFromDisplayValue, "longPressMenuFunction", StrId::STR_CAT_CONTROLS);
}

struct ShortPowerButtonOption {
  StrId label;
  uint8_t value;
};

// Menu order differs from the persisted enum order, so map by value. Upstream's entries come
// first, in upstream's order, then Silkscreen's Toggle Frontlight (keyFrontlightAvailable()).
// Choosing it turns the power double-click off (CrossPointSettings::powerDoubleClickLight()),
// which would otherwise toggle twice.
inline std::vector<ShortPowerButtonOption> shortPowerButtonOptions() {
  std::vector<ShortPowerButtonOption> options = {{StrId::STR_IGNORE, CrossPointSettings::IGNORE},
                                                 {StrId::STR_SLEEP, CrossPointSettings::SLEEP},
                                                 {StrId::STR_PAGE_TURN, CrossPointSettings::PAGE_TURN},
                                                 {StrId::STR_FORCE_REFRESH, CrossPointSettings::FORCE_REFRESH},
                                                 {StrId::STR_FOOTNOTES, CrossPointSettings::FOOTNOTES}};
  options.reserve(CrossPointSettings::SHORT_PWRBTN_COUNT);
  if (BoardConfig::hasTouch()) {
    options.push_back({StrId::STR_CONFIRM, CrossPointSettings::PWR_CONFIRM});
  } else {
    options.push_back({StrId::STR_SAVE_CLIPPING, CrossPointSettings::CREATE_CLIPPING});
  }
  // Touch boards without a home key have no button for these shortcuts. Silkscreen keeps its keys
  // with touch (hasFrontButtons(): upstream's boards answer !hasTouch(), so nothing else changes), and
  // its power double-click defers every short-click action, which these (acted on at the release in
  // MappedInputManager::update()) do not wait for.
  if (BoardConfig::hasTouch() && !BoardConfig::hasHomeKey() && !BoardConfig::hasFrontButtons()) {
    options.push_back({StrId::STR_BOOKMARK_OPTION, CrossPointSettings::PWR_BOOKMARK});
    options.push_back({StrId::STR_KOSYNC, CrossPointSettings::PWR_SYNC});
    options.push_back({StrId::STR_READER_MENU, CrossPointSettings::PWR_READER_MENU});
  }
  if (keyFrontlightAvailable()) {
    options.push_back({StrId::STR_TOGGLE_FRONTLIGHT, CrossPointSettings::TOGGLE_FRONTLIGHT});
  }
  return options;
}

// Whether a saved short power button value loads: each of upstream's, whatever the board
// offers, and Toggle Frontlight (Silkscreen's) only where it is offered. Silkscreen never offers
// upstream's Bookmark, Sync and Reader Menu (above), and what it saved there before them was its
// Toggle Frontlight (then 7): such a file loads as Ignore, not as an action nobody picked.
inline bool shortPowerButtonValueAccepted(const uint8_t value) {
  if (value == CrossPointSettings::TOGGLE_FRONTLIGHT) return keyFrontlightAvailable();
  if (BoardConfig::isSilkscreen() && value >= CrossPointSettings::PWR_BOOKMARK &&
      value <= CrossPointSettings::PWR_READER_MENU) {
    return false;
  }
  return value <= CrossPointSettings::PWR_READER_MENU;
}

inline std::vector<StrId> buildShortPowerButtonValues() {
  std::vector<StrId> values;
  for (const auto& option : shortPowerButtonOptions()) values.push_back(option.label);
  return values;
}

// The saved value's position in this board's list; one the board does not offer shows as Ignore.
inline uint8_t shortPowerButtonDisplayValue() {
  const auto options = shortPowerButtonOptions();
  for (size_t i = 0; i < options.size(); i++) {
    if (options[i].value == SETTINGS.shortPwrBtn) return static_cast<uint8_t>(i);
  }
  return CrossPointSettings::IGNORE;
}

inline void setShortPowerButtonFromDisplayValue(const uint8_t displayValue) {
  const auto options = shortPowerButtonOptions();
  SETTINGS.shortPwrBtn = displayValue < options.size() ? options[displayValue].value : CrossPointSettings::IGNORE;
}

// Where Toggle Frontlight sits in this board's list, or -1 where it is not offered. The web
// settings page hides the power double-click row while it is chosen, and its position moves
// with the touch panel (Confirm is listed only with one).
inline int shortPowerButtonToggleFrontlightIndex() {
  const auto options = shortPowerButtonOptions();
  for (size_t i = 0; i < options.size(); i++) {
    if (options[i].value == CrossPointSettings::TOGGLE_FRONTLIGHT) return static_cast<int>(i);
  }
  return -1;
}

inline SettingInfo buildShortPowerButtonSetting() {
  return SettingInfo::DynamicEnum(StrId::STR_SHORT_PWR_BTN, buildShortPowerButtonValues(), shortPowerButtonDisplayValue,
                                  setShortPowerButtonFromDisplayValue, "shortPwrBtn", StrId::STR_CAT_CONTROLS);
}

inline std::vector<StrId> homeThemeValues() {
  static constexpr StrId VALUES[] = {StrId::STR_THEME_CLASSIC, StrId::STR_THEME_LYRA, StrId::STR_THEME_LYRA_EXTENDED,
                                     StrId::STR_THEME_ROUNDEDRAFF, StrId::STR_THEME_COVER_GRID};
  const size_t count = UITheme::supportsCoverGrid() ? std::size(VALUES) : std::size(VALUES) - 1;
  return {VALUES, VALUES + count};
}

using SettingVisitor = void (*)(void* context, SettingInfo& info);

// Visits synchronously in list order. Borrowed references are valid only during
// the callback; move from the descriptor to retain ownership. Its board conditions
// must be fixed before settings load, or load and save would see different keys.
void forEachBaseSetting(SettingVisitor visit, void* context);

template <typename Fn>
void forEachBaseSetting(Fn& fn) {
  forEachBaseSetting([](void* context, SettingInfo& info) { (*static_cast<Fn*>(context))(info); }, &fn);
}

// Collects the same descriptors into an owning list for UI and web callers.
std::vector<SettingInfo> getBaseSettingsList(size_t extraCapacity = 0);

// UI callers own the list for runtime filtering and font/dictionary options.
inline std::vector<SettingInfo> getSettingsList(const SdCardFontRegistry* registry = nullptr,
                                                const std::vector<DictionaryEntry>* dictionaries = nullptr) {
  auto v = getBaseSettingsList(dictionaries && !dictionaries->empty() ? 1 : 0);
  // Frontlit and lightless variants of a board share one binary (EEGO A4);
  // presence is the I2C probe result from Frontlight.begin(). UI-only: the probe
  // has not run at load time, so the base list must keep the persisted key.
  if (!Frontlight.present()) {
    v.erase(std::remove_if(v.begin(), v.end(),
                           [](const SettingInfo& s) { return s.nameId == StrId::STR_RESTORE_LIGHT_ON_WAKE; }),
            v.end());
  }
  // Tilt page turn needs an IMU with a gyroscope. UI-only, like the frontlight
  // entry: a failed probe on one boot must not drop the saved value.
  if (!halTiltSensor.isAvailable()) {
    v.erase(
        std::remove_if(v.begin(), v.end(), [](const SettingInfo& s) { return s.nameId == StrId::STR_TILT_PAGE_TURN; }),
        v.end());
  }
  if (registry && registry->getFamilyCount() > 0) {
    auto it = std::find_if(v.begin(), v.end(), [](const SettingInfo& s) { return s.nameId == StrId::STR_FONT_FAMILY; });
    if (it != v.end()) {
      *it = buildFontFamilySetting(registry);
    }
  }
  {
    // Unconditional: even with no SD fonts installed the sizes come from the
    // built-in family rather than a fixed Small/Medium/Large/XL enum.
    auto it = std::find_if(v.begin(), v.end(), [](const SettingInfo& s) { return s.nameId == StrId::STR_FONT_SIZE; });
    if (it != v.end()) {
      *it = buildFontSizeSetting(registry);
    }
  }
  if (dictionaries && !dictionaries->empty()) {
    // Insert at the end of the Reader category (just before the first Controls entry).
    auto it =
        std::find_if(v.begin(), v.end(), [](const SettingInfo& s) { return s.category == StrId::STR_CAT_CONTROLS; });
    v.insert(it, buildDictionarySetting(*dictionaries));
  }
  return v;
}

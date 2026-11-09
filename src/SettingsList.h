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
#include "ReaderLineSpacing.h"
#include "activities/settings/SettingsActivity.h"
#include "components/UITheme.h"
#include "util/DictionaryRegistry.h"
#include "util/SlideSwitch.h"

// Build the font family setting dynamically. When registry is non-null, SD card fonts
// are appended after the built-in fonts. Otherwise only built-in fonts are listed.
inline SettingInfo buildFontFamilySetting(const SdCardFontRegistry* registry) {
  // The render code checks enumStringValues first, then enumValues, so with SD
  // fonts present every option (built-in first) is listed as a string.
  std::vector<std::string> allStringValues;
  if (registry && registry->getFamilyCount() > 0) {
    const auto& families = registry->getFamilies();
    allStringValues.reserve(CrossPointSettings::BUILTIN_FONT_COUNT + families.size());
    allStringValues.push_back(I18N.get(StrId::STR_NOTO_SANS));
    std::transform(families.begin(), families.end(), std::back_inserter(allStringValues),
                   [](const SdCardFontFamilyInfo& f) { return f.name; });
  }

  SettingInfo s;
  s.nameId = StrId::STR_FONT_FAMILY;
  s.type = SettingType::ENUM;
  s.enumValues = {StrId::STR_NOTO_SANS};
  s.enumStringValues = std::move(allStringValues);
  s.key = "fontFamily";
  s.category = StrId::STR_CAT_READER;
  s.inTextSettings = true;  // matches the static font-family entry it replaces

  // The lambdas read the registry instead of copying the names: it is only
  // rescanned while no list built from it is in use, so the indexes match.
  s.valueGetter = [registry]() -> uint8_t {
    // If an SD card font is selected, find its index
    if (registry && SETTINGS.sdFontFamilyName[0] != '\0') {
      const auto& families = registry->getFamilies();
      for (size_t i = 0; i < families.size(); i++) {
        if (families[i].name == SETTINGS.sdFontFamilyName) {
          return static_cast<uint8_t>(CrossPointSettings::BUILTIN_FONT_COUNT + i);
        }
      }
      // SD font name not found in registry — fall through to built-in
    }
    return SETTINGS.fontFamily < CrossPointSettings::BUILTIN_FONT_COUNT ? SETTINGS.fontFamily : 0;
  };

  s.valueSetter = [registry](uint8_t v) {
    if (v < CrossPointSettings::BUILTIN_FONT_COUNT) {
      SETTINGS.fontFamily = v;
      SETTINGS.sdFontFamilyName[0] = '\0';
    } else if (registry) {
      const size_t sdIdx = v - CrossPointSettings::BUILTIN_FONT_COUNT;
      const auto& families = registry->getFamilies();
      if (sdIdx < families.size()) {
        strncpy(SETTINGS.sdFontFamilyName, families[sdIdx].name.c_str(), sizeof(SETTINGS.sdFontFamilyName) - 1);
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
  // Captured by copy so the indexes keep matching the option labels built here,
  // even if the family changes later.
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

inline constexpr char DICTIONARY_SETTING_KEY[] = "dictionaryName";

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
  s.key = DICTIONARY_SETTING_KEY;  // web settings API; persisted by name, not by the generic loop
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

struct ButtonSettingOption {
  StrId label;
  uint8_t value;
};

struct ButtonSettingOptions {
  ButtonSettingOption entries[std::max(static_cast<size_t>(CrossPointSettings::LONG_PRESS_MENU_FUNCTION_COUNT),
                                       static_cast<size_t>(CrossPointSettings::SHORT_PWRBTN_COUNT))];
  uint8_t count;
};

inline int buttonSettingIndex(const ButtonSettingOptions& options, const uint8_t value) {
  for (uint8_t i = 0; i < options.count; ++i) {
    if (options.entries[i].value == value) return i;
  }
  return -1;
}

// The long-press Confirm values this board offers, in list order: upstream's (Reader Menu
// only with a Home key, then Save Clipping and Footnotes) and, after them, Silkscreen's
// front-light panel, only with a light and no Home key (a Home-key board reaches the light
// through its Home actions). The setting maps its list positions to these values, and the
// value is what is saved, so a value is never renumbered: upstream's keep theirs (Save
// Clipping 5, Footnotes 6).
inline ButtonSettingOptions longPressMenuValues() {
  ButtonSettingOptions v = {{{StrId::STR_KOSYNC, CrossPointSettings::LP_MENU_KOSYNC},
                             {StrId::STR_DISABLED, CrossPointSettings::LP_MENU_DISABLED},
                             {StrId::STR_BOOKMARK_OPTION, CrossPointSettings::LP_MENU_BOOKMARK},
                             {StrId::STR_DICTIONARY, CrossPointSettings::LP_MENU_DICTIONARY}},
                            4};
  if (BoardConfig::hasHomeKey()) {
    v.entries[v.count++] = {StrId::STR_READER_MENU, CrossPointSettings::LP_MENU_READER_MENU};
  }
  v.entries[v.count++] = {StrId::STR_SAVE_CLIPPING, CrossPointSettings::LP_MENU_CREATE_CLIPPING};
  v.entries[v.count++] = {StrId::STR_FOOTNOTES, CrossPointSettings::LP_MENU_FOOTNOTES};
  if (!BoardConfig::hasHomeKey() && keyFrontlightAvailable()) {
    v.entries[v.count++] = {StrId::STR_FRONTLIGHT, CrossPointSettings::LP_MENU_FRONTLIGHT_PANEL};
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
    labels.push_back(v.entries[i].label);
  }
  return labels;
}

// The saved value's position in this board's list; one the board does not offer shows as Disabled.
inline uint8_t longPressMenuDisplayValue() {
  const auto v = longPressMenuValues();
  const int index = buttonSettingIndex(v, SETTINGS.longPressMenuFunction);
  return static_cast<uint8_t>(index >= 0 ? index : buttonSettingIndex(v, CrossPointSettings::LP_MENU_DISABLED));
}

inline void setLongPressMenuFromDisplayValue(const uint8_t displayValue) {
  const auto v = longPressMenuValues();
  if (displayValue < v.count) SETTINGS.longPressMenuFunction = v.entries[displayValue].value;
}

inline SettingInfo buildLongPressMenuSetting() {
  return SettingInfo::DynamicEnum(StrId::STR_LONG_PRESS_MENU, buildLongPressMenuValues(), longPressMenuDisplayValue,
                                  setLongPressMenuFromDisplayValue, "longPressMenuFunction", StrId::STR_CAT_CONTROLS);
}

// Menu order differs from the persisted enum order, so map by value. Upstream's entries come
// first, in upstream's order, then Silkscreen's Toggle Frontlight (keyFrontlightAvailable()).
// Choosing it turns the power double-click off (CrossPointSettings::powerDoubleClickLight()),
// which would otherwise toggle twice.
inline ButtonSettingOptions shortPowerButtonOptions() {
  ButtonSettingOptions options = {{{StrId::STR_IGNORE, CrossPointSettings::IGNORE},
                                   {StrId::STR_SLEEP, CrossPointSettings::SLEEP},
                                   {StrId::STR_PAGE_TURN, CrossPointSettings::PAGE_TURN},
                                   {StrId::STR_FORCE_REFRESH, CrossPointSettings::FORCE_REFRESH},
                                   {StrId::STR_FOOTNOTES, CrossPointSettings::FOOTNOTES}},
                                  5};
  if (BoardConfig::hasTouch()) {
    options.entries[options.count++] = {StrId::STR_CONFIRM, CrossPointSettings::PWR_CONFIRM};
  } else {
    options.entries[options.count++] = {StrId::STR_SAVE_CLIPPING, CrossPointSettings::CREATE_CLIPPING};
  }
  // Touch boards without a home key have no button for these shortcuts. Silkscreen keeps its keys
  // with touch (hasFrontButtons(): upstream's boards answer !hasTouch(), so nothing else changes), and
  // its power double-click defers every short-click action, which these (acted on at the release in
  // MappedInputManager::update()) do not wait for.
  if (BoardConfig::hasTouch() && !BoardConfig::hasHomeKey() && !BoardConfig::hasFrontButtons()) {
    options.entries[options.count++] = {StrId::STR_BOOKMARK_OPTION, CrossPointSettings::PWR_BOOKMARK};
    options.entries[options.count++] = {StrId::STR_KOSYNC, CrossPointSettings::PWR_SYNC};
    options.entries[options.count++] = {StrId::STR_READER_MENU, CrossPointSettings::PWR_READER_MENU};
  }
  if (keyFrontlightAvailable()) {
    options.entries[options.count++] = {StrId::STR_TOGGLE_FRONTLIGHT, CrossPointSettings::TOGGLE_FRONTLIGHT};
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
  const auto options = shortPowerButtonOptions();
  std::vector<StrId> values;
  values.reserve(options.count);
  for (uint8_t i = 0; i < options.count; ++i) values.push_back(options.entries[i].label);
  return values;
}

// The saved value's position in this board's list; one the board does not offer shows as Ignore.
inline uint8_t shortPowerButtonDisplayValue() {
  const auto options = shortPowerButtonOptions();
  const int index = buttonSettingIndex(options, SETTINGS.shortPwrBtn);
  return index >= 0 ? static_cast<uint8_t>(index) : CrossPointSettings::IGNORE;
}

inline void setShortPowerButtonFromDisplayValue(const uint8_t displayValue) {
  const auto options = shortPowerButtonOptions();
  SETTINGS.shortPwrBtn =
      displayValue < options.count ? options.entries[displayValue].value : CrossPointSettings::IGNORE;
}

// Where Toggle Frontlight sits in this board's list, or -1 where it is not offered. The web
// settings page hides the power double-click row while it is chosen, and its position moves
// with the touch panel (Confirm is listed only with one).
inline int shortPowerButtonToggleFrontlightIndex() {
  const auto options = shortPowerButtonOptions();
  return buttonSettingIndex(options, CrossPointSettings::TOGGLE_FRONTLIGHT);
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

// Number of entries forEachBaseSetting() visits.
size_t countBaseSettings();

// True if this board hides the setting at runtime. UI-only: the probes have not
// run at load time, so the base list keeps these keys persisted.
inline bool isHiddenOnDevice(const SettingInfo& s) {
#if FREEINK_DEVICE_SILKSCREEN && !FREEINK_SILKSCREEN_368
  // Keep this key persisted before the panel probe; filter only runtime lists.
  if (s.valuePtr == &CrossPointSettings::standardHalfRefresh &&
      BoardConfig::ACTIVE.displayController != BoardConfig::DisplayController::SSD1677) {
    return true;
  }
#endif
  switch (s.nameId) {
    // Frontlit and lightless variants of a board share one binary (EEGO A4);
    // presence is the I2C probe result from Frontlight.begin(). The slide
    // switch's position owns its bound function (the light at boot too).
    case StrId::STR_RESTORE_LIGHT_ON_WAKE:
      return !Frontlight.present() || slideSwitchOwnsLight();
    case StrId::STR_NIGHT_MODE:
      return slideSwitchOwns(CrossPointSettings::SLIDE_SWITCH_NIGHT_MODE);
    case StrId::STR_TOUCH_READER_CONTROLS:
      return slideSwitchOwns(CrossPointSettings::SLIDE_SWITCH_TOUCH_CONTROLS);
    // Tilt page turn needs an IMU with a gyroscope. A failed probe on one boot
    // must not drop the saved value.
    case StrId::STR_TILT_PAGE_TURN:
      return !halTiltSensor.isAvailable();
    // Clock settings need the RTC, as on the device (ClockSettingsActivity).
    case StrId::STR_CLOCK:
    case StrId::STR_CLOCK_FORMAT:
    case StrId::STR_TIMEZONE:
    case StrId::STR_CLOCK_DST:
    case StrId::STR_CLOCK_IN_HEADER:
    case StrId::STR_CLOCK_SYNCED:
      return !halClock.isAvailable();
    // Touch boards without front keys use Home or short-power actions for
    // these shortcuts. Keys beside touch keep their long-press menu.
    case StrId::STR_LONG_PRESS_MENU:
      return BoardConfig::hasTouch() && !BoardConfig::hasFrontButtons();
    default:
      return false;
  }
}

// Replaces the font family and size entries with options built from the fonts
// present; other entries are left unchanged.
inline void buildFontOptions(SettingInfo& s, const SdCardFontRegistry* registry) {
  if (s.nameId == StrId::STR_FONT_FAMILY && registry && registry->getFamilyCount() > 0) {
    s = buildFontFamilySetting(registry);
  } else if (s.nameId == StrId::STR_FONT_SIZE) {
    // Unconditional: even with no SD fonts installed the sizes come from the
    // built-in family rather than a fixed Small/Medium/Large/XL enum.
    s = buildFontSizeSetting(registry);
  }
}

// Visits the settings shown on this device in list order, one at a time, for
// callers that need not hold the whole list in memory. Entries whose key
// wants(key) rejects are skipped before their options are built; wants gets
// nullptr for entries without a key (actions). The visited entry is valid only
// during the callback; move from it to keep it.
template <typename Wants, typename Visit>
void forEachSetting(const SdCardFontRegistry* registry, const std::vector<DictionaryEntry>* dictionaries, Wants wants,
                    Visit visit) {
  bool dictionaryPending = dictionaries && !dictionaries->empty();
  const auto visitDictionary = [&] {
    dictionaryPending = false;
    if (!wants(DICTIONARY_SETTING_KEY)) return;
    SettingInfo s = buildDictionarySetting(*dictionaries);
    visit(s);
  };
  auto each = [&](SettingInfo& s) {
    if (isHiddenOnDevice(s)) return;
    if (s.valuePtr == &CrossPointSettings::vocabularyHistoryEnabled) {
      if (!dictionaryPending) return;
      visitDictionary();
    }
    if (dictionaryPending && s.category == StrId::STR_CAT_CONTROLS) visitDictionary();
    if (!wants(s.key)) return;
    buildFontOptions(s, registry);
    visit(s);
  };
  forEachBaseSetting(each);
  if (dictionaryPending) visitDictionary();
}

// UI callers own the list for runtime filtering and font/dictionary options.
inline std::vector<SettingInfo> getSettingsList(const SdCardFontRegistry* registry = nullptr,
                                                const std::vector<DictionaryEntry>* dictionaries = nullptr) {
  std::vector<SettingInfo> v;
  // Room for every base entry and the dictionary, so the list never reallocates.
  v.reserve(countBaseSettings() + (dictionaries && !dictionaries->empty() ? 1 : 0));
  forEachSetting(
      registry, dictionaries, [](const char*) { return true; }, [&v](SettingInfo& s) { v.push_back(std::move(s)); });
  return v;
}

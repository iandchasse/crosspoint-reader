#!/usr/bin/env bash
# Deep-sleep guard (docs/silkscreen/PORTING.md, battery-drain rules ZD-01/ZD-02):
# - the only deep-sleep entry is freeink::PowerManager::deepSleep(), reached from
#   HalPowerManager on the Arduino loop task;
# - the only wake source is the power button, armed by PowerManager (ext1 on
#   Xtensa, the GPIO wake on RISC-V);
# - no timer, ULP, touch-pad, ext0, UART or radio wake, no light sleep, no RTC
#   power-domain override, no ultra-low sleep mode. The one exception is upstream's
#   RTC_PERIPH override in PowerManager::armWakeOnPins(), allowed only behind the
#   InputPins::powerExternalPull exemption that keeps Silkscreen's IO18 out of it.
# Comment lines are ignored. Exits non-zero and lists the offending lines.
set -u
cd "$(dirname "$0")/.." || exit 2

roots=(src lib freeink-sdk/libs)
power_manager="freeink-sdk/libs/hardware/PowerManager/src/PowerManager.cpp"

code_matches() {
  grep -rnE --include='*.c' --include='*.cpp' --include='*.h' --include='*.hpp' "$1" "${roots[@]}" |
    grep -v '/test/' |
    grep -vE '^[^:]+:[0-9]+:[[:space:]]*(//|\*|/\*)'
}

status=0

forbidden='esp_sleep_enable_timer_wakeup|esp_deep_sleep\(|ESP\.deepSleep|esp_sleep_enable_ulp_wakeup|ulp_run|ulp_riscv_run|esp_sleep_enable_touchpad_wakeup|touchSleepWakeUpEnable|esp_sleep_enable_ext0_wakeup|esp_sleep_enable_gpio_wakeup|esp_sleep_enable_uart_wakeup|esp_sleep_enable_wifi_wakeup|esp_sleep_enable_bt_wakeup|esp_light_sleep_start|rtc_sleep_enable_ultra_low|ESP_SLEEP_ULTRA_LOW_MODE'
if hits=$(code_matches "$forbidden"); then
  echo "Forbidden sleep or wake API (only the power button may wake the device):"
  echo "$hits"
  status=1
fi

# esp_sleep_pd_config: the only call allowed is upstream's, for ESP_PD_DOMAIN_RTC_PERIPH in
# PowerManager::armWakeOnPins() (it keeps the RTC pull it sets on each wake pad powered). On Silkscreen the
# power pin is biased by the board (IO18: R62 and R76), where an RTC pull is forbidden (POWER.md section 2) and
# the power domain stays at the IDF default (ZD-01), so the call must sit behind the powerExternalPull
# exemption: in the same function, a statement naming powerExternalPull and after it a line that returns on
# the wake mask (`... gpioMask ... return;`), both ahead of the call. Anywhere else, or with another
# argument, it is an RTC power-domain override.
if hits=$(code_matches 'esp_sleep_pd_config' | grep -v "^${power_manager}:"); then
  echo "esp_sleep_pd_config() outside ${power_manager}:"
  echo "$hits"
  status=1
fi
if ! awk '
  /^void PowerManager::armWakeOnPins/ { fn = 1 }
  fn && /^}/ { fn = 0 }
  fn && /powerExternalPull/ && $0 !~ /^[[:space:]]*(\/\/|\*|\/\*)/ { named = 1 }
  fn && named && /gpioMask/ && /return[[:space:]]*;/ && $0 !~ /^[[:space:]]*(\/\/|\*|\/\*)/ { exempt = 1 }
  /esp_sleep_pd_config/ && $0 !~ /^[[:space:]]*(\/\/|\*|\/\*)/ {
    calls++
    if (!fn) outside++
    else if (!exempt) unguarded++
    if ($0 !~ /esp_sleep_pd_config\(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON\)/) wrong++
  }
  END { exit !(calls <= 1 && outside == 0 && unguarded == 0 && wrong == 0) }
' "$power_manager"; then
  echo "esp_sleep_pd_config() in ${power_manager} must be the single ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON"
  echo "call in PowerManager::armWakeOnPins(), behind the InputPins::powerExternalPull exemption and its early return."
  status=1
fi

for api in 'esp_deep_sleep_start' 'esp_sleep_enable_ext1_wakeup' 'esp_deep_sleep_enable_gpio_wakeup'; do
  if hits=$(code_matches "${api}[[:space:]]*\(" | grep -v "^${power_manager}:"); then
    echo "${api}() outside ${power_manager}:"
    echo "$hits"
    status=1
  fi
done

if [ "$status" -eq 0 ]; then
  echo "Sleep and wake sources OK: PowerManager is the only deep-sleep entry and wake-source owner."
fi
exit "$status"

#!/usr/bin/env bash
# Deep-sleep guard (docs/silkscreen/PORTING.md, battery-drain rules ZD-01/ZD-02):
# - the only deep-sleep entry is freeink::PowerManager::deepSleep(), reached from
#   HalPowerManager on the Arduino loop task;
# - the only wake source is the power button, armed by PowerManager (ext1 on
#   Xtensa, the GPIO wake on RISC-V);
# - no timer, ULP, touch-pad, ext0, UART or radio wake, no light sleep, no RTC
#   power-domain override, no ultra-low sleep mode.
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

forbidden='esp_sleep_enable_timer_wakeup|esp_deep_sleep\(|ESP\.deepSleep|esp_sleep_enable_ulp_wakeup|ulp_run|ulp_riscv_run|esp_sleep_enable_touchpad_wakeup|touchSleepWakeUpEnable|esp_sleep_enable_ext0_wakeup|esp_sleep_enable_gpio_wakeup|esp_sleep_enable_uart_wakeup|esp_sleep_enable_wifi_wakeup|esp_sleep_enable_bt_wakeup|esp_light_sleep_start|esp_sleep_pd_config|rtc_sleep_enable_ultra_low|ESP_SLEEP_ULTRA_LOW_MODE'
if hits=$(code_matches "$forbidden"); then
  echo "Forbidden sleep or wake API (only the power button may wake the device):"
  echo "$hits"
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

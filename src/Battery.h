#pragma once
#include <BatteryMonitor.h>

#define BAT_GPIO1 1  // Battery voltage

static BatteryMonitor battery(BAT_GPIO1);

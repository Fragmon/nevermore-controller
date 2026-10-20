#pragma once

#include <chrono>

namespace nevermore {

using namespace std::literals::chrono_literals;

// period between setting changes getting persisted
// special value: 0 to disable
constexpr auto SETTINGS_PERSIST_PERIOD = 10s;

// period between sensor polls (in sec)
// SGP40's VOC Index algo is calibrated for 1s and 10s update rates,
// and has a minimum update rate of 0.5s.
constexpr auto SENSOR_UPDATE_PERIOD = 1s;
static_assert(0.5s <= SENSOR_UPDATE_PERIOD,
        "SENSOR_UPDATE_PERIOD too low, SGP40 needs at least 0.5s between measures.");

constexpr auto ADVERTISE_INTERVAL_MIN = 300ms;
constexpr auto ADVERTISE_INTERVAL_MAX = 500ms;

// Set to desired baud rate. Most sensors support 400 kbit/s.
// Compile time error checks will trigger if set too high for included sensors.
constexpr uint32_t I2C_BAUD_RATE_SENSOR_MAX = 400 * 1000;

// basically `PICO_STDIO_USB_CONNECT_WAIT_TIMEOUT_MS`, but only applies
// when we know for certain that we're not USB powered.
constexpr auto STDIO_USB_CONNECT_TIMEOUT = 1s;

// Hide the intake labels & plot series on the display.
// Useful for builds with only a single (exhaust) sensor board.
constexpr bool DISPLAY_HIDE_INTAKE = true;

// Occasionally show a random quote overlay on the display at startup.
constexpr bool DISPLAY_STARTUP_QUOTES = false;

// Use the modern single-reading display layout (`src/ui/modern`) instead of the
// layout selected by the `display_ui` setting.
constexpr bool DISPLAY_UI_MODERN = true;

// How long the modern layout shows the logo splash at startup. 0 to disable.
constexpr auto DISPLAY_SPLASH_DURATION = 2s;

}  // namespace nevermore

#pragma once

#include "ui/common.h"

namespace nevermore::ui::modern {

constexpr uint32_t COLOUR_BACKGROUND = 0x0B1416;
constexpr uint32_t COLOUR_ACCENT = 0x3FB8BD;    // teal, matches the logo backdrop
constexpr uint32_t COLOUR_OVERRIDE = 0xF0B93A;  // fan power manually overridden

// Builds the screen (and an optional logo splash, see `DISPLAY_SPLASH_DURATION`).
// The returned UI only exposes the fan arc & touch overlay; all labels and the
// history plot are owned & refreshed by this module.
NevermoreDisplayUI create();

}  // namespace nevermore::ui::modern

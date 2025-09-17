#pragma once

#include "../../sdk/reaper_atmos.h"

// Initializes built-in Atmos speaker formats and resets routing state.
void AtmosEngine_Initialize();

// Releases any cached routing state. Built-in formats are rebuilt on the
// next initialization.
void AtmosEngine_Shutdown();

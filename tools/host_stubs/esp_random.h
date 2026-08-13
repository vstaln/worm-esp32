// Host stubs for ESP-IDF headers used by the worm firmware, so the sim can be
// verified with plain gcc before flashing. Real values are not needed for
// behavioural verification.
#pragma once
#include <stdlib.h>
static inline uint32_t esp_random(void) { return (uint32_t)rand(); }

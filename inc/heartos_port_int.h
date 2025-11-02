#pragma once
#include <stdint.h>
#include "heartos.h"   // for hrt_tick_source_t

/* Core-private accessors for port code. Do NOT install this header. */
uint32_t          hrt__cfg_core_hz(void);
hrt_tick_source_t hrt__cfg_tick_src(void);
uint32_t          hrt__cfg_tick_hz(void);

#pragma once
#include <stdint.h>
#include "hardrt.h"   // for hrt_tick_source_t

#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
    /* Core-private accessors for port code. Do NOT install this header. */
    uint32_t          hrt__cfg_core_hz(void);
    hrt_tick_source_t hrt__cfg_tick_src(void);
    uint32_t          hrt__cfg_tick_hz(void);

    void hrt__save_current_sp(uint32_t sp);   // store PSP into current TCB if current>=0
    uint32_t hrt__load_next_sp_and_set_current(int next_id); // set current=next, return next->sp
    int  hrt__get_current(void);
    int  hrt__pick_next_ready(void);
    uint32_t hrt__schedule(const uint32_t old_sp);


    /**
     * @brief function to identify the stack pointer validity.
     *
     * @param sp  stack pointer
     */
    void hrt_port_sp_valid(uint32_t sp);
#ifdef __cplusplus
}
#endif
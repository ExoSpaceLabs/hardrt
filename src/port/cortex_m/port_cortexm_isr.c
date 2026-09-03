/* SPDX-License-Identifier: Apache-2.0 */
#include <stdint.h>
#include <stddef.h>
#include "hardrt.h"
#include "hardrt_port_contract.h"

#ifndef HARDRT_DEBUG
#define HARDRT_DEBUG 0
#endif

#if HARDRT_DEBUG == 1
volatile uint8_t dbg_tasks_returned = 0;
volatile uint8_t dbg_pend_from_tramp = 0;
#endif

void PendSV_Handler(void);

void hrt__task_trampoline(void) {
    const int id = hrt__get_current();
#if HARDRT_DEBUG == 1
    if (id < 0) hrt_error(ERR_INVALID_ID);
#endif

    _hrt_tcb_t *t = hrt__tcb(id);
#if HARDRT_DEBUG == 1
    if (t == NULL) hrt_error(ERR_TCB_NULL);
#endif

    t->entry(t->arg);
    hrt_task_delete();
}

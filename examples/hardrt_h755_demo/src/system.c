// vendor/stm32/Templates/sysmem.c
// Minimal heap provider for newlib on GNU ld. Works with STM32 H7 linker scripts.
// SPDX-License-Identifier: Apache-2.0

#include <stdint.h>
#include <stddef.h>

// Common symbols exported by GCC STM32 linker scripts.
// Prefer __HeapLimit if present, else grow until stack (unguarded).
extern uint8_t _end;        // end of .bss (heap base)
extern uint8_t _estack;     // stack top (down-growing)

// Optional symbols some scripts define:
extern uint8_t __HeapBase;   // if present, start heap here
extern uint8_t __HeapLimit;  // if present, stop heap here

void* _sbrk(ptrdiff_t incr)
{
    static uint8_t *heap;
    if (!heap) {
        // Choose base
        if (&__HeapBase) heap = &__HeapBase;
        else             heap = &_end;
    }

    uint8_t *prev = heap;
    uint8_t *next = heap + incr;

    // If linker provides __HeapLimit, enforce it.
    if (&__HeapLimit && next > &__HeapLimit) {
        return (void*)-1; // out of memory
    }

    // Fallback safety: don’t cross into stack by more than 256 bytes guard.
    if ((!&__HeapLimit) && (next + 256 > &_estack)) {
        return (void*)-1;
    }

    heap = next;
    return prev;
}

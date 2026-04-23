#ifndef HARDRT_MUTEX_H
#define HARDRT_MUTEX_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "hardrt.h"

#define HRT_MUTEX_NO_OWNER (-1)

  typedef struct {
    volatile uint8_t locked;        /* 0 = unlocked, 1 = locked */
    int16_t owner;                  /* task id of owner (uint8), HRT_MUTEX_NO_OWNER if unlocked */

    uint8_t q[HARDRT_MAX_TASKS];    /* FIFO waiters */
    uint8_t head;
    uint8_t tail;
    uint8_t count_wait;
  } hrt_mutex_t;

  static inline void hrt_mutex_init(hrt_mutex_t *m) {
    m->locked = 0u;
    m->owner = HRT_MUTEX_NO_OWNER;
    m->head = 0u;
    m->tail = 0u;
    m->count_wait = 0u;
  }

  /**
   * @brief Block until the mutex is acquired.
   *
   * MUST be called from a task context (current ID >= 0).
   *
   * @param m Pointer to the mutex.
   * @return 0 on success, -1 on error (bad context or recursive lock attempt).
   */
  int hrt_mutex_lock(hrt_mutex_t *m);

  /**
   * @brief Attempt to acquire the mutex without blocking.
   *
   * MUST be called from a task context (current ID >= 0).
   *
   * @param m Pointer to the mutex.
   * @return 0 on success, -1 on failure (already locked or invalid context).
   */
  int hrt_mutex_try_lock(hrt_mutex_t *m);

  /**
   * @brief Release a mutex held by the current caller.
   *
   * MUST be called from a task context (current ID >= 0).
   *
   * @param m Pointer to the mutex.
   * @return 0 on success, -1 on failure (not locked, or not the owner).
   */
  int hrt_mutex_unlock(hrt_mutex_t *m);

#ifdef __cplusplus
}
#endif

#endif
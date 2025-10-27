#ifndef HEARTOS_CFG_H
#define HEARTOS_CFG_H

/**
 * @brief Configuration and portability macros for HeaRTOS.
 * @note You can override these with -D at compile time to adapt to your platform.
 */

/**
 * @brief Assertion macro used throughout the kernel.
 * @param x Expression that must evaluate to non-zero.
 * @note Override by defining HRT_ASSERT to integrate with your platform's assert/logging.
 */
#ifndef HRT_ASSERT
  #include <assert.h>
  #define HRT_ASSERT(x) assert(x)
#endif

#endif
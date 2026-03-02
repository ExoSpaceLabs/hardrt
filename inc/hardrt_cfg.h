#ifndef HARDRT_CFG_H
#define HARDRT_CFG_H

/**
 * @brief Configuration and portability macros for HardRT.
 * @note These can be overridden with -D at compile time to adapt to the platform.
 */

/**
 * @brief Assertion macro used throughout the kernel.
 * @param x Expression that must evaluate to non-zero.
 * @note Override by defining HRT_ASSERT to integrate with the platform's assert/logging.
 */
#ifndef HRT_ASSERT
#include <assert.h>
#define HRT_ASSERT(x) assert(x)
#endif

#endif

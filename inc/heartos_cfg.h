#ifndef HEARTOS_CFG_H
#define HEARTOS_CFG_H

/* You can override these via -D at compile time */
#ifndef HRT_ASSERT
  #include <assert.h>
  #define HRT_ASSERT(x) assert(x)
#endif

#endif
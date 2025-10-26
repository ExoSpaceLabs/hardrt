/* SPDX-License-Identifier: Apache-2.0 */
#include "heartos_version.h"
#include "heartos.h"

const char* hrt_version_string(void) { return HEARTOS_VERSION_STRING; }
unsigned    hrt_version_u32(void) {
    return (HEARTOS_VERSION_MAJOR << 16) |
           (HEARTOS_VERSION_MINOR << 8)  |
           (HEARTOS_VERSION_PATCH);
}

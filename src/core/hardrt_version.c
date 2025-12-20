/* SPDX-License-Identifier: Apache-2.0 */
#include "hardrt_version.h"
#include "hardrt.h"

const char *hrt_version_string(void) { return HARDRT_VERSION_STRING; }

unsigned hrt_version_u32(void) {
    return (HARDRT_VERSION_MAJOR << 16) |
           (HARDRT_VERSION_MINOR << 8) |
           (HARDRT_VERSION_PATCH);
}

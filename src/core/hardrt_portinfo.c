/* SPDX-License-Identifier: Apache-2.0 */
#include "hardrt_port.h"

const char *hrt_port_name(void) {
    return HARDRT_PORT_STRING;
}

int hrt_port_id(void) {
    return HARDRT_PORT_ID;
}

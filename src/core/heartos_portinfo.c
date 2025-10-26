/* SPDX-License-Identifier: Apache-2.0 */
#include "heartos_port.h"

const char* hrt_port_name(void) {
    return HEARTOS_PORT_STRING;
}
int hrt_port_id(void) {
    return HEARTOS_PORT_ID;
}

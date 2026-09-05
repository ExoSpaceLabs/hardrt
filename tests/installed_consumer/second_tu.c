#include <hardrt.h>
#include <hardrt_port.h>

/* A second installed-package translation unit deliberately includes the public
 * umbrella header. Private kernel/idle storage must not be instantiated here;
 * hardrt.h contains declarations and public static-allocation types only. */
int hardrt_installed_second_tu_probe(void) {
    return hrt_port_id() + (int)(hrt_version_u32() != 0u);
}

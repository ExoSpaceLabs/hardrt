/* Verifies version and port identity APIs report expected values on POSIX. */
#include "test_common.h"

static void test_version_and_port_identity(void) {
    const char *ver = hrt_version_string();
    unsigned v32 = hrt_version_u32();
    T_ASSERT_TRUE(ver && strlen(ver) > 0, "version string not empty");
    T_ASSERT_TRUE(v32 != 0, "version u32 non-zero");

    const char *port = hrt_port_name();
    int pid = hrt_port_id();
    T_ASSERT_STREQ("posix", port, "port name should be posix under POSIX tests");
    T_ASSERT_EQ_INT(1, pid, "port id should be 1 for posix");
}

static const test_case_t CASES[] = {
    {"Version and Port Identity", test_version_and_port_identity},
};

const test_case_t *get_tests_identity(int *out_count) {
    if (out_count) *out_count = (int) (sizeof(CASES) / sizeof(CASES[0]));
    return CASES;
}

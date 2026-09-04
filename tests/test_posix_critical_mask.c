/* SPDX-License-Identifier: Apache-2.0 */
#define _XOPEN_SOURCE 700
#include <signal.h>

#include "test_common.h"
#include "hardrt_port_contract.h"

static hrt_config_t external_cfg(void) {
    hrt_config_t cfg = {0};
    cfg.tick_hz = 1000;
    cfg.policy = HRT_SCHED_PRIORITY;
    cfg.default_slice = 0;
    cfg.tick_src = HRT_TICK_EXTERNAL;
    return cfg;
}

static int signal_blocked(const sigset_t *mask, const int signo) {
    return sigismember(mask, signo) == 1;
}

static void test_preblocked_sigalrm_survives_critical_section(void) {
    hrt__test_reset_scheduler_state();
    const hrt_config_t cfg = external_cfg();
    T_ASSERT_EQ_INT(0, hrt_init(&cfg), "init POSIX preblocked-mask test");

    sigset_t original;
    sigset_t alarm_only;
    sigset_t observed;
    sigemptyset(&alarm_only);
    sigaddset(&alarm_only, SIGALRM);
    T_ASSERT_EQ_INT(0, sigprocmask(SIG_SETMASK, NULL, &original),
                    "captured original process signal mask");
    T_ASSERT_EQ_INT(0, sigprocmask(SIG_BLOCK, &alarm_only, NULL),
                    "pre-blocked SIGALRM before HardRT critical section");

    hrt_port_crit_enter();
    hrt_port_crit_exit();

    T_ASSERT_EQ_INT(0, sigprocmask(SIG_SETMASK, NULL, &observed),
                    "queried signal mask after critical section");
    T_ASSERT_EQ_INT(1, signal_blocked(&observed, SIGALRM),
                    "outer critical exit preserves caller-preblocked SIGALRM");

    T_ASSERT_EQ_INT(0, sigprocmask(SIG_SETMASK, &original, NULL),
                    "restored original process signal mask");
}

static void test_nested_critical_section_restores_exact_outer_mask(void) {
    hrt__test_reset_scheduler_state();
    const hrt_config_t cfg = external_cfg();
    T_ASSERT_EQ_INT(0, hrt_init(&cfg), "init POSIX nested-mask test");

    sigset_t original;
    sigset_t pre;
    sigset_t observed;
    T_ASSERT_EQ_INT(0, sigprocmask(SIG_SETMASK, NULL, &original),
                    "captured original mask for nested test");

    /* Known pre-entry mask: SIGUSR1 blocked, SIGALRM and SIGUSR2 unblocked. */
    sigemptyset(&pre);
    sigaddset(&pre, SIGUSR1);
    T_ASSERT_EQ_INT(0, sigprocmask(SIG_SETMASK, &pre, NULL),
                    "installed known pre-entry signal mask");

    hrt_port_crit_enter();
    T_ASSERT_EQ_INT(0, sigprocmask(SIG_SETMASK, NULL, &observed),
                    "queried outer critical mask");
    T_ASSERT_EQ_INT(1, signal_blocked(&observed, SIGALRM),
                    "outer critical entry blocks SIGALRM");
    T_ASSERT_EQ_INT(1, signal_blocked(&observed, SIGUSR1),
                    "outer critical entry preserves preblocked SIGUSR1");
    T_ASSERT_EQ_INT(0, signal_blocked(&observed, SIGUSR2),
                    "outer critical entry leaves unrelated SIGUSR2 unblocked");

    hrt_port_crit_enter();
    hrt_port_crit_exit();
    T_ASSERT_EQ_INT(0, sigprocmask(SIG_SETMASK, NULL, &observed),
                    "queried mask after nested exit");
    T_ASSERT_EQ_INT(1, signal_blocked(&observed, SIGALRM),
                    "nested exit keeps outer SIGALRM protection active");
    T_ASSERT_EQ_INT(1, signal_blocked(&observed, SIGUSR1),
                    "nested exit does not overwrite saved outer mask");
    T_ASSERT_EQ_INT(0, signal_blocked(&observed, SIGUSR2),
                    "nested exit preserves unrelated signal state");

    hrt_port_crit_exit();
    T_ASSERT_EQ_INT(0, sigprocmask(SIG_SETMASK, NULL, &observed),
                    "queried mask after outer exit");
    T_ASSERT_EQ_INT(0, signal_blocked(&observed, SIGALRM),
                    "outer exit restores pre-entry SIGALRM unblocked state");
    T_ASSERT_EQ_INT(1, signal_blocked(&observed, SIGUSR1),
                    "outer exit restores pre-entry SIGUSR1 blocked state");
    T_ASSERT_EQ_INT(0, signal_blocked(&observed, SIGUSR2),
                    "outer exit restores pre-entry SIGUSR2 unblocked state");

    T_ASSERT_EQ_INT(0, sigprocmask(SIG_SETMASK, &original, NULL),
                    "restored original mask after nested test");
}

static const test_case_t CASES[] = {
    {"POSIX critical section preserves preblocked SIGALRM", test_preblocked_sigalrm_survives_critical_section},
    {"POSIX nested critical section restores exact outer mask", test_nested_critical_section_restores_exact_outer_mask},
};

const test_case_t *get_tests_posix_critical_mask(int *out_count) {
    if (out_count) *out_count = (int)(sizeof(CASES) / sizeof(CASES[0]));
    return CASES;
}

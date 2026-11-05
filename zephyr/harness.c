/* The Zephyr side of running a `goose --m32` program on qemu_cortex_m3:
   Zephyr's main hands over to the program's entry point (gs_app_main,
   goose_zephyr.h), and the kernel serves what newlib's time functions and
   the program's end need. The generated C is otherwise plain libc
   (GS_FREESTANDING). */
#include <zephyr/kernel.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "goose_zephyr.h"

extern int gs_app_main(int argc, char **argv);

/* Where exit() and the runtime's _Exit() end up: the status goes to QEMU
   through ARM semihosting (SYS_EXIT, or SYS_EXIT_EXTENDED for a nonzero
   one), so a test run can tell a pass from a failure; without a
   semihosting host the thread sleeps forever. */
void _exit(int status) {
    fflush(NULL);
#if defined(__ARM_ARCH) && defined(CONFIG_SEMIHOST)
    uint32_t block[2] = { 0x20026u, (uint32_t)status };
    register uint32_t r0 __asm__("r0") = (status == 0) ? 0x18u : 0x20u;
    register uint32_t r1 __asm__("r1") = (status == 0) ? 0x20026u : (uint32_t)(uintptr_t)block;
    __asm__ volatile("bkpt 0xab" : : "r"(r0), "r"(r1) : "memory");
#endif
    for (;;) {
        k_sleep(K_FOREVER);
    }
}

int main(void) {
    char *argv[] = { "goose", NULL };
    int rc = gs_app_main(1, argv);
    _exit(rc);
    return rc;
}

/* The clocks os.time() and os.clock() read, both from the kernel's cycle
   counter (there is no wall clock on the board), and the sleep. */
int clock_gettime(clockid_t clk_id, struct timespec *tp) {
    (void)clk_id;
    if (!tp) {
        errno = EINVAL;
        return -1;
    }
    uint64_t ns = k_cyc_to_ns_floor64(k_cycle_get_64());
    tp->tv_sec = (time_t)(ns / 1000000000ull);
    tp->tv_nsec = (long)(ns % 1000000000ull);
    return 0;
}

int nanosleep(const struct timespec *req, struct timespec *rem) {
    (void)rem;
    if (!req) return 0;
    int64_t ms = (int64_t)req->tv_sec * 1000 + req->tv_nsec / 1000000;
    while (ms > 0) {
        int32_t part = ms > 0x7fffffff ? 0x7fffffff : (int32_t)ms;
        k_msleep(part);
        ms -= part;
    }
    return 0;
}

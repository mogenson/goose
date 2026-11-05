/* Force-included ahead of the generated C (-include, CMakeLists.txt): what
   the program needs from this target that plain newlib does not give it.
   Newlib without Zephyr's POSIX layer declares neither clock_gettime nor
   nanosleep and names no CLOCK_MONOTONIC, though it has struct timespec
   and CLOCK_REALTIME; harness.c defines the two over the kernel. And the
   program's entry point goes by gs_app_main, since Zephyr has the main. */
#pragma once
#include <time.h>

#define GS_MAIN gs_app_main

#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC 4
#endif
int clock_gettime(clockid_t clk_id, struct timespec *tp);
int nanosleep(const struct timespec *req, struct timespec *rem);

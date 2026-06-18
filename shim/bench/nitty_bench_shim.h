#ifndef NITTY_BENCH_SHIM_H
#define NITTY_BENCH_SHIM_H

#include <stdint.h>

/* Returns high-resolution monotonic time in nanoseconds */
int64_t nitty_bench_time_now_ns(void);

/* Returns current process RSS in bytes */
int64_t nitty_bench_read_rss(void);

/* Returns current number of open file descriptors */
int64_t nitty_bench_read_fds(void);

#endif /* NITTY_BENCH_SHIM_H */

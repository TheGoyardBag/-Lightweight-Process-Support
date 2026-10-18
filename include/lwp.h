#ifndef LWP_H
#define LWP_H
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A cooperative user-level thread library, not an OS process/thread API. */
typedef uint64_t lwp_tid;
typedef void *(*lwp_fn)(void *);
#define LWP_NO_THREAD ((lwp_tid)0)
#define LWP_DEFAULT_STACK ((size_t)256 * 1024)
#define LWP_MIN_STACK ((size_t)64 * 1024)
#define LWP_MAX_STACK ((size_t)16 * 1024 * 1024)

/* All fallible calls return 0 on success and -1 with errno on failure. */
int lwp_create(lwp_tid *out, lwp_fn function, void *argument, size_t stack_size);
int lwp_run(void);                 /* Run the FIFO scheduler from the main context. */
int lwp_yield(void);               /* Voluntarily let another ready thread run. */
int lwp_join(lwp_tid id, void **result); /* One joiner; zombie handles are consumed. */
#if defined(__cplusplus)
[[noreturn]] void lwp_exit(void *result);
#else
_Noreturn void lwp_exit(void *result);
#endif
lwp_tid lwp_self(void);            /* Zero outside an LWP. */
size_t lwp_live_count(void);       /* Excludes exited, unjoined threads. */
int lwp_destroy(void);             /* From main only; release all remaining handles. */

#ifdef __cplusplus
}
#endif
#endif

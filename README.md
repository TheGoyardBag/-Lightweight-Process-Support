# Lightweight Process Support

**A small cooperative user-level thread library for Linux, implemented in C.**

The library gives each task a separate stack and saved execution context, then runs
ready tasks in FIFO round-robin order when they voluntarily yield. It supports creation,
yielding, return values, blocking joins, explicit exit, and guarded stack allocation.

This is a learning-oriented runtime, **not** a replacement for POSIX threads and not a
preemptive or multi-core scheduler. Context save/restore uses libc `ucontext`; the scheduler,
thread lifecycle, wait handling, guard mappings, and cleanup are implemented here.

## Build and run

Requires **Linux with glibc**, a C11 compiler, and Make. Windows users can use WSL2.
On macOS, use a Linux VM or the included Dockerfile; native macOS is not supported.

```bash
make test
make demo
make benchmark
make ubsan
```

A clean compiler-specific run is `make clean && make test CC=clang`.

```bash
docker build -t lwp-lab .
docker run --rm lwp-lab
```

The tests intentionally fault in child processes to verify both guard pages. Core dumps
are disabled for those child processes. A successful test suite is not proof of general
memory safety; guard pages are a mitigation for sequential stack over/underflow.

## Example

```c
#include "lwp.h"
#include <stdio.h>

static void *task(void *message) {
    puts((char *)message);
    lwp_yield();
    puts("resumed");
    return message;
}

int main(void) {
    lwp_tid id;
    if (lwp_create(&id, task, "hello", 0) || lwp_run()) return 1;
    void *result;
    if (lwp_join(id, &result)) return 1;
    return lwp_destroy() != 0;
}
```

Link with `cc -Iinclude example.c build/liblwp.a -o example` after `make`.

## Runtime design

```text
main → scheduler → READY task → RUNNING
                         ↑          │
                         └── yield ─┘
                                    ├── join → BLOCKED → target exits → READY
                                    └── return/exit → ZOMBIE → scheduler frees stack
                                                               → join frees handle
```

A thread's stack mapping is `PROT_NONE guard | readable/writable stack | PROT_NONE guard`.
The stack is page-aligned with `mmap` and `mprotect`. It is never executable. Each thread
gets a default 256 KiB usable stack (minimum 64 KiB, maximum 16 MiB). Allocation and size
arithmetic are checked. A trampoline takes **zero** `makecontext` arguments to avoid
passing pointers through the API's integer varargs.

The scheduler resumes on its own stack before unmapping an exited thread's stack. The
small zombie record remains until `lwp_join` or `lwp_destroy`, preserving the return value.
A thread may spawn another thread and join it. A single joiner is supported per target;
self-joins and circular waits are rejected. `errno` is saved/restored across cooperative
switches. Other thread-local variables are **not** isolated between LWPs.

## API behavior

| Function | Contract |
|---|---|
| `lwp_create` | Enqueue a task; size 0 selects the default stack |
| `lwp_run` | Run until no tasks are ready; may only be called from main |
| `lwp_yield` | Voluntarily return to the scheduler; LWP context only |
| `lwp_join` | Wait from an LWP or reap an exited task from main; consumes the handle |
| `lwp_exit` | Exit the current LWP; calling from main is an error and aborts |
| `lwp_self` | Current ID, or zero from main |
| `lwp_live_count` | Number of non-exited tasks |
| `lwp_destroy` | Release every remaining mapping and handle from main |

Fallible functions return 0 on success, -1 with `errno` on error. Calling main-context
`join` before the target exits returns `EBUSY`; use `lwp_run` first. A task must not return
a pointer to its stack. `destroy` may discard unstarted tasks and unjoined results; it
cannot free application-owned arguments or heap-allocated return values.

## Tests and measurements

`tests/test_lwp.c` covers FIFO order, blocking/single-owner joins, circular waits, explicit
exit, invalid input, reentrancy rejection, spawning, errno isolation, repeated creation and
cleanup, stack rounding, and both guard pages. `reports/` contains actual local validation.

The benchmark measures two tasks voluntarily yielding through the scheduler. It includes
bookkeeping and two context transfers per yield; timings are hardware/load dependent and
should not be presented as raw CPU context-switch cost or compared to OS threads without
an appropriate benchmark design.

## Deliberate limitations

- One scheduler on one kernel thread: blocking I/O blocks **all** LWPs; no parallelism.
- Non-preemptive: a task that never yields can monopolize execution.
- Not reentrant, not async-signal-safe, not safe to call concurrently from pthreads.
- No cancellation, detached threads, timer interrupts, mutexes, or I/O readiness loop.
- Guard pages do not protect arbitrary heap accesses or large jumps beyond the mapping.
- `ucontext` was removed from POSIX.1-2008; this implementation explicitly targets Linux/glibc.
- No custom assembly context switch is claimed. AddressSanitizer's `ucontext` support is
  limited; the supplied sanitizer target uses UBSan instead.

## References

- [makecontext / swapcontext](https://man7.org/linux/man-pages/man3/makecontext.3.html)
- [mmap](https://man7.org/linux/man-pages/man2/mmap.2.html)
- [mprotect](https://man7.org/linux/man-pages/man2/mprotect.2.html)

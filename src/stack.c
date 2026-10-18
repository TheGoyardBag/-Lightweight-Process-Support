#define _GNU_SOURCE
#include "stack.h"
#include "lwp.h"
#include <errno.h>
#include <stdint.h>
#include <sys/mman.h>
#include <unistd.h>

int guarded_stack_create(guarded_stack *out, size_t requested) {
    if (!out) { errno = EINVAL; return -1; }
    *out = (guarded_stack){0};
    if (!requested) requested = LWP_DEFAULT_STACK;
    if (requested < LWP_MIN_STACK || requested > LWP_MAX_STACK) {
        errno = EINVAL;
        return -1;
    }
    long raw_page = sysconf(_SC_PAGESIZE);
    if (raw_page <= 0) { errno = ENOSYS; return -1; }
    size_t page = (size_t)raw_page;
    if (requested > SIZE_MAX - (page - 1)) { errno = EOVERFLOW; return -1; }
    size_t usable = ((requested + page - 1) / page) * page;
    if (page > SIZE_MAX / 2 || usable > SIZE_MAX - 2 * page) {
        errno = EOVERFLOW;
        return -1;
    }
    size_t total = usable + 2 * page;
    void *memory = mmap(NULL, total, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (memory == MAP_FAILED) return -1;
    void *middle = (char *)memory + page;
    if (mprotect(middle, usable, PROT_READ | PROT_WRITE) == -1) {
        int saved = errno;
        munmap(memory, total);
        errno = saved;
        return -1;
    }
    *out = (guarded_stack){memory, total, middle, usable};
    return 0;
}

void guarded_stack_release(guarded_stack *stack) {
    if (stack && stack->mapping) {
        munmap(stack->mapping, stack->mapping_size);
        *stack = (guarded_stack){0};
    }
}

#define _POSIX_C_SOURCE 200809L
#include "lwp.h"
#include "stack.h"
#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

static int tests;
#define TEST(fn) do { assert(lwp_destroy() == 0); fn(); assert(lwp_destroy() == 0); \
    tests++; printf("PASS %s\n", #fn); } while (0)
static int marker;
static void *simple(void *p) { return p; }

static void test_empty_scheduler(void) {
    assert(lwp_run() == 0);
    assert(lwp_self() == 0);
    assert(lwp_live_count() == 0);
}
static void test_return_and_join(void) {
    lwp_tid id;
    assert(lwp_create(&id, simple, &marker, 0) == 0);
    assert(lwp_live_count() == 1);
    assert(lwp_join(id, NULL) == -1 && errno == EBUSY);
    assert(lwp_run() == 0);
    assert(lwp_live_count() == 0);
    void *value = NULL;
    assert(lwp_join(id, &value) == 0 && value == &marker);
    assert(lwp_join(id, NULL) == -1 && errno == ESRCH);
}
static char trace[16];
static size_t trace_length;
static void *trace_worker(void *arg) {
    for (int i = 0; i < 3; i++) {
        trace[trace_length++] = *(char *)arg;
        assert(lwp_yield() == 0);
    }
    return NULL;
}
static void test_fifo_scheduling(void) {
    lwp_tid a, b;
    char ac = 'A', bc = 'B';
    trace_length = 0;
    assert(lwp_create(&a, trace_worker, &ac, 0) == 0);
    assert(lwp_create(&b, trace_worker, &bc, 0) == 0);
    assert(lwp_run() == 0);
    assert(trace_length == 6);
    for (size_t i = 0; i < trace_length; i++) assert(trace[i] == (i % 2 ? 'B' : 'A'));
}
static void *explicit_exit(void *arg) { lwp_exit(arg); }
static void test_explicit_exit(void) {
    lwp_tid id;
    assert(lwp_create(&id, explicit_exit, &marker, 0) == 0);
    assert(lwp_run() == 0);
    void *value = NULL;
    assert(lwp_join(id, &value) == 0 && value == &marker);
}
static lwp_tid target;
static int join_completed;
static void *waiter(void *unused) {
    (void)unused;
    void *value = NULL;
    assert(lwp_join(target, &value) == 0 && value == &marker);
    join_completed = 1;
    return NULL;
}
static void test_blocking_join(void) {
    lwp_tid waiting;
    join_completed = 0;
    assert(lwp_create(&waiting, waiter, NULL, 0) == 0);
    assert(lwp_create(&target, simple, &marker, 0) == 0);
    assert(lwp_run() == 0 && join_completed == 1);
    assert(lwp_join(target, NULL) == -1 && errno == ESRCH);
}
static void *self_join(void *arg) {
    (void)arg;
    assert(lwp_join(lwp_self(), NULL) == -1 && errno == EDEADLK);
    assert(lwp_run() == -1 && errno == EBUSY);
    assert(lwp_destroy() == -1 && errno == EBUSY);
    return NULL;
}
static void test_reentrancy_and_self_join(void) {
    lwp_tid id;
    assert(lwp_create(&id, self_join, NULL, 0) == 0);
    assert(lwp_run() == 0);
}
static lwp_tid cycle_a, cycle_b;
static void *cycle_first(void *arg) {
    (void)arg;
    assert(lwp_join(cycle_b, NULL) == 0);
    return NULL;
}
static void *cycle_second(void *arg) {
    (void)arg;
    assert(lwp_join(cycle_a, NULL) == -1 && errno == EDEADLK);
    return NULL;
}
static void test_join_cycle_rejection(void) {
    assert(lwp_create(&cycle_a, cycle_first, NULL, 0) == 0);
    assert(lwp_create(&cycle_b, cycle_second, NULL, 0) == 0);
    assert(lwp_run() == 0);
}
static void *errno_worker(void *arg) {
    int expected = *(int *)arg;
    for (int i = 0; i < 50; i++) {
        errno = expected;
        assert(lwp_yield() == 0);
        assert(errno == expected);
    }
    return NULL;
}
static void test_errno_preserved(void) {
    lwp_tid a, b;
    int first = EDOM, second = ERANGE;
    assert(lwp_create(&a, errno_worker, &first, 0) == 0);
    assert(lwp_create(&b, errno_worker, &second, 0) == 0);
    errno = E2BIG;
    assert(lwp_run() == 0 && errno == E2BIG);
}
static void *spawn_worker(void *arg) {
    lwp_tid child;
    void *result;
    assert(lwp_create(&child, simple, arg, 0) == 0);
    assert(lwp_join(child, &result) == 0 && result == arg);
    return result;
}
static void test_spawn_from_thread(void) {
    lwp_tid parent;
    assert(lwp_create(&parent, spawn_worker, &marker, 0) == 0);
    assert(lwp_run() == 0);
    void *result;
    assert(lwp_join(parent, &result) == 0 && result == &marker);
}
static void test_invalid_arguments(void) {
    lwp_tid id;
    assert(lwp_create(NULL, simple, NULL, 0) == -1 && errno == EINVAL);
    assert(lwp_create(&id, NULL, NULL, 0) == -1 && errno == EINVAL);
    assert(lwp_create(&id, simple, NULL, 1) == -1 && errno == EINVAL);
    assert(lwp_create(&id, simple, NULL, SIZE_MAX) == -1 && errno == EINVAL);
    assert(lwp_yield() == -1 && errno == EPERM);
    assert(lwp_join(UINT64_MAX, NULL) == -1 && errno == ESRCH);
}
static void test_repeated_lifecycle(void) {
    lwp_tid old = 0;
    for (int round = 0; round < 30; round++) {
        lwp_tid ids[30];
        for (int i = 0; i < 30; i++) assert(lwp_create(&ids[i], simple, NULL, 0) == 0);
        assert(ids[0] > old);
        old = ids[29];
        assert(lwp_run() == 0);
        for (int i = 0; i < 30; i++) assert(lwp_join(ids[i], NULL) == 0);
        assert(lwp_destroy() == 0);
    }
}
static void expect_guard_fault(int upper) {
    pid_t child = fork();
    assert(child >= 0);
    if (!child) {
        struct rlimit zero = {0, 0};
        setrlimit(RLIMIT_CORE, &zero);
        guarded_stack stack;
        if (guarded_stack_create(&stack, LWP_MIN_STACK)) _exit(2);
        volatile char *guard = upper ? (char *)stack.usable + stack.usable_size
                                    : (char *)stack.mapping;
        *guard = 1;
        _exit(3);
    }
    int status;
    assert(waitpid(child, &status, 0) == child);
    assert(WIFSIGNALED(status) && WTERMSIG(status) == SIGSEGV);
}
static void test_lower_guard_page(void) { expect_guard_fault(0); }
static void test_upper_guard_page(void) { expect_guard_fault(1); }
static void test_stack_rounding_and_release(void) {
    guarded_stack stack;
    assert(guarded_stack_create(&stack, LWP_MIN_STACK + 1) == 0);
    assert(stack.usable_size >= LWP_MIN_STACK + 1);
    ((char *)stack.usable)[0] = 1;
    ((char *)stack.usable)[stack.usable_size - 1] = 2;
    guarded_stack_release(&stack);
    assert(stack.mapping == NULL && stack.usable_size == 0);
    guarded_stack_release(&stack);
}
static void *contending_waiter(void *arg) {
    (void)arg;
    assert(lwp_join(target, NULL) == -1 && errno == EBUSY);
    return NULL;
}
static void test_single_joiner(void) {
    lwp_tid a, b;
    assert(lwp_create(&a, waiter, NULL, 0) == 0);
    assert(lwp_create(&b, contending_waiter, NULL, 0) == 0);
    assert(lwp_create(&target, simple, &marker, 0) == 0);
    assert(lwp_run() == 0);
}
int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    TEST(test_empty_scheduler);
    TEST(test_return_and_join);
    TEST(test_fifo_scheduling);
    TEST(test_explicit_exit);
    TEST(test_blocking_join);
    TEST(test_reentrancy_and_self_join);
    TEST(test_join_cycle_rejection);
    TEST(test_errno_preserved);
    TEST(test_spawn_from_thread);
    TEST(test_invalid_arguments);
    TEST(test_repeated_lifecycle);
    TEST(test_lower_guard_page);
    TEST(test_upper_guard_page);
    TEST(test_stack_rounding_and_release);
    TEST(test_single_joiner);
    printf("%d tests passed\n", tests);
    return 0;
}

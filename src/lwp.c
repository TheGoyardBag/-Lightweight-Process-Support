#define _XOPEN_SOURCE 700
#include "lwp.h"
#include "stack.h"
#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <ucontext.h>

/* Intentionally single-kernel-thread, non-reentrant, and not signal-safe. */
typedef enum { READY, RUNNING, BLOCKED, ZOMBIE } state;
typedef struct thread thread;
struct thread {
    lwp_tid id;
    ucontext_t context;
    guarded_stack stack;
    lwp_fn function;
    void *argument;
    void *result;
    state status;
    int saved_errno;
    thread *joiner;
    thread *waiting_on;
    thread *registry_next;
    thread *ready_next;
};

static struct {
    ucontext_t context;
    thread *current;
    thread *registry;
    thread *head;
    thread *tail;
    size_t live;
    lwp_tid next_id;
    bool running;
} scheduler = {.next_id = 1};

static void enqueue(thread *item) {
    item->ready_next = NULL;
    if (scheduler.tail) scheduler.tail->ready_next = item;
    else scheduler.head = item;
    scheduler.tail = item;
}

static thread *dequeue(void) {
    thread *item = scheduler.head;
    if (item) {
        scheduler.head = item->ready_next;
        if (!scheduler.head) scheduler.tail = NULL;
        item->ready_next = NULL;
    }
    return item;
}

static thread *find_thread(lwp_tid id) {
    for (thread *item = scheduler.registry; item; item = item->registry_next)
        if (item->id == id) return item;
    return NULL;
}

static void remove_thread(thread *item) {
    thread **link = &scheduler.registry;
    while (*link && *link != item) link = &(*link)->registry_next;
    if (*link) *link = item->registry_next;
    guarded_stack_release(&item->stack);
    free(item);
}

/* Never free an executing stack. Exits return to the scheduler before munmap. */
static void trampoline(void) {
    thread *self = scheduler.current;
    void *result = self->function(self->argument);
    lwp_exit(result);
}

int lwp_create(lwp_tid *out, lwp_fn function, void *argument, size_t stack_size) {
    if (!out || !function) { errno = EINVAL; return -1; }
    *out = LWP_NO_THREAD;
    if (scheduler.next_id == UINT64_MAX) { errno = EOVERFLOW; return -1; }
    thread *item = calloc(1, sizeof(*item));
    if (!item) return -1;
    if (guarded_stack_create(&item->stack, stack_size) == -1) { free(item); return -1; }
    if (getcontext(&item->context) == -1) {
        int saved = errno;
        guarded_stack_release(&item->stack);
        free(item);
        errno = saved;
        return -1;
    }
    item->id = scheduler.next_id++;
    item->function = function;
    item->argument = argument;
    item->status = READY;
    item->context.uc_stack.ss_sp = item->stack.usable;
    item->context.uc_stack.ss_size = item->stack.usable_size;
    item->context.uc_stack.ss_flags = 0;
    item->context.uc_link = &scheduler.context;
    /* No pointer-valued varargs: makecontext's integer argument portability trap is avoided. */
    makecontext(&item->context, trampoline, 0);
    item->registry_next = scheduler.registry;
    scheduler.registry = item;
    enqueue(item);
    scheduler.live++;
    *out = item->id;
    return 0;
}

int lwp_run(void) {
    if (scheduler.running || scheduler.current) { errno = EBUSY; return -1; }
    int caller_errno = errno;
    scheduler.running = true;
    thread *item;
    while ((item = dequeue()) != NULL) {
        scheduler.current = item;
        item->status = RUNNING;
        errno = item->saved_errno;
        if (swapcontext(&scheduler.context, &item->context) == -1) {
            int saved = errno;
            item->status = READY;
            enqueue(item);
            scheduler.current = NULL;
            scheduler.running = false;
            errno = saved;
            return -1;
        }
        scheduler.current = NULL;
        if (item->status == READY) enqueue(item);
        else if (item->status == ZOMBIE) guarded_stack_release(&item->stack);
    }
    scheduler.running = false;
    if (scheduler.live != 0) { errno = EDEADLK; return -1; }
    errno = caller_errno;
    return 0;
}

int lwp_yield(void) {
    thread *self = scheduler.current;
    if (!self) { errno = EPERM; return -1; }
    self->saved_errno = errno;
    self->status = READY;
    if (swapcontext(&self->context, &scheduler.context) == -1) {
        self->status = RUNNING;
        return -1;
    }
    return 0;
}

_Noreturn void lwp_exit(void *result) {
    thread *self = scheduler.current;
    if (!self) abort(); /* API misuse: a noreturn exit has no caller to return an error to. */
    self->result = result;
    self->status = ZOMBIE;
    scheduler.live--;
    if (self->joiner && self->joiner->status == BLOCKED) {
        self->joiner->status = READY;
        enqueue(self->joiner);
    }
    if (setcontext(&scheduler.context) == -1) abort();
    abort();
}

int lwp_join(lwp_tid id, void **result) {
    thread *target = find_thread(id);
    thread *self = scheduler.current;
    if (!target) { errno = ESRCH; return -1; }
    if (target == self) { errno = EDEADLK; return -1; }
    if (target->joiner && target->joiner != self) { errno = EBUSY; return -1; }
    if (target->status != ZOMBIE) {
        if (!self) { errno = EBUSY; return -1; }
        /* Reject a circular wait before suspending. Existing wait chains are acyclic. */
        for (thread *cursor = target; cursor; cursor = cursor->waiting_on) {
            if (cursor == self) { errno = EDEADLK; return -1; }
        }
        target->joiner = self;
        self->waiting_on = target;
        self->saved_errno = errno;
        self->status = BLOCKED;
        if (swapcontext(&self->context, &scheduler.context) == -1) {
            self->status = RUNNING;
            self->waiting_on = NULL;
            target->joiner = NULL;
            return -1;
        }
        self->waiting_on = NULL;
    }
    if (result) *result = target->result;
    remove_thread(target);
    return 0;
}

lwp_tid lwp_self(void) { return scheduler.current ? scheduler.current->id : LWP_NO_THREAD; }
size_t lwp_live_count(void) { return scheduler.live; }

int lwp_destroy(void) {
    if (scheduler.running || scheduler.current) { errno = EBUSY; return -1; }
    while (scheduler.registry) remove_thread(scheduler.registry);
    scheduler.head = scheduler.tail = NULL;
    scheduler.live = 0;
    /* Do not reset IDs: stale handles must never identify newly created threads. */
    return 0;
}

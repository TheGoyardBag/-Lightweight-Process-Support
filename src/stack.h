#ifndef LWP_STACK_H
#define LWP_STACK_H
#include <stddef.h>

typedef struct {
    void *mapping;
    size_t mapping_size;
    void *usable;
    size_t usable_size;
} guarded_stack;

int guarded_stack_create(guarded_stack *out, size_t requested);
void guarded_stack_release(guarded_stack *stack);
#endif

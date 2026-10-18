#include "lwp.h"
#include <stdio.h>
#include <stdlib.h>

static void *worker(void *name) {
    for (int turn = 0; turn < 3; turn++) {
        printf("thread %llu (%s): turn %d\n", (unsigned long long)lwp_self(),
               (char *)name, turn);
        if (lwp_yield() == -1) { perror("lwp_yield"); exit(1); }
    }
    return name;
}

int main(void) {
    lwp_tid a, b;
    if (lwp_create(&a, worker, "alpha", 0) || lwp_create(&b, worker, "beta", 0)
        || lwp_run()) { perror("lwp"); return 1; }
    void *result;
    if (lwp_join(a, &result)) return 1;
    printf("joined alpha: %s\n", (char *)result);
    if (lwp_join(b, NULL) || lwp_destroy()) return 1;
    return 0;
}

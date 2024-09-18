#ifndef NUTEST
#define NUTEST

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    const char* msg;
    const char* func;
    int line;
    int pass;
} nutest_result;

#define NUTEST_PASS (nutest_result){.pass = 1, .msg = NULL, .func = __func__, .line = __LINE__};

#define NUTEST_ASSERT(cond)                                               \
    if (!(cond)) {                                                        \
        return (nutest_result){.pass = 0, .msg = NULL, .func = __func__, .line = __LINE__}; \
    }

typedef nutest_result (*nutest_func)(void);

typedef struct {
    nutest_func* data;
    size_t len;
    size_t cap;
} nutest_vec;

void nutest_vec_new(nutest_vec* nv) {
    size_t default_cap = 32;
    nv->data = (nutest_func*)malloc(default_cap * sizeof(nutest_func));
    assert(nv->data);
    nv->cap = default_cap;
    nv->len = 0;
    return;
}

void nutest_vec_free(nutest_vec* nv) {
    free(nv->data);
    return;
}

void nutest_vec_push(nutest_vec* nv, nutest_func f) {
    if (nv->cap == nv->len) {
        nv->cap = nv->cap * 3 / 2;
        nv->data = (nutest_func*)realloc(nv->data, sizeof(nutest_func) * nv->cap);
        assert(nv->data);
    }
    nv->data[nv->len] = f;
    nv->len++;
    return;
}

nutest_vec nutest_registry = {.data = NULL};

void nutest_register(nutest_func f) {
    if (nutest_registry.data == NULL) {
        nutest_vec_new(&nutest_registry);
    }
    nutest_vec_push(&nutest_registry, f);
    return;
}

size_t nutest_run(void) {
    size_t failed = 0;
    for (size_t i = 0; i < nutest_registry.len; i++) {
        nutest_result result = nutest_registry.data[i]();
        printf("%s: ", result.func);
        if (result.pass) {
            printf("pass\n");
        } else {
            failed++;
            printf("failed\n");
            printf("    line: %d\n", result.line);
        }
    }
    nutest_vec_free(&nutest_registry);
    return failed;
}

#endif

#ifndef CGOTPL_FUNC
#define CGOTPL_FUNC

#include <stddef.h>

#include "json.h"

typedef struct {
    json_value val;
    bool is_heap;
} tracked_value;

#define TRACKED_NULL (tracked_value){.is_heap = false, .val = JSON_NULL}

void tracked_value_free(tracked_value* val);

typedef struct {
    stream* in;
    long* args;
    size_t idx;
    size_t args_len;
    tracked_value* piped;
    void* state;
} template_arg_iter;

#define ERR_FUNC_INVALID_ARG_LEN -1000

int template_arg_iter_next(template_arg_iter* iter, tracked_value* result);
int template_arg_iter_len(template_arg_iter* iter);

bool is_empty(json_value* val);
int func_not(template_arg_iter* iter, tracked_value* out);
int func_and(template_arg_iter* iter, tracked_value* out);
int func_or(template_arg_iter* iter, tracked_value* out);

#endif

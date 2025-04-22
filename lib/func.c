#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include "json.h"

#include "func.h"

bool is_empty(json_value* val) {
    switch (val->ty) {
        case JSON_TY_NULL:
        case JSON_TY_FALSE:
            return true;
        case JSON_TY_TRUE:
            return false;
        case JSON_TY_NUMBER:
            return val->inner.num == 0.0;
        case JSON_TY_STRING:
            return strlen(val->inner.str) == 0;
        case JSON_TY_ARRAY:
            return val->inner.arr.len == 0;
        case JSON_TY_OBJECT:
            return val->inner.obj.count == 0;
    }
    assert(0);
}

int func_not(template_arg_iter* iter, tracked_value* out) {
    if (template_arg_iter_len(iter) != 1) {
        return ERR_FUNC_INVALID_ARG_LEN;
    }
    tracked_value val;
    int err = template_arg_iter_next(iter, &val);
    if (err != 0) {
        return err;
    }
    out->is_scratch = false;
    if (is_empty(&val.val)) {
        out->val.ty = JSON_TY_TRUE;
    } else {
        out->val.ty = JSON_TY_FALSE;
    }
    return 0;
}

int func_and(template_arg_iter* iter, tracked_value* out) {
    size_t args_len = template_arg_iter_len(iter);
    if (args_len == 0) {
        return ERR_FUNC_INVALID_ARG_LEN;
    }
    tracked_value current;
    for (size_t i = 0; i < args_len; i++) {
        int err = template_arg_iter_next(iter, &current);
        if (err != 0) {
            return err;
        }
        if (is_empty(&current.val)) {
            *out = current;
            return 0;
        }
    }
    *out = current;
    return 0;
}

int func_or(template_arg_iter* iter, tracked_value* out) {
    size_t args_len = template_arg_iter_len(iter);
    if (args_len == 0) {
        return ERR_FUNC_INVALID_ARG_LEN;
    }
    tracked_value current;
    for (size_t i = 0; i < args_len; i++) {
        int err = template_arg_iter_next(iter, &current);
        if (err != 0) {
            return err;
        }
        if (!is_empty(&current.val)) {
            *out = current;
            return 0;
        }
    }
    *out = current;
    return 0;
}

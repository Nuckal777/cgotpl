#include "func.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "json.h"

void tracked_value_free(tracked_value* val) {
    if (val->is_heap) {
        json_value_free(&val->val);
    }
}

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
    tracked_value arg = TRACKED_NULL;
    int err = template_arg_iter_next(iter, &arg);
    if (err != 0) {
        tracked_value_free(&arg);
        return err;
    }
    out->is_heap = false;
    if (is_empty(&arg.val)) {
        out->val.ty = JSON_TY_TRUE;
    } else {
        out->val.ty = JSON_TY_FALSE;
    }
    tracked_value_free(&arg);
    return 0;
}

int func_and(template_arg_iter* iter, tracked_value* out) {
    size_t args_len = template_arg_iter_len(iter);
    if (args_len == 0) {
        return ERR_FUNC_INVALID_ARG_LEN;
    }
    for (size_t i = 0; i < args_len; i++) {
        tracked_value current = TRACKED_NULL;
        int err = template_arg_iter_next(iter, &current);
        if (err != 0) {
            tracked_value_free(&current);
            return err;
        }
        if (i == args_len - 1 || is_empty(&current.val)) {
            *out = current;
            return 0;
        }
        tracked_value_free(&current);
    }
    return 0;
}

int func_or(template_arg_iter* iter, tracked_value* out) {
    size_t args_len = template_arg_iter_len(iter);
    if (args_len == 0) {
        return ERR_FUNC_INVALID_ARG_LEN;
    }
    for (size_t i = 0; i < args_len; i++) {
        tracked_value current = TRACKED_NULL;
        int err = template_arg_iter_next(iter, &current);
        if (err != 0) {
            tracked_value_free(&current);
            return err;
        }
        if (i == args_len - 1 || !is_empty(&current.val)) {
            *out = current;
            return 0;
        }
        tracked_value_free(&current);
    }
    return 0;
}

int func_len(template_arg_iter* iter, tracked_value* out) {
    if (template_arg_iter_len(iter) != 1) {
        return ERR_FUNC_INVALID_ARG_LEN;
    }
    tracked_value arg = TRACKED_NULL;
    int err = template_arg_iter_next(iter, &arg);
    if (err != 0) {
        tracked_value_free(&arg);
        return err;
    }
    out->is_heap = false;
    out->val.ty = JSON_TY_NUMBER;
    switch (arg.val.ty) {
        case JSON_TY_STRING:
            out->val.inner.num = strlen(arg.val.inner.str);
            break;
        case JSON_TY_ARRAY:
            out->val.inner.num = arg.val.inner.arr.len;
            break;
        case JSON_TY_OBJECT:
            out->val.inner.num = arg.val.inner.obj.count;
            break;
        default:
            err = ERR_FUNC_INVALID_ARG_TYPE;
            break;
    }
    tracked_value_free(&arg);
    return err;
}

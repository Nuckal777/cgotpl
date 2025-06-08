#include "func.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "json.h"

void tracked_value_free(tracked_value* val) {
    if (val->is_heap) {
        json_value_free(&val->val);
    }
}

#define BUF_DEFAULT_CAP 128

void buf_init(buf* b) {
    b->len = 0;
    b->cap = BUF_DEFAULT_CAP;
    b->data = malloc(b->cap);
    assert(b->data);
}

void buf_append(buf* b, const char* arr, size_t n) {
    while (b->len + n >= b->cap) {
        b->cap = b->cap * 3 / 2;
        b->data = realloc(b->data, b->cap);
        assert(b->data);
    }
    memcpy(b->data + b->len, arr, n);
    b->len += n;
}

void buf_free(buf* b) {
    b->len = 0;
    b->cap = 0;
    free(b->data);
    b->data = NULL;
}

typedef struct {
    buf* b;
    size_t count;
    size_t len;
} sprintentry_data;

void sprintentry(entry* e, void* userdata);

int sprintval(buf* b, json_value* val) {
    size_t expected;
    char print_buf[128];
    const char str_true[] = "true";
    const char str_false[] = "false";
    const char str_null[] = "<nil>";
    json_array* arr;
    hashmap* obj;
    switch (val->ty) {
        case JSON_TY_NUMBER:
            expected = snprintf(print_buf, sizeof(print_buf), "%g", val->inner.num);
            if (expected > sizeof(print_buf) - 1) {
                return ERR_BUF_OVERFLOW;
            }
            buf_append(b, print_buf, expected);
            return 0;
        case JSON_TY_STRING:
            buf_append(b, val->inner.str, strlen(val->inner.str));
            return 0;
        case JSON_TY_TRUE:
            buf_append(b, str_true, sizeof(str_true) - 1);
            return 0;
        case JSON_TY_FALSE:
            buf_append(b, str_false, sizeof(str_false) - 1);
            return 0;
        case JSON_TY_NULL:
            buf_append(b, str_null, sizeof(str_null) - 1);
            return 0;
        case JSON_TY_ARRAY:
            arr = &val->inner.arr;
            buf_append(b, "[", 1);
            for (size_t i = 0; i < arr->len; i++) {
                sprintval(b, arr->data + i);
                if (i != arr->len - 1) {
                    buf_append(b, " ", 1);
                }
            }
            buf_append(b, "]", 1);
            return 0;
        case JSON_TY_OBJECT:
            obj = &val->inner.obj;
            buf_append(b, "map[", 4);
            sprintentry_data data = {.b = b, .count = 0, .len = obj->count};
            hashmap_iter(obj, &data, sprintentry);
            buf_append(b, "]", 1);
            return 0;
    }
    assert(0);
}

void sprintentry(entry* e, void* userdata) {
    sprintentry_data* data = (sprintentry_data*)userdata;
    const char* key = e->key;
    buf_append(data->b, key, strlen(key));
    buf_append(data->b, ":", 1);
    sprintval(data->b, (json_value*)e->value);
    data->count++;
    if (data->count != data->len) {
        buf_append(data->b, " ", 1);
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

int buf_print(template_arg_iter* iter, buf* b) {
    buf_init(b);
    size_t args_len = template_arg_iter_len(iter);
    for (size_t i = 0; i < args_len; i++) {
        tracked_value arg = TRACKED_NULL;
        int err = template_arg_iter_next(iter, &arg);
        if (err != 0) {
            buf_free(b);
            return err;
        }
        err = sprintval(b, &arg.val);
        tracked_value_free(&arg);
        if (err != 0) {
            buf_free(b);
            return err;
        }
        if (i != args_len - 1) {
            buf_append(b, " ", 1);
        }
    }
    return 0;
}

int func_print(template_arg_iter* iter, tracked_value* out) {
    buf b;
    int err = buf_print(iter, &b);
    if (err != 0) {
        return err;
    }
    buf_append(&b, "", 1);
    out->is_heap = true;
    out->val.ty = JSON_TY_STRING;
    out->val.inner.str = b.data;
    return 0;
}

int func_println(template_arg_iter* iter, tracked_value* out) {
    buf b;
    int err = buf_print(iter, &b);
    if (err != 0) {
        return err;
    }
    buf_append(&b, "\n", 2);
    out->is_heap = true;
    out->val.ty = JSON_TY_STRING;
    out->val.inner.str = b.data;
    return 0;
}

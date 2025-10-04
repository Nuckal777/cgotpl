#include "func.h"

#include <assert.h>
#include <ctype.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

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
    const char* null_str;
} sprintentry_data;

void sprintentry(entry* e, void* userdata);

int sprintval(buf* b, json_value* val, const char* null_str) {
    size_t expected;
    char print_buf[128];
    const char true_str[] = "true";
    const char false_str[] = "false";
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
            buf_append(b, true_str, sizeof(true_str) - 1);
            return 0;
        case JSON_TY_FALSE:
            buf_append(b, false_str, sizeof(false_str) - 1);
            return 0;
        case JSON_TY_NULL:
            buf_append(b, null_str, strlen(null_str));
            return 0;
        case JSON_TY_ARRAY:
            arr = &val->inner.arr;
            buf_append(b, "[", 1);
            for (size_t i = 0; i < arr->len; i++) {
                sprintval(b, arr->data + i, null_str);
                if (i != arr->len - 1) {
                    buf_append(b, " ", 1);
                }
            }
            buf_append(b, "]", 1);
            return 0;
        case JSON_TY_OBJECT:
            obj = &val->inner.obj;
            buf_append(b, "map[", 4);
            sprintentry_data data = {.b = b, .count = 0, .len = obj->count, .null_str = null_str};
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
    sprintval(data->b, (json_value*)e->value, data->null_str);
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

// urlquery uses <no value> to represent null values and
// does not separate non-string values for some reason
int buf_print(template_arg_iter* iter, buf* b, const char* null_str) {
    buf_init(b);
    size_t args_len = template_arg_iter_len(iter);
    bool prev_str = true;
    bool is_nil_str = strcmp(null_str, NULL_STR_NIL) == 0;
    for (size_t i = 0; i < args_len; i++) {
        tracked_value arg = TRACKED_NULL;
        int err = template_arg_iter_next(iter, &arg);
        if (err != 0) {
            buf_free(b);
            return err;
        }
        if (is_nil_str && !prev_str && arg.val.ty != JSON_TY_STRING) {
            buf_append(b, " ", 1);
        }
        prev_str = arg.val.ty == JSON_TY_STRING;
        err = sprintval(b, &arg.val, null_str);
        tracked_value_free(&arg);
        if (err != 0) {
            buf_free(b);
            return err;
        }
    }
    return 0;
}

int func_print(template_arg_iter* iter, tracked_value* out) {
    buf b;
    int err = buf_print(iter, &b, NULL_STR_NIL);
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
    int err = buf_print(iter, &b, NULL_STR_NIL);
    if (err != 0) {
        return err;
    }
    buf_append(&b, "\n", 2);
    out->is_heap = true;
    out->val.ty = JSON_TY_STRING;
    out->val.inner.str = b.data;
    return 0;
}

int validate_index(json_value* val, size_t* out) {
    if (val->ty != JSON_TY_NUMBER) {
        return ERR_FUNC_INVALID_ARG_TYPE;
    }
    double num = val->inner.num;
    if (num < 0.0 || num > (double_t)SIZE_MAX || num != trunc(num)) {
        return ERR_FUNC_INVALID_ARG_VAL;
    }
    *out = (size_t)num;
    return 0;
}

// This function can trigger use-after-frees, if the tracked_value passed
// as first argument has `is_heap = true`. In the current implementation
// via the template it is not possible to construct a tracked_value with
// `is_heap = false` of JSON_TY_OBJECT or JSON_TY_ARRAY, so this case
// cannot occur.
int func_index(template_arg_iter* iter, tracked_value* out) {
    size_t args_len = template_arg_iter_len(iter);
    if (args_len == 0) {
        return ERR_FUNC_INVALID_ARG_LEN;
    }
    tracked_value val = TRACKED_NULL;
    int err = template_arg_iter_next(iter, &val);
    if (err != 0) {
        return err;
    }
    if (args_len == 1) {
        *out = val;
        return 0;
    }
    json_value* sub = &val.val;
    for (size_t i = 1; i < args_len; i++) {
        tracked_value arg = TRACKED_NULL;
        switch (sub->ty) {
            case JSON_TY_OBJECT:
                err = template_arg_iter_next(iter, &arg);
                if (err != 0) {
                    tracked_value_free(&val);
                    return err;
                }
                if (arg.val.ty != JSON_TY_STRING) {
                    tracked_value_free(&arg);
                    tracked_value_free(&val);
                    return ERR_FUNC_INVALID_ARG_TYPE;
                }
                int found = hashmap_get(&sub->inner.obj, arg.val.inner.str, (const void**)&sub);
                if (!found) {
                    tracked_value_free(&arg);
                    tracked_value_free(&val);
                    return ERR_FUNC_INDEX_NOT_FOUND;
                }
                tracked_value_free(&arg);
                break;
            case JSON_TY_ARRAY:
                err = template_arg_iter_next(iter, &arg);
                if (err != 0) {
                    tracked_value_free(&val);
                    return err;
                }
                size_t idx;
                err = validate_index(&arg.val, &idx);
                if (err != 0) {
                    tracked_value_free(&arg);
                    tracked_value_free(&val);
                    return err;
                }
                json_array* arr = &sub->inner.arr;
                if (idx >= arr->len) {
                    tracked_value_free(&arg);
                    tracked_value_free(&val);
                    return ERR_FUNC_INDEX_NOT_FOUND;
                }
                sub = arr->data + idx;
                tracked_value_free(&arg);
                break;
            default:
                tracked_value_free(&val);
                return ERR_FUNC_INVALID_ARG_TYPE;
        }
    }
    out->val = *sub;
    out->is_heap = false;
    tracked_value_free(&val);
    return 0;
}

// This function can trigger use-after-frees, if the tracked_value passed
// as first argument has `is_heap = true`. In the current implementation
// via the template it is not possible to construct a tracked_value with
// `is_heap = false` of JSON_TY_ARRAY, so this case cannot occur.
int func_slice(template_arg_iter* iter, tracked_value* out) {
    int err = 0;
    int args_len = template_arg_iter_len(iter);
    if (args_len < 2 || args_len > 4) {
        return ERR_FUNC_INVALID_ARG_LEN;
    }
    tracked_value target = TRACKED_NULL;
    err = template_arg_iter_next(iter, &target);
    if (err != 0) {
        return err;
    }
    if (target.val.ty != JSON_TY_ARRAY && target.val.ty != JSON_TY_STRING) {
        err = ERR_FUNC_INVALID_ARG_TYPE;
        goto cleanup_target;
    }
    tracked_value start_val = TRACKED_NULL;
    err = template_arg_iter_next(iter, &start_val);
    if (err != 0) {
        goto cleanup_target;
    }
    size_t start_idx;
    err = validate_index(&start_val.val, &start_idx);
    if (err != 0) {
        goto cleanup_start;
    }
    tracked_value end_val = TRACKED_NULL;
    size_t target_len;
    if (target.val.ty == JSON_TY_ARRAY) {
        target_len = target.val.inner.arr.len;
    } else {
        target_len = strlen(target.val.inner.str);
        if (args_len > 3) {  // strings cannot be 3-indexed
            err = ERR_FUNC_INVALID_ARG_LEN;
            goto cleanup_end;
        }
    }
    if (start_idx >= target_len) {
        err = ERR_FUNC_INVALID_ARG_VAL;
        goto cleanup_end;
    }
    size_t end_idx = target_len;
    if (args_len > 2) {
        err = template_arg_iter_next(iter, &end_val);
        if (err != 0) {
            goto cleanup_start;
        }
        size_t end;
        err = validate_index(&end_val.val, &end);
        if (err != 0) {
            goto cleanup_end;
        }
        if (end > target_len) {
            err = ERR_FUNC_INVALID_ARG_VAL;
            goto cleanup_end;
        }
        end_idx = end;
    }
    if (end_idx < start_idx) {
        err = ERR_FUNC_INVALID_ARG_VAL;
        goto cleanup_end;
    }
    size_t len = end_idx - start_idx;
    if (args_len == 4) {
        tracked_value cap = TRACKED_NULL;
        err = template_arg_iter_next(iter, &cap);
        if (err != 0) {
            goto cleanup_end;
        }
        size_t cap_idx;
        err = validate_index(&cap.val, &cap_idx);
        if (err != 0) {
            tracked_value_free(&cap);
            goto cleanup_end;
        }
        if (cap_idx < end_idx) {
            err = ERR_FUNC_INVALID_ARG_VAL;
            tracked_value_free(&cap);
            goto cleanup_end;
        }
    }
    if (target.val.ty == JSON_TY_ARRAY) {
        json_array arr = target.val.inner.arr;
        out->is_heap = false;
        out->val = (json_value){.ty = JSON_TY_ARRAY,
                                .inner = {.arr = {
                                              .data = arr.data + start_idx,
                                              .cap = arr.cap - start_idx,
                                              .len = len,
                                          }}};
    } else {
        char* sliced = malloc(len + 1);
        assert(sliced);
        strncpy(sliced, target.val.inner.str + start_idx, len);
        sliced[len] = 0;
        out->is_heap = true;
        out->val.ty = JSON_TY_STRING;
        out->val.inner.str = sliced;
    }
cleanup_end:
    tracked_value_free(&end_val);
cleanup_start:
    tracked_value_free(&start_val);
cleanup_target:
    tracked_value_free(&target);
    return err;
}

int go_equal(json_value* a, json_value* b, int* equal) {
    if (a->ty == JSON_TY_NULL || b->ty == JSON_TY_NULL) {
        *equal = (a->ty == b->ty);
        return 0;
    }
    if ((a->ty == JSON_TY_FALSE && b->ty == JSON_TY_TRUE) || (a->ty == JSON_TY_TRUE && b->ty == JSON_TY_FALSE)) {
        *equal = 0;
        return 0;
    }
    if (a->ty != b->ty) {
        return ERR_FUNC_INVALID_ARG_TYPE;
    }
    switch (a->ty) {
        case JSON_TY_NULL:
        case JSON_TY_TRUE:
        case JSON_TY_FALSE:
            *equal = 1;
            return 0;
        case JSON_TY_NUMBER:
            *equal = a->inner.num == b->inner.num;
            return 0;
        case JSON_TY_STRING:
            *equal = !strcmp(a->inner.str, b->inner.str);
            return 0;
        default:  // for some reason arrays and objects cannot be compared
            return ERR_FUNC_INVALID_ARG_TYPE;
    }
    assert(0);
}

int func_eq(template_arg_iter* iter, tracked_value* out) {
    int err = 0;
    size_t args_len = template_arg_iter_len(iter);
    if (args_len < 2) {
        return ERR_FUNC_INVALID_ARG_LEN;
    }
    tracked_value base = TRACKED_NULL;
    err = template_arg_iter_next(iter, &base);
    if (err) {
        return err;
    }
    out->is_heap = false;
    out->val.ty = JSON_TY_FALSE;
    // short circuiting is forbidden by the spec
    for (size_t i = 1; i < args_len; i++) {
        tracked_value local = TRACKED_NULL;
        err = template_arg_iter_next(iter, &local);
        if (err) {
            goto cleanup;
        }
        int equal;
        err = go_equal(&base.val, &local.val, &equal);
        tracked_value_free(&local);
        if (err) {
            goto cleanup;
        }
        if (equal) {
            out->val.ty = JSON_TY_TRUE;
        }
    }
cleanup:
    tracked_value_free(&base);
    return err;
}

int func_ne(template_arg_iter* iter, tracked_value* out) {
    int err = 0;
    size_t args_len = template_arg_iter_len(iter);
    if (args_len != 2) {
        return ERR_FUNC_INVALID_ARG_LEN;
    }
    tracked_value a = TRACKED_NULL;
    err = template_arg_iter_next(iter, &a);
    if (err) {
        return err;
    }
    tracked_value b = TRACKED_NULL;
    err = template_arg_iter_next(iter, &b);
    if (err) {
        goto cleanup;
    }
    int equal;
    err = go_equal(&a.val, &b.val, &equal);
    tracked_value_free(&b);
    if (err) {
        goto cleanup;
    }
    out->is_heap = false;
    out->val.ty = equal ? JSON_TY_FALSE : JSON_TY_TRUE;
cleanup:
    tracked_value_free(&a);
    return err;
}

int func_cmp(template_arg_iter* iter, tracked_value* out, int cmp_op) {
    size_t args_len = template_arg_iter_len(iter);
    if (args_len != 2) {
        return ERR_FUNC_INVALID_ARG_LEN;
    }
    int err = 0;
    tracked_value a = TRACKED_NULL;
    err = template_arg_iter_next(iter, &a);
    if (err) {
        return err;
    }
    tracked_value b = TRACKED_NULL;
    err = template_arg_iter_next(iter, &b);
    if (err) {
        goto cleanup;
    }
    if (a.val.ty != b.val.ty) {
        err = ERR_FUNC_INVALID_ARG_TYPE;
        tracked_value_free(&b);
        goto cleanup;
    }
    int result = 0;
    switch (a.val.ty) {
        case JSON_TY_STRING:
            result = strcmp(a.val.inner.str, b.val.inner.str);  // a > b
            switch (cmp_op) {
                case CMP_OP_LE:
                    if (result == 0) {
                        result = -1;
                    }
                    // deliberate fallthrough
                case CMP_OP_LT:
                    result = result * -1;
                    break;
                case CMP_OP_GE:
                    if (result == 0) {
                        result = 1;
                    }
                    break;
            }
            break;
        case JSON_TY_NUMBER:
            switch (cmp_op) {
                case CMP_OP_LT:
                    result = a.val.inner.num < b.val.inner.num;
                    break;
                case CMP_OP_LE:
                    result = a.val.inner.num <= b.val.inner.num;
                    break;
                case CMP_OP_GT:
                    result = a.val.inner.num > b.val.inner.num;
                    break;
                case CMP_OP_GE:
                    result = a.val.inner.num >= b.val.inner.num;
                    break;
            }
            break;
        default:
            err = ERR_FUNC_INVALID_ARG_TYPE;
            break;
    }
    out->is_heap = false;
    out->val.ty = result > 0 ? JSON_TY_TRUE : JSON_TY_FALSE;
    tracked_value_free(&b);
cleanup:
    tracked_value_free(&a);
    return err;
}

int func_lt(template_arg_iter* iter, tracked_value* out) {
    return func_cmp(iter, out, CMP_OP_LT);
}

int func_le(template_arg_iter* iter, tracked_value* out) {
    return func_cmp(iter, out, CMP_OP_LE);
}

int func_gt(template_arg_iter* iter, tracked_value* out) {
    return func_cmp(iter, out, CMP_OP_GT);
}

int func_ge(template_arg_iter* iter, tracked_value* out) {
    return func_cmp(iter, out, CMP_OP_GE);
}

int func_urlquery(template_arg_iter* iter, tracked_value* out) {
    buf print;
    int err = buf_print(iter, &print, NULL_STR_NO_VALUE);
    if (err) {
        return err;
    }
    buf b;
    buf_init(&b);
    const char* extra = "-_.~";
    for (size_t i = 0; i < print.len; i++) {
        unsigned char c = print.data[i];
        if (isalnum(c) || strchr(extra, c)) {
            buf_append(&b, print.data + i, 1);
        } else if (c == ' ') {
            buf_append(&b, "+", 1);
        } else {
            char hex[4];
            snprintf(hex, sizeof(hex), "%%%02X", c);
            buf_append(&b, hex, 3);
        }
    }
    buf_free(&print);
    buf_append(&b, "", 1);
    out->is_heap = true;
    out->val.ty = JSON_TY_STRING;
    out->val.inner.str = b.data;
    return 0;
}

int func_html(template_arg_iter* iter, tracked_value* out) {
    buf print;
    int err = buf_print(iter, &print, NULL_STR_NO_VALUE);
    if (err) {
        return err;
    }
    buf b;
    buf_init(&b);
    for (size_t i = 0; i < print.len; i++) {
        char c = print.data[i];
        switch (c) {
            case 0:
                buf_append(&b, "\\uFFFD", 6);
                break;
            case '<':
                buf_append(&b, "&lt;", 4);
                break;
            case '>':
                buf_append(&b, "&gt;", 4);
                break;
            case '"':
                buf_append(&b, "&#34;", 5);
                break;
            case '\'':
                buf_append(&b, "&#39;", 5);
                break;
            case '&':
                buf_append(&b, "&amp;", 5);
                break;
            default:
                buf_append(&b, &c, 1);
                break;
        }
    }
    buf_free(&print);
    buf_append(&b, "", 1);
    out->is_heap = true;
    out->val.ty = JSON_TY_STRING;
    out->val.inner.str = b.data;
    return 0;
}

void funcmap_new(hashmap* map) {
    hashmap_new(map, hashmap_strcmp, hashmap_strlen, HASH_FUNC_DJB2);
    hashmap_insert(map, "not", func_not);
    hashmap_insert(map, "and", func_and);
    hashmap_insert(map, "or", func_or);
    hashmap_insert(map, "len", func_len);
    hashmap_insert(map, "print", func_print);
    hashmap_insert(map, "println", func_println);
    hashmap_insert(map, "index", func_index);
    hashmap_insert(map, "slice", func_slice);
    hashmap_insert(map, "eq", func_eq);
    hashmap_insert(map, "ne", func_ne);
    hashmap_insert(map, "lt", func_lt);
    hashmap_insert(map, "le", func_le);
    hashmap_insert(map, "gt", func_gt);
    hashmap_insert(map, "ge", func_ge);
    hashmap_insert(map, "urlquery", func_urlquery);
    hashmap_insert(map, "html", func_html);
}

void funcmap_free(hashmap* map) {
    hashmap_free(map);
}

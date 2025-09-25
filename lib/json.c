#include "json.h"

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "encode.h"
#include "map.h"
#include "stream.h"

#define ERR_JSON_INVALID_ESCAPE -800
#define ERR_JSON_INVALID_SYNTAX -801
#define ERR_JSON_BUFFER_OVERFLOW -802
#define ERR_JSON_ARRAY_CLOSE -803
#define ERR_JSON_OBJECT_CLOSE -804

void json_array_free(json_array* arr) {
    for (size_t i = 0; i < arr->len; i++) {
        json_value_free(arr->data + i);
    }
    free(arr->data);
    arr->data = NULL;
    arr->len = 0;
    arr->cap = 0;
}

void map_free(entry* e, void* userdata) {
    free(e->key);
    json_value_free((json_value*)e->value);
    free(e->value);
}

void json_object_free(hashmap* obj) {
    hashmap_iter(obj, NULL, map_free);
    hashmap_free(obj);
}

void json_value_free(json_value* val) {
    switch (val->ty) {
        case JSON_TY_STRING:
            free(val->inner.str);
            break;
        case JSON_TY_ARRAY:
            json_array_free(&val->inner.arr);
            break;
        case JSON_TY_OBJECT:
            json_object_free(&val->inner.obj);
            break;
    }
}

void json_value_copy_iter(entry* entry, void* userdata) {
    hashmap* dest = (hashmap*)userdata;
    char* key = strdup(entry->key);
    json_value* value = malloc(sizeof(json_value));
    assert(value);
    json_value_copy(value, entry->value);
    hashmap_insert(dest, key, value);
}

void json_value_copy(json_value* dest, const json_value* src) {
    dest->ty = src->ty;
    switch (src->ty) {
        case JSON_TY_NULL:
        case JSON_TY_TRUE:
        case JSON_TY_FALSE:
            return;
        case JSON_TY_NUMBER:
            dest->inner.num = src->inner.num;
            return;
        case JSON_TY_STRING:
            dest->inner.str = strdup(src->inner.str);
            return;
        case JSON_TY_ARRAY:
            dest->inner.arr = src->inner.arr;
            dest->inner.arr.data = malloc(sizeof(json_value) * dest->inner.arr.cap);
            assert(dest->inner.arr.data);
            const json_array* srcarr = &src->inner.arr;
            json_array* destarr = &dest->inner.arr;
            for (size_t i = 0; i < srcarr->len; i++) {
                json_value_copy(&destarr->data[i], &srcarr->data[i]);
            }
            return;
        case JSON_TY_OBJECT:
            hashmap_new(&dest->inner.obj, hashmap_strcmp, hashmap_strlen, HASH_FUNC_DJB2);
            hashmap_iter(&src->inner.obj, &dest->inner.obj, json_value_copy_iter);
            return;
    }
    assert(0);
}

int json_value_equal(const json_value* a, const json_value* b) {
    if (a->ty != b->ty) {
        return 0;
    }
    switch (a->ty) {
        case JSON_TY_NULL:
        case JSON_TY_TRUE:
        case JSON_TY_FALSE:
            return 1;
        case JSON_TY_NUMBER:
            return a->inner.num == b->inner.num;
        case JSON_TY_STRING:
            return !strcmp(a->inner.str, b->inner.str);
        case JSON_TY_ARRAY: {
            if (a->inner.arr.len != b->inner.arr.len) {
                return 0;
            }
            for (size_t i = 0; i < a->inner.arr.len; ++i) {
                if (!json_value_equal(&a->inner.arr.data[i], &b->inner.arr.data[i])) {
                    return 0;
                }
            }
            return 1;
        }
        case JSON_TY_OBJECT: {
            if (a->inner.obj.count != b->inner.obj.count) {
                return 0;
            }
            char** keys = (char**)hashmap_keys(&a->inner.obj);
            for (size_t i = 0; i < a->inner.obj.count; i++) {
                json_value* aval;
                json_value* bval;
                assert(hashmap_get(&a->inner.obj, keys[i], (const void**)&aval));
                assert(hashmap_get(&b->inner.obj, keys[i], (const void**)&bval));
                if (!json_value_equal(aval, bval)) {
                    free(keys);
                    return 0;
                }
            }
            free(keys);
            return 1;
        }
        default:
            assert(0);
    }
}

void json_str_append(char val, char** buf, size_t* len, size_t* cap) {
    if (*len == *cap) {
        *cap = *cap * 3 / 2;
        *buf = realloc(*buf, *cap);
        assert(*buf);
    }
    (*buf)[*len] = val;
    *len += 1;
    return;
}

// the leading quotation mark was just read
// will consume the trailing quotation mark
int json_parse_str(stream* st, char** out, size_t* out_cap) {
    size_t out_len = 0;
    *out_cap = 32;
    *out = malloc(*out_cap);

    unsigned char cp[4];
    size_t cp_len;
    int escaped = false;
    int err = 0;
    unsigned char escaped_cp[5];
    while (true) {
        err = stream_next_utf8_cp(st, cp, &cp_len);
        if (err != 0) {
            goto cleanup;
        }
        if (escaped) {
            if (cp_len != 1) {
                err = ERR_JSON_INVALID_ESCAPE;
                goto cleanup;
            }
            switch (cp[0]) {
                case '"':
                case '\\':
                case '/':
                    json_str_append(cp[0], out, &out_len, out_cap);
                    break;
                case 'b':
                    json_str_append('\b', out, &out_len, out_cap);
                    break;
                case 'f':
                    json_str_append('\f', out, &out_len, out_cap);
                    break;
                case 'n':
                    json_str_append('\n', out, &out_len, out_cap);
                    break;
                case 'r':
                    json_str_append('\r', out, &out_len, out_cap);
                    break;
                case 't':
                    json_str_append('\t', out, &out_len, out_cap);
                    break;
                case 'u':
                    for (size_t i = 0; i < 4; i++) {
                        err = stream_next_utf8_cp(st, cp, &cp_len);
                        if (err != 0) {
                            goto cleanup;
                        }
                        if (cp_len != 1) {
                            err = ERR_JSON_INVALID_ESCAPE;
                            goto cleanup;
                        }
                        if (!((cp[0] >= '0' && cp[0] <= '9') || (cp[0] >= 'a' && cp[0] <= 'f'))) {
                            err = ERR_JSON_INVALID_ESCAPE;
                            goto cleanup;
                        }
                        escaped_cp[i] = cp[0];
                    }
                    escaped_cp[4] = 0;
                    long unescaped_cp = strtol((const char*)escaped_cp, NULL, 16);
                    char encoded[4];
                    size_t encoded_len;
                    utf8_encode((int32_t)unescaped_cp, encoded, &encoded_len);
                    for (size_t i = 0; i < encoded_len; i++) {
                        json_str_append(encoded[i], out, &out_len, out_cap);
                    }
                    break;
                default:
                    err = ERR_JSON_INVALID_ESCAPE;
                    goto cleanup;
            }
            escaped = false;
        } else {
            if (cp_len > 1) {
                for (size_t i = 0; i < cp_len; i++) {
                    json_str_append(cp[i], out, &out_len, out_cap);
                }
                continue;
            }
            if (cp[0] == '\\') {
                escaped = true;
                continue;
            }
            if (cp[0] == '"') {
                break;
            }
            json_str_append(cp[0], out, &out_len, out_cap);
        }
    }
cleanup:
    if (err == 0) {
        json_str_append(0, out, &out_len, out_cap);
    } else {
        free(*out);
        *out = NULL;
    }
    return err;
}

bool json_is_terminal(char c) {
    return c == ',' || c == '}' || c == ']' || c == 0x20 || c == 0x09 || c == 0x0a || c == 0x0d;
}

#define JSON_NO_LAST_CHAR 0

int json_parse_pos_number(stream* st, char first, double* out, char* last) {
    unsigned char cp[4];
    size_t cp_len;
    char buf[128];
    buf[0] = first;
    size_t buf_idx = 1;
    if (first == '0') {
        *out = 0;
        int err = stream_next_utf8_cp(st, cp, &cp_len);
        if (err == EOF) {
            *last = JSON_NO_LAST_CHAR;
            return 0;
        }
        if (err != 0) {
            return err;
        }
        if (cp_len != 1) {
            return ERR_JSON_INVALID_SYNTAX;
        }
        if (json_is_terminal(cp[0])) {
            *last = cp[0];
            return 0;
        }
        if (cp[0] != '.') {
            return ERR_JSON_INVALID_SYNTAX;
        }
        buf[buf_idx] = cp[0];
        buf_idx++;
    }
    while (buf_idx < sizeof(buf)) {
        int err = stream_next_utf8_cp(st, cp, &cp_len);
        if (err == EOF) {
            cp[0] = JSON_NO_LAST_CHAR;
            goto finish;
        }
        if (err != 0) {
            return err;
        }
        if (cp_len != 1) {
            return ERR_JSON_INVALID_SYNTAX;
        }
        if (json_is_terminal(cp[0])) {
            goto finish;
        }
        if (!(cp[0] >= '0' && cp[0] <= '9') && cp[0] != 'e' && cp[0] != 'E' && cp[0] != '.') {
            return ERR_JSON_INVALID_SYNTAX;
        }
        buf[buf_idx] = cp[0];
        buf_idx++;
    }
    return ERR_JSON_BUFFER_OVERFLOW;
finish:
    buf[buf_idx] = 0;
    char* end;
    *out = strtod(buf, &end);
    if (errno == ERANGE) {
        errno = 0;
        return ERR_JSON_INVALID_SYNTAX;
    }
    if (buf_idx != end - buf) {
        return ERR_JSON_INVALID_SYNTAX;
    }
    *last = cp[0];
    return 0;
}

// out needs to be at least 4 bytes long
int json_skip_whitespace(stream* st, unsigned char* out, size_t* out_len) {
    while (true) {
        int err = stream_next_utf8_cp(st, out, out_len);
        if (err != 0) {
            return err;
        }
        if (*out_len > 1) {
            return 0;
        }
        // space, horizonzal tab, new line, carriage return
        if (out[0] != 0x20 && out[0] != 0x09 && out[0] != 0x0a && out[0] != 0x0d) {
            return 0;
        }
    }
}

int json_match_ascii(stream* st, char* expected, size_t len) {
    unsigned char cp[4];
    size_t cp_len;
    for (size_t i = 0; i < len; i++) {
        int err = stream_next_utf8_cp(st, cp, &cp_len);
        if (err != 0) {
            return err;
        }
        if (cp_len > 1) {
            return ERR_JSON_INVALID_SYNTAX;
        }
        if (expected[i] != cp[0]) {
            return ERR_JSON_INVALID_SYNTAX;
        }
    }
    return 0;
}

int json_parse_value(stream* st, json_value* val, char* last_char);

#define JSON_ARRAY_DEFAULT_CAP 8

int json_parse_array(stream* st, json_array* arr) {
    char last_char;
    unsigned char cp[4];
    size_t cp_len;
    int err = 0;
    json_value val;
    arr->data = NULL;
    arr->len = 0;
    arr->cap = 0;
    while (true) {
        err = json_parse_value(st, &val, &last_char);
        if (err != 0) {
            if (err == ERR_JSON_ARRAY_CLOSE) {
                err = 0;
            }
            goto cleanup;
        }
        if (arr->data == NULL) {
            arr->data = malloc(JSON_ARRAY_DEFAULT_CAP * sizeof(json_value));
            assert(arr->data);
            arr->cap = JSON_ARRAY_DEFAULT_CAP;
        }
        if (arr->len == arr->cap) {
            arr->cap = arr->cap * 3 / 2;
            arr->data = realloc(arr->data, arr->cap * sizeof(json_value));
            assert(arr->data);
        }
        memcpy(arr->data + arr->len, &val, sizeof(val));
        arr->len++;
        switch (last_char) {
            case ']':
                goto cleanup;
            case ',':
                continue;
        }
        err = json_skip_whitespace(st, cp, &cp_len);
        if (err != 0) {
            goto cleanup;
        }
        switch (cp[0]) {
            case ']':
                goto cleanup;
            case ',':
                continue;
            default:
                err = ERR_JSON_INVALID_SYNTAX;
                goto cleanup;
        }
    }
cleanup:
    if (err != 0) {
        json_array_free(arr);
    }
    return err;
}

int json_parse_object(stream* st, hashmap* obj) {
    bool first = true;
    int err = 0;
    unsigned char cp[4];
    size_t cp_len;
    char* key = NULL;
    char last_char;
    hashmap_new(obj, hashmap_strcmp, hashmap_strlen, HASH_FUNC_DJB2);
    while (true) {
        err = json_skip_whitespace(st, cp, &cp_len);
        if (err != 0) {
            goto cleanup;
        }
        if (first && cp[0] == '}') {
            goto cleanup;
        }
        first = false;
        if (cp[0] != '"') {
            err = ERR_JSON_INVALID_SYNTAX;
            goto cleanup;
        }
        key = NULL;
        size_t key_cap;
        err = json_parse_str(st, &key, &key_cap);
        if (err != 0) {
            goto cleanup;
        }
        err = json_skip_whitespace(st, cp, &cp_len);
        if (err != 0) {
            goto cleanup;
        }
        if (cp[0] != ':') {
            err = ERR_JSON_INVALID_SYNTAX;
            goto cleanup;
        }
        json_value* val = malloc(sizeof(json_value));
        assert(val);
        err = json_parse_value(st, val, &last_char);
        if (err != 0) {
            free(val);
            goto cleanup;
        }
        entry prev = hashmap_insert(obj, key, val);
        key = NULL;
        if (prev.exists) {
            free(prev.key);
            json_value_free(prev.value);
            free(prev.value);
        }
        switch (last_char) {
            case '}':
                goto cleanup;
            case ',':
                continue;
        }
        err = json_skip_whitespace(st, cp, &cp_len);
        if (err != 0) {
            goto cleanup;
        }
        switch (cp[0]) {
            case '}':
                goto cleanup;
            case ',':
                continue;
            default:
                err = ERR_JSON_INVALID_SYNTAX;
                goto cleanup;
        }
    }
cleanup:
    if (err != 0) {
        free(key);
        json_object_free(obj);
    }
    return err;
}

int json_parse_value(stream* st, json_value* val, char* last_char) {
    unsigned char cp[4];
    size_t cp_len, out_cap;
    char true_str[3] = {'r', 'u', 'e'};
    char false_str[4] = {'a', 'l', 's', 'e'};
    char null_str[3] = {'u', 'l', 'l'};
    *last_char = JSON_NO_LAST_CHAR;
    int err = json_skip_whitespace(st, cp, &cp_len);
    if (err != 0) {
        return err;
    }
    switch (cp[0]) {
        case '"':
            val->ty = JSON_TY_STRING;
            return json_parse_str(st, &val->inner.str, &out_cap);
        case 't':
            val->ty = JSON_TY_TRUE;
            return json_match_ascii(st, true_str, sizeof(true_str));
        case 'f':
            val->ty = JSON_TY_FALSE;
            return json_match_ascii(st, false_str, sizeof(false_str));
        case 'n':
            val->ty = JSON_TY_NULL;
            return json_match_ascii(st, null_str, sizeof(null_str));
        case '0':
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9':
            val->ty = JSON_TY_NUMBER;
            return json_parse_pos_number(st, cp[0], &val->inner.num, last_char);
        case '-':
            val->ty = JSON_TY_NUMBER;
            err = stream_next_utf8_cp(st, cp, &cp_len);
            if (err != 0) {
                return err;
            }
            if (cp_len != 1) {
                return ERR_JSON_INVALID_SYNTAX;
            }
            err = json_parse_pos_number(st, cp[0], &val->inner.num, last_char);
            if (err != 0) {
                return err;
            }
            val->inner.num *= -1;
            return 0;
        case '[':
            val->ty = JSON_TY_ARRAY;
            return json_parse_array(st, &val->inner.arr);
        case ']':
            return ERR_JSON_ARRAY_CLOSE;
        case '{':
            val->ty = JSON_TY_OBJECT;
            return json_parse_object(st, &val->inner.obj);
        default:
            *last_char = cp[0];
            return ERR_JSON_INVALID_SYNTAX;
    }
    return 0;
}

int json_parse(stream* st, json_value* val) {
    char last_char;
    return json_parse_value(st, val, &last_char);
}

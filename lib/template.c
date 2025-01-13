#include "template.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "encode.h"
#include "json.h"
#include "map.h"
#include "stream.h"

typedef struct {
    char* data;
    size_t len;
    size_t cap;
} buf;

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
    const char* str_true = "true";
    const char* str_false = "false";
    const char* str_null = "<nil>";
    json_array* arr;
    hashmap* obj;
    switch (val->ty) {
        case JSON_TY_NUMBER:
            expected = snprintf(print_buf, sizeof(print_buf), "%g", val->inner.num);
            if (expected > sizeof(print_buf) - 1) {
                return ERR_TEMPLATE_BUFFER_OVERFLOW;
            }
            buf_append(b, print_buf, expected);
            return 0;
        case JSON_TY_STRING:
            buf_append(b, val->inner.str, strlen(val->inner.str));
            return 0;
        case JSON_TY_TRUE:
            buf_append(b, str_true, strlen(str_true));
            return 0;
        case JSON_TY_FALSE:
            buf_append(b, str_false, strlen(str_false));
            return 0;
        case JSON_TY_NULL:
            buf_append(b, str_null, strlen(str_null));
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

typedef struct {
    size_t len;
    size_t cap;
    hashmap* frames;
} stack;

#define DEFAULT_STACK_CAP 8

void stack_new(stack* s) {
    s->len = 0;
    s->cap = DEFAULT_STACK_CAP;
    s->frames = malloc(sizeof(hashmap) * s->cap);
    assert(s->frames);
}

void stack_free_entry(entry* entry, void* userdata) {
    free(entry->key);
    json_value_free(entry->value);
    free(entry->value);
}

void stack_free(stack* s) {
    for (size_t i = 0; i < s->len; i++) {
        hashmap_iter(&s->frames[i], NULL, stack_free_entry);
        hashmap_free(&s->frames[i]);
    }
    free(s->frames);
}

void stack_push_frame(stack* s) {
    if (s->len == s->cap) {
        s->cap = s->cap * 3 / 2;
        s->frames = realloc(s->frames, sizeof(hashmap) * s->cap);
        assert(s->frames);
    }
    hashmap_new(&s->frames[s->len], hashmap_strcmp, hashmap_strlen, HASH_FUNC_DJB2);
    s->len++;
}

void stack_pop_frame(stack* s) {
    assert(s->len > 0);
    hashmap_iter(&s->frames[s->len - 1], NULL, stack_free_entry);
    hashmap_free(&s->frames[s->len - 1]);
    s->len--;
}

void stack_set_var(stack* s, char* var, json_value* value) {
    entry previous = hashmap_insert(&s->frames[s->len - 1], (void*)var, value);
    if (previous.key != NULL) {
        stack_free_entry(&previous, NULL);
    }
}

const json_value* stack_find_var(stack* s, const char* var) {
    for (ptrdiff_t i = s->len - 1; i >= 0; i--) {
        const json_value* out;
        if (hashmap_get(&s->frames[i], var, (const void**)&out)) {
            return out;
        }
    }
    return NULL;
}

#define STATE_IDENT_CAP 128

#define RETURN_REASON_REGULAR 0
#define RETURN_REASON_END 1
#define RETURN_REASON_ELSE 2
#define RETURN_REASON_BREAK 3
#define RETURN_REASON_CONTINUE 4

typedef struct {
    json_value scratch_val;
    json_value* dot;
    buf out;
    size_t out_nospace;
    stack stack;
    int return_reason;
    char ident[STATE_IDENT_CAP];
} state;

json_value state_set_scratch(state* state, json_value val) {
    json_value_free(&state->scratch_val);
    state->scratch_val = val;
    return state->scratch_val;
}

int template_skip_whitespace(stream* in) {
    unsigned char cp[4];
    size_t cp_len;
    int err = 0;
    bool space = true;
    while (space) {
        err = stream_next_utf8_cp(in, cp, &cp_len);
        if (err != 0) {
            return err;
        }
        space = isspace(cp[0]);
    }
    err = stream_seek(in, -cp_len);
    if (err != 0) {
        return err;
    }
    return err;
}

int template_parse_number(stream* in, double* out) {
    unsigned char cp[4];
    size_t cp_len;
    int frac_allowed = true;
    int sign_allowed = true;
    int is_hex = false;
    char buf[128];
    size_t buf_idx = 0;
    while (buf_idx < sizeof(buf)) {
        int err = stream_next_utf8_cp(in, cp, &cp_len);
        if (err != 0) {
            return err;
        }
        if (cp_len != 1) {
            return ERR_TEMPLATE_INVALID_SYNTAX;
        }
        switch (cp[0]) {
            case '+':
            case '-':
                if (!sign_allowed) {
                    return ERR_TEMPLATE_INVALID_SYNTAX;
                }
                sign_allowed = false;
                break;
            case '.':
                if (!frac_allowed) {
                    return ERR_TEMPLATE_INVALID_SYNTAX;
                }
                frac_allowed = false;
                break;
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
                sign_allowed = false;
                break;
            case 'a':
            case 'A':
            case 'b':
            case 'B':
            case 'c':
            case 'C':
            case 'd':
            case 'D':
            case 'f':
            case 'F':
                if (!is_hex) {
                    return ERR_TEMPLATE_INVALID_SYNTAX;
                }
                sign_allowed = false;
                break;
            case 'e':
            case 'E':
                if (is_hex) {
                    sign_allowed = false;
                } else {
                    if (!frac_allowed) {
                        return ERR_TEMPLATE_INVALID_SYNTAX;
                    }
                    sign_allowed = true;
                    frac_allowed = false;
                }
                break;
            case 'x':
            case 'X':
                if (buf_idx != 1 || buf[0] != '0') {
                    return ERR_TEMPLATE_INVALID_SYNTAX;
                }
                is_hex = true;
                break;
            case 'p':
            case 'P':
                if (!is_hex || !frac_allowed) {
                    return ERR_TEMPLATE_INVALID_SYNTAX;
                }
                sign_allowed = true;
                frac_allowed = false;
                break;
            default:
                buf[buf_idx] = 0;
                *out = strtod(buf, NULL);
                if (errno == ERANGE) {
                    errno = 0;
                    return ERR_TEMPLATE_INVALID_SYNTAX;
                }
                return stream_seek(in, -1);
        }
        buf[buf_idx] = cp[0];
        buf_idx++;
    }
    return ERR_TEMPLATE_BUFFER_OVERFLOW;
}

int template_parse_backtick_str(stream* in, char** out) {
    *out = NULL;
    unsigned char cp[4];
    size_t cp_len;
    buf b;
    int err = 0;
    buf_init(&b);
    while (true) {
        err = stream_next_utf8_cp(in, cp, &cp_len);
        if (err != 0) {
            goto cleanup;
        }
        if (cp_len > 1 || cp[0] != '`') {
            buf_append(&b, (const char*)cp, cp_len);
            continue;
        }
        buf_append(&b, "\0", 1);
        *out = b.data;
        goto cleanup;
    }
cleanup:
    if (err != 0) {
        buf_free(&b);
    }
    return err;
}

int template_parse_regular_str(stream* in, char** out) {
    *out = NULL;
    unsigned char cp[4];
    size_t cp_len;
    buf b;
    int err = 0;
    buf_init(&b);
    while (true) {
        err = stream_next_utf8_cp(in, cp, &cp_len);
        if (err != 0) {
            goto cleanup;
        }
        if (cp_len > 1) {
            buf_append(&b, (const char*)cp, cp_len);
            continue;
        }
        switch (cp[0]) {
            case '"':
                buf_append(&b, "\0", 1);
                *out = b.data;
                goto cleanup;
            case '\\':
                err = stream_next_utf8_cp(in, cp, &cp_len);
                if (err != 0) {
                    goto cleanup;
                }
                if (cp_len != 1) {
                    err = ERR_TEMPLATE_INVALID_SYNTAX;
                    goto cleanup;
                }
                unsigned char escaped_cp[5];
                switch (cp[0]) {
                    case '"':
                    case '\\':
                    case '/':
                        buf_append(&b, (const char*)cp, 1);
                        break;
                    case 'b':
                        buf_append(&b, "\b", 1);
                        break;
                    case 'f':
                        buf_append(&b, "\f", 1);
                        break;
                    case 'n':
                        buf_append(&b, "\n", 1);
                        break;
                    case 'r':
                        buf_append(&b, "\r", 1);
                        break;
                    case 't':
                        buf_append(&b, "\t", 1);
                        break;
                    case 'u':
                        for (size_t i = 0; i < 4; i++) {
                            err = stream_next_utf8_cp(in, cp, &cp_len);
                            if (err != 0) {
                                goto cleanup;
                            }
                            if (cp_len != 1) {
                                err = ERR_TEMPLATE_INVALID_ESCAPE;
                                goto cleanup;
                            }
                            if (!((cp[0] >= '0' && cp[0] <= '9') || (cp[0] >= 'a' && cp[0] <= 'f'))) {
                                err = ERR_TEMPLATE_INVALID_ESCAPE;
                                goto cleanup;
                            }
                            escaped_cp[i] = cp[0];
                        }
                        escaped_cp[4] = 0;
                        long unescaped_cp = strtol((const char*)escaped_cp, NULL, 16);
                        char encoded[4];
                        size_t encoded_len;
                        utf8_encode((int32_t)unescaped_cp, encoded, &encoded_len);
                        buf_append(&b, encoded, encoded_len);
                        break;
                }
                continue;
            default:
                buf_append(&b, (const char*)cp, 1);
                continue;
        }
    }
cleanup:
    if (err != 0) {
        buf_free(&b);
    }
    return err;
}

int template_parse_ident(stream* in, state* state) {
    unsigned char cp[4];
    size_t cp_len;
    int err = 0;
    for (size_t i = 0; i < STATE_IDENT_CAP; i++) {
        err = stream_next_utf8_cp(in, cp, &cp_len);
        if (err != 0) {
            return err;
        }
        if (cp_len > 1) {
            return ERR_TEMPLATE_INVALID_SYNTAX;
        }
        if (!isalnum(cp[0])) {
            state->ident[i] = 0;
            return stream_seek(in, -1);
        }
        state->ident[i] = cp[0];
    }
    return ERR_TEMPLATE_BUFFER_OVERFLOW;
}

int template_parse_path_expr_recurse(stream* in, state* state, json_value* result) {
    unsigned char cp[4];
    size_t cp_len;
    int err = stream_next_utf8_cp(in, cp, &cp_len);
    if (err != 0) {
        return err;
    }
    if (cp_len != 1) {
        return ERR_TEMPLATE_INVALID_SYNTAX;
    }
    if (cp[0] != '.') {
        *result = *state->dot;
        return stream_seek(in, -1);
    }
    err = template_parse_ident(in, state);
    if (err != 0) {
        return err;
    }
    if (strlen(state->ident) == 0) {
        return ERR_TEMPLATE_INVALID_SYNTAX;
    }
    if (state->dot->ty != JSON_TY_OBJECT) {
        return ERR_TEMPLATE_NO_OBJECT;
    }
    json_value* next;
    int found = hashmap_get(&state->dot->inner.obj, state->ident, (const void**)&next);
    if (!found) {
        return ERR_TEMPLATE_KEY_UNKNOWN;
    }
    json_value* current = state->dot;
    state->dot = next;
    err = template_parse_path_expr_recurse(in, state, result);
    state->dot = current;
    return err;
}

int template_parse_path_expr(stream* in, state* state, json_value* result) {
    *result = JSON_NULL;
    int err = template_parse_ident(in, state);
    if (err != 0) {
        return err;
    }
    if (strlen(state->ident) == 0) {
        *result = *state->dot;
        return 0;
    }
    if (state->dot->ty != JSON_TY_OBJECT) {
        return ERR_TEMPLATE_NO_OBJECT;
    }
    json_value* next;
    int found = hashmap_get(&state->dot->inner.obj, state->ident, (const void**)&next);
    if (!found) {
        return ERR_TEMPLATE_KEY_UNKNOWN;
    }
    json_value* current = state->dot;
    state->dot = next;
    err = template_parse_path_expr_recurse(in, state, result);
    state->dot = current;
    return err;
}

int template_parse_var_value(stream* in, state* state, json_value* result) {
    unsigned char cp[4];
    size_t cp_len;
    int err = stream_next_utf8_cp(in, cp, &cp_len);
    if (err != 0) {
        return err;
    }
    if (cp_len > 1 || !isspace(cp[0])) {
        err = stream_seek(in, -cp_len);
        if (err != 0) {
            return err;
        }
        err = template_parse_ident(in, state);
        if (err != 0) {
            return err;
        }
    } else {
        state->ident[0] = 0;
    }
    const json_value* out = stack_find_var(&state->stack, state->ident);
    if (out == NULL) {
        return ERR_TEMPLATE_VAR_UNKNOWN;
    }
    *result = *out;
    return 0;
}

int template_parse_value(stream* in, state* state, json_value* result, unsigned char first) {
    unsigned char cp[4];
    size_t cp_len;
    int err = 0;
    double val;
    size_t seek_back = -1;
    size_t identifier_len = 0;
    json_value out = JSON_NULL;
    long pre_pos;
    switch (first) {
        case 't':
            err = template_parse_ident(in, state);
            if (err != 0) {
                return err;
            }
            bool is_true = strcmp("rue", state->ident) == 0;
            if (is_true) {
                out.ty = JSON_TY_TRUE;
                *result = state_set_scratch(state, out);
                return 0;
            }
            identifier_len = strlen(state->ident);
            err = stream_seek(in, -identifier_len - 1);
            if (err != 0) {
                return err;
            }
            return ERR_TEMPLATE_NO_LITERAL;
        case 'f':
            err = template_parse_ident(in, state);
            if (err != 0) {
                return err;
            }
            bool is_false = strcmp("alse", state->ident) == 0;
            if (is_false) {
                out.ty = JSON_TY_FALSE;
                *result = state_set_scratch(state, out);
                return 0;
            }
            identifier_len = strlen(state->ident);
            err = stream_seek(in, -identifier_len - 1);
            if (err != 0) {
                return err;
            }
            return ERR_TEMPLATE_NO_LITERAL;
        case 'n':
            err = template_parse_ident(in, state);
            if (err != 0) {
                return err;
            }
            bool is_nil = strcmp("il", state->ident) == 0;
            if (is_nil) {
                out.ty = JSON_TY_NULL;
                *result = state_set_scratch(state, out);
                return 0;
            }
            err = stream_seek(in, -identifier_len - 1);
            if (err != 0) {
                return err;
            }
            return ERR_TEMPLATE_NO_LITERAL;
        case '$':
            return template_parse_var_value(in, state, result);
        case '"':
            err = template_parse_regular_str(in, &out.inner.str);
            if (err != 0) {
                return err;
            }
            out.ty = JSON_TY_STRING;
            *result = state_set_scratch(state, out);
            return 0;
        case '`':
            err = template_parse_backtick_str(in, &out.inner.str);
            if (err != 0) {
                return err;
            }
            out.ty = JSON_TY_STRING;
            *result = state_set_scratch(state, out);
            return 0;
        case '-':
            err = stream_next_utf8_cp(in, cp, &cp_len);
            if (err != 0) {
                return err;
            }
            if (cp_len != 1) {
                return ERR_TEMPLATE_INVALID_SYNTAX;
            }
            if (!isdigit(cp[0]) && cp[0] != '+') {
                return ERR_TEMPLATE_LITERAL_DASH;
            }
            seek_back = -2;
            // delibarate fallthrough
        case '+':
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
            err = stream_seek(in, seek_back);
            if (err != 0) {
                return err;
            }
            err = template_parse_number(in, &val);
            if (err != 0) {
                return err;
            }
            out.ty = JSON_TY_NUMBER;
            out.inner.num = val;
            *result = state_set_scratch(state, out);
            return 0;
        case '.':
            return template_parse_path_expr(in, state, result);
        default:
            return ERR_TEMPLATE_NO_LITERAL;
    }
    return 0;
}

int template_parse_var_mutation(stream* in, state* state, json_value* result) {
    unsigned char cp[4];
    size_t cp_len;
    *result = JSON_NULL;
    // parse var name
    int err = stream_next_utf8_cp(in, cp, &cp_len);
    if (err != 0) {
        return err;
    }
    if (cp_len > 1 || !isspace(cp[0])) {
        err = stream_seek(in, -cp_len);
        if (err != 0) {
            return err;
        }
        err = template_parse_ident(in, state);
        if (err != 0) {
            return err;
        }
    } else {
        state->ident[0] = 0;
    }
    err = template_skip_whitespace(in);
    if (err != 0) {
        return err;
    }
    // check for definition/assignment
    err = stream_next_utf8_cp(in, cp, &cp_len);
    if (err != 0) {
        return err;
    }
    if (cp_len > 1) {
        return ERR_TEMPLATE_NO_MUTATION;
    }
    bool is_assignment = true;
    switch (cp[0]) {
        case '=':
            break;
        case ':':
            err = stream_next_utf8_cp(in, cp, &cp_len);
            if (err != 0) {
                return err;
            }
            if (cp_len > 1 || cp[0] != '=') {
                return ERR_TEMPLATE_NO_MUTATION;
            }
            is_assignment = false;
            break;
        default:
            return ERR_TEMPLATE_NO_MUTATION;
    }
    const json_value* current_val = stack_find_var(&state->stack, state->ident);
    if (current_val == NULL && is_assignment) {
        return ERR_TEMPLATE_VAR_UNKNOWN;
    }
    err = template_skip_whitespace(in);
    if (err != 0) {
        return err;
    }
    // parse the value
    err = stream_next_utf8_cp(in, cp, &cp_len);
    if (err != 0) {
        return err;
    }
    if (cp_len > 1) {
        return ERR_TEMPLATE_INVALID_SYNTAX;
    }
    json_value out;
    char* ident_copy = strdup(state->ident);
    err = template_parse_value(in, state, &out, cp[0]);  // returns a pointer to state->scratch_val
    if (err != 0) {
        free(ident_copy);
        return err;
    }
    // nil cannot be assigned in go
    if (out.ty == JSON_TY_NULL) {
        free(ident_copy);
        return ERR_TEMPLATE_KEYWORD_UNEXPECTED;
    }
    json_value* value_copy = malloc(sizeof(json_value));
    assert(value_copy);
    json_value_copy(value_copy, &out);
    stack_set_var(&state->stack, ident_copy, value_copy);
    *result = state->scratch_val;
    return 0;
}

int template_parse_value_with_var_mut(stream* in, state* state, json_value* result, unsigned char first) {
    if (first == '$') {
        long pre_pos;
        int err = stream_pos(in, &pre_pos);
        if (err != 0) {
            return err;
        }
        err = template_parse_var_mutation(in, state, result);
        if (err != ERR_TEMPLATE_NO_MUTATION) {
            return err;
        }
        long post_pos;
        err = stream_pos(in, &post_pos);
        if (err != 0) {
            return err;
        }
        err = stream_seek(in, pre_pos - post_pos);
        if (err != 0) {
            return err;
        }
    }
    return template_parse_value(in, state, result, first);
}

int template_parse_arg(stream* in, state* state, json_value* arg) {
    *arg = JSON_NULL;
    unsigned char cp[4];
    size_t cp_len;
    int err = template_skip_whitespace(in);
    if (err != 0) {
        return err;
    }
    err = stream_next_utf8_cp(in, cp, &cp_len);
    if (err != 0) {
        return err;
    }
    if (cp_len != 1) {
        return ERR_TEMPLATE_INVALID_SYNTAX;
    }
    return template_parse_value_with_var_mut(in, state, arg, cp[0]);
}

int template_end_pipeline(stream* in, state* state, json_value* result) {
    unsigned char cp[4];
    size_t cp_len;
    bool has_minus = false;
    while (true) {
        int err = stream_next_utf8_cp(in, cp, &cp_len);
        if (err != 0) {
            return err;
        }
        if (cp_len != 1) {
            return ERR_TEMPLATE_INVALID_SYNTAX;
        }
        if (isspace(cp[0]) && !has_minus) {
            continue;
        }
        switch (cp[0]) {
            case '-':
                has_minus = true;
                continue;
            case '}':
                err = stream_next_utf8_cp(in, cp, &cp_len);
                if (err != 0) {
                    return err;
                }
                if (cp[0] != '}') {
                    continue;
                }
                if (has_minus) {
                    template_skip_whitespace(in);
                }
                if (result->ty == JSON_TY_NULL) {
                    return 0;
                }
                return sprintval(&state->out, result);
            default:
                return ERR_TEMPLATE_INVALID_SYNTAX;
        }
    }
}

int template_parse_noop_ident(stream* in, state* state, char leading) {
    unsigned char cp[4];
    size_t cp_len;
    int err = 0;
    state->ident[0] = leading;
    for (size_t i = 1; i < STATE_IDENT_CAP; i++) {
        err = stream_next_utf8_cp(in, cp, &cp_len);
        if (err != 0) {
            return err;
        }
        if (cp_len > 1) {
            return ERR_TEMPLATE_INVALID_SYNTAX;
        }
        bool keep = isalnum(cp[0]) || cp[0] == '.' || cp[0] == '$' || cp[0] == '|' || cp[0] == '(' || cp[0] == ')';
        if (!keep) {
            state->ident[i] = 0;
            return stream_seek(in, -1);
        }
        state->ident[i] = cp[0];
    }
    return ERR_TEMPLATE_BUFFER_OVERFLOW;
}

int template_pipeline_noop(stream* in, state* state, size_t start_depth) {
    unsigned char cp[4];
    size_t cp_len;
    int err = stream_next_utf8_cp(in, cp, &cp_len);
    if (err != 0) {
        return err;
    }
    if (cp_len != 1) {
        return ERR_TEMPLATE_INVALID_SYNTAX;
    }
    if (cp[0] != '-') {
        err = stream_seek(in, -1);
        if (err != 0) {
            return err;
        }
    }
    size_t ident_count = 0;
    while (true) {
        err = template_skip_whitespace(in);
        if (err != 0) {
            return err;
        }
        err = stream_next_utf8_cp(in, cp, &cp_len);
        if (err != 0) {
            return err;
        }
        if (cp_len != 1) {
            return ERR_TEMPLATE_INVALID_SYNTAX;
        }
        char leading = (char)cp[0];
        char* str;
        switch (leading) {
            case '"':
                err = template_parse_regular_str(in, &str);
                if (err != 0) {
                    return 0;
                }
                free(str);
                break;
            case '`':
                err = template_parse_backtick_str(in, &str);
                if (err != 0) {
                    return 0;
                }
                free(str);
                break;
            case '-':
                err = stream_next_utf8_cp(in, cp, &cp_len);
                if (err != 0) {
                    return err;
                }
                if (cp_len != 1 || cp[0] != '}') {
                    return ERR_TEMPLATE_INVALID_SYNTAX;
                }
                // delibarate fallthrough
            case '}':
                err = stream_next_utf8_cp(in, cp, &cp_len);
                if (err != 0) {
                    return err;
                }
                if (cp_len != 1 || cp[0] != '}') {
                    return ERR_TEMPLATE_INVALID_SYNTAX;
                }
                return 0;
            default:
                err = template_parse_noop_ident(in, state, leading);
                if (err != 0) {
                    return err;
                }
                if (strcmp("if", state->ident) == 0 || strcmp("range", state->ident) == 0 || strcmp("with", state->ident) == 0) {
                    if (ident_count > 0) {
                        return ERR_TEMPLATE_INVALID_SYNTAX;
                    }
                    stack_push_frame(&state->stack);
                } else if (strcmp("end", state->ident) == 0) {
                    if (ident_count > 0) {
                        return ERR_TEMPLATE_INVALID_SYNTAX;
                    }
                    if (state->stack.len == start_depth) {
                        state->return_reason = RETURN_REASON_END;
                        return 0;
                    }
                    stack_pop_frame(&state->stack);
                } else if (strcmp("else", state->ident) == 0) {
                    if (ident_count > 0) {
                        return ERR_TEMPLATE_INVALID_SYNTAX;
                    }
                    if (state->stack.len == start_depth) {
                        state->return_reason = RETURN_REASON_ELSE;
                        return 0;
                    }
                }
                break;
        }
        ident_count++;
    }
}

int template_noop(stream* in, state* state) {
    unsigned char cp[4];
    size_t cp_len;
    size_t start_depth = state->stack.len;
    int err = 0;
    while (true) {
        err = stream_next_utf8_cp(in, cp, &cp_len);
        if (err != 0) {
            return err;
        }
        if (cp[0] != '{') {
            continue;
        }
        err = stream_next_utf8_cp(in, cp, &cp_len);
        if (err != 0) {
            return err;
        }
        if (cp[0] != '{') {
            continue;
        }
        err = template_pipeline_noop(in, state, start_depth);
        if (err != 0) {
            return err;
        }
        if (state->return_reason == RETURN_REASON_END || state->return_reason == RETURN_REASON_ELSE) {
            return 0;
        }
    }
}

int template_plain(stream* in, state* state);

int template_if(stream* in, state* state) {
    json_value nothing = JSON_NULL;
    bool any_branch = false;
    while (true) {
        stack_push_frame(&state->stack);
        json_value cond;
        int err = template_parse_arg(in, state, &cond);
        if (err != 0) {
            return err;
        }
        err = template_end_pipeline(in, state, &nothing);
        if (err != 0) {
            return err;
        }
        bool cond_empty = is_empty(&cond);
        if (cond_empty || any_branch) {
            err = template_noop(in, state);
            if (err != 0) {
                return err;
            }
        } else {
            any_branch = true;
            err = template_plain(in, state);
            if (err != 0) {
                return err;
            }
        }
        bool is_else = state->return_reason == RETURN_REASON_ELSE;
        if (state->return_reason != RETURN_REASON_BREAK && state->return_reason != RETURN_REASON_CONTINUE) {
            state->return_reason = RETURN_REASON_REGULAR;
        }
        stack_pop_frame(&state->stack);
        if (!is_else) {  // end, break, continue
            return 0;
        }
        err = template_skip_whitespace(in);
        if (err != 0) {
            return err;
        }
        err = template_parse_ident(in, state);
        if (err != 0) {
            return err;
        }
        size_t ident_len = strlen(state->ident);
        if (ident_len == 0) {  // clean else pipeline
            break;
        }
        if (strcmp(state->ident, "if") != 0) {  // no else if
            return ERR_TEMPLATE_INVALID_SYNTAX;
        }
    }
    int err = template_end_pipeline(in, state, &nothing);
    if (err != 0) {
        return err;
    }
    stack_push_frame(&state->stack);
    if (any_branch) {
        err = template_noop(in, state);
    } else {
        err = template_plain(in, state);
    }
    stack_pop_frame(&state->stack);
    if (err != 0) {
        return err;
    }
    if (state->return_reason != RETURN_REASON_END) {
        return ERR_TEMPLATE_INVALID_SYNTAX;
    }
    if (state->return_reason != RETURN_REASON_BREAK && state->return_reason != RETURN_REASON_CONTINUE) {
        state->return_reason = RETURN_REASON_REGULAR;
    }
    return 0;
}

typedef struct {
    int ty;
    size_t count;
    size_t len;
    char** keys;
    union {
        const json_array* arr;
        const hashmap* obj;
    } inner;
} value_iter;

int compare_str(const void* a, const void* b) {
    return strcmp(*((char**)a), *((char**)b));
}

int value_iter_new(value_iter* iter, json_value* val) {
    switch (val->ty) {
        case JSON_TY_ARRAY:
            iter->ty = JSON_TY_ARRAY;
            iter->count = 0;
            iter->len = val->inner.arr.len;
            iter->inner.arr = &val->inner.arr;
            iter->keys = NULL;
            return 0;
        case JSON_TY_OBJECT:
            iter->ty = JSON_TY_OBJECT;
            iter->count = 0;
            iter->len = val->inner.obj.count;
            iter->inner.obj = &val->inner.obj;
            iter->keys = (char**)hashmap_keys(&val->inner.obj);
            qsort(iter->keys, iter->len, sizeof(char*), compare_str);
            return 0;
    }
    return ERR_TEMPLATE_NO_ITERABLE;
}

void value_iter_free(value_iter* iter) {
    if (iter->ty == JSON_TY_OBJECT) {
        free(iter->keys);
    }
}

typedef struct {
    size_t idx;
    json_value key;
    json_value val;
} value_iter_out;

bool value_iter_next(value_iter* iter, value_iter_out* out) {
    const json_array* arr;
    const hashmap* obj;
    if (iter->count >= iter->len) {
        return false;
    }
    switch (iter->ty) {
        case JSON_TY_ARRAY:
            arr = iter->inner.arr;
            out->idx = iter->count;
            out->key.ty = JSON_TY_NUMBER;
            out->key.inner.num = iter->count;
            out->val = arr->data[iter->count];
            iter->count++;
            return true;
        case JSON_TY_OBJECT:
            obj = iter->inner.obj;
            out->idx = iter->count;
            out->key.ty = JSON_TY_STRING;
            char* key = iter->keys[iter->count];
            out->key.inner.str = key;
            json_value* val;
            if (!hashmap_get(obj, key, (const void**)&val)) {
                assert("key removed from hashmap during iteration");
            }
            out->val = *val;
            iter->count++;
            return true;
    }
    assert(0);
}

int template_range(stream* in, state* state) {
    json_value nothing = JSON_NULL;
    json_value arg;
    stack_push_frame(&state->stack); // holds arg if var def
    int err = template_parse_arg(in, state, &arg);
    if (err != 0) {
        return err;
    }
    if (arg.ty != JSON_TY_ARRAY && arg.ty != JSON_TY_OBJECT) {
        return ERR_TEMPLATE_NO_ITERABLE;
    }
    if (is_empty(&arg)) {
        err = template_end_pipeline(in, state, &nothing);
        if (err != 0) {
            return err;
        }
        err = template_noop(in, state);
        if (err != 0) {
            return err;
        }
        bool is_else = state->return_reason == RETURN_REASON_ELSE;
        state->return_reason = RETURN_REASON_REGULAR;
        if (!is_else) {
            stack_pop_frame(&state->stack);
            return 0;
        }
        err = template_end_pipeline(in, state, &nothing);
        if (err != 0) {
            return err;
        }
        err = template_plain(in, state);
        if (err != 0) {
            return err;
        }
        stack_pop_frame(&state->stack);
        state->return_reason = RETURN_REASON_REGULAR;
        return 0;
    }

    json_value* current = state->dot;
    long pre_pos;
    err = stream_pos(in, &pre_pos);
    if (err != 0) {
        return err;
    }
    value_iter iter;
    err = value_iter_new(&iter, &arg);
    if (err != 0) {
        return err;
    }
    value_iter_out out;
    while (value_iter_next(&iter, &out)) {
        err = template_end_pipeline(in, state, &nothing);
        if (err != 0) {
            goto cleanup;
        }
        // post range pipeline
        state->dot = &out.val;
        stack_push_frame(&state->stack);
        err = template_plain(in, state);
        if (err != 0) {
            goto cleanup;
        }
        stack_pop_frame(&state->stack);
        long post_pos;
        err = stream_pos(in, &post_pos);
        if (err != 0) {
            goto cleanup;
        }
        err = stream_seek(in, pre_pos - post_pos);
        if (err != 0) {
            goto cleanup;
        }
        if (state->return_reason == RETURN_REASON_BREAK) {
            break;
        }
        state->return_reason = RETURN_REASON_REGULAR;
    }
    stack_pop_frame(&state->stack);
    // find the terminating end/else pipeline.
    // each iteration of the iteration loop could
    // be terminated early by a break/continue statement.
    err = template_end_pipeline(in, state, &nothing);
    if (err != 0) {
        goto cleanup;
    }
    err = template_noop(in, state);
    if (err != 0) {
        goto cleanup;
    }
    bool is_else = state->return_reason == RETURN_REASON_ELSE;
    state->return_reason = RETURN_REASON_REGULAR;
    state->dot = current;
    if (!is_else) {
        goto cleanup;
    }
    err = template_end_pipeline(in, state, &nothing);
    if (err != 0) {
        goto cleanup;
    }
    err = template_noop(in, state);
    if (err != 0) {
        goto cleanup;
    }
    if (state->return_reason != RETURN_REASON_END) {
        err = ERR_TEMPLATE_INVALID_SYNTAX;
        goto cleanup;
    }
    state->return_reason = RETURN_REASON_REGULAR;
cleanup:
    value_iter_free(&iter);
    return err;
}

int template_with(stream* in, state* state) {
    json_value nothing = JSON_NULL;
    bool any_branch = false;
    while (true) {
        stack_push_frame(&state->stack);
        json_value arg;
        int err = template_parse_arg(in, state, &arg);
        if (err == ERR_TEMPLATE_KEY_UNKNOWN) {
            arg = JSON_NULL;
            err = 0;
        }
        if (err != 0) {
            return err;
        }
        err = template_end_pipeline(in, state, &nothing);
        if (err != 0) {
            return err;
        }
        bool arg_empty = is_empty(&arg);
        if (arg_empty) {
            err = template_noop(in, state);
            if (err != 0) {
                return err;
            }
        } else {
            any_branch = true;
            json_value* previous = state->dot;
            state->dot = &arg;
            err = template_plain(in, state);
            if (err != 0) {
                return err;
            }
            state->dot = previous;
        }
        bool is_else = state->return_reason == RETURN_REASON_ELSE;
        if (state->return_reason != RETURN_REASON_BREAK && state->return_reason != RETURN_REASON_CONTINUE) {
            state->return_reason = RETURN_REASON_REGULAR;
        }
        stack_pop_frame(&state->stack);
        if (!is_else) {  // end, break, continue
            return 0;
        }
        err = template_skip_whitespace(in);
        if (err != 0) {
            return err;
        }
        err = template_parse_ident(in, state);
        if (err != 0) {
            return err;
        }
        size_t ident_len = strlen(state->ident);
        if (ident_len == 0) {  // clean else pipeline
            break;
        }
        if (strcmp(state->ident, "with") != 0) {  // no else with
            return ERR_TEMPLATE_INVALID_SYNTAX;
        }
    }
    int err = template_end_pipeline(in, state, &nothing);
    if (err != 0) {
        return err;
    }
    stack_push_frame(&state->stack);
    if (any_branch) {
        err = template_noop(in, state);
    } else {
        err = template_plain(in, state);
    }
    stack_pop_frame(&state->stack);
    if (err != 0) {
        return err;
    }
    if (state->return_reason != RETURN_REASON_END) {
        return ERR_TEMPLATE_INVALID_SYNTAX;
    }
    if (state->return_reason != RETURN_REASON_BREAK && state->return_reason != RETURN_REASON_CONTINUE) {
        state->return_reason = RETURN_REASON_REGULAR;
    }
    return 0;
}

int template_dispatch_keyword(stream* in, state* state) {
    int err = template_parse_ident(in, state);
    if (err != 0) {
        return err;
    }
    if (strcmp("if", state->ident) == 0) {
        return template_if(in, state);
    }
    if (strcmp("range", state->ident) == 0) {
        return template_range(in, state);
    }
    if (strcmp("with", state->ident) == 0) {
        return template_with(in, state);
    }
    if (strcmp("end", state->ident) == 0) {
        // only used in the non-noop case
        // in the noop case template_noop
        // takes care of the matching "end"
        state->return_reason = RETURN_REASON_END;
        return 0;
    }
    if (strcmp("else", state->ident) == 0) {
        // only used in the non-noop case
        // in the noop case template_noop
        // takes care of the matching "else"
        state->return_reason = RETURN_REASON_ELSE;
        return 0;
    }
    if (strcmp("break", state->ident) == 0) {
        // only used in the non-noop case
        // the noop case doesn't care about
        // break pipelines.
        state->return_reason = RETURN_REASON_BREAK;
        return 0;
    }
    if (strcmp("continue", state->ident) == 0) {
        // only used in the non-noop case
        // the noop case doesn't care about
        // continue pipelines.
        state->return_reason = RETURN_REASON_CONTINUE;
        return 0;
    }
    return ERR_TEMPLATE_KEYWORD_UNKNOWN;
}

int template_dispatch_func(stream* in, state* state, json_value* result) {
    int err = template_parse_ident(in, state);
    if (err != 0) {
        return err;
    }
    return ERR_TEMPLATE_FUNC_UNKNOWN;
}

int template_dispatch_pipeline(stream* in, state* state, json_value* result) {
    unsigned char cp[4];
    size_t cp_len;
    int err = template_skip_whitespace(in);
    if (err != 0) {
        return err;
    }
    err = stream_next_utf8_cp(in, cp, &cp_len);
    if (err != 0) {
        return err;
    }
    if (cp_len != 1) {
        return ERR_TEMPLATE_INVALID_SYNTAX;
    }
    if (cp[0] == '$') {
        long pre_pos;
        err = stream_pos(in, &pre_pos);
        if (err != 0) {
            return err;
        }
        err = template_parse_var_mutation(in, state, result);
        if (err != ERR_TEMPLATE_NO_MUTATION) {
            // top level var assignments/definitions have their result discarded
            *result = JSON_NULL;
            return err;
        }
        long post_pos;
        err = stream_pos(in, &post_pos);
        if (err != 0) {
            return err;
        }
        err = stream_seek(in, pre_pos - post_pos);
        if (err != 0) {
            return err;
        }
    }
    err = template_parse_value(in, state, result, cp[0]);
    switch (err) {
        case 0:
            return 0;
        case ERR_TEMPLATE_NO_LITERAL:
            break;
        case ERR_TEMPLATE_LITERAL_DASH:
            return stream_seek(in, -2);
        default:
            return err;
    }
    if (isalpha(cp[0])) {
        err = stream_seek(in, -1);
        if (err != 0) {
            return err;
        }
        err = template_dispatch_keyword(in, state);
        switch (err) {
            case 0:
                return 0;
            case ERR_TEMPLATE_KEYWORD_UNKNOWN:
                break;
            default:
                return err;
        }
        err = stream_seek(in, -strlen(state->ident));
        if (err != 0) {
            return err;
        }
        err = template_dispatch_func(in, state, result);
        if (err != 0) {
            return err;
        }
    }
    // when skip whitespace stops directly at the end
    if (cp[0] == '}') {
        return stream_seek(in, -1);
    }
    return ERR_TEMPLATE_INVALID_SYNTAX;
}

int template_invoke_pipeline(stream* in, state* state) {
    json_value result = JSON_NULL;
    int err = template_dispatch_pipeline(in, state, &result);
    if (err != 0) {
        return err;
    }
    if (state->return_reason != RETURN_REASON_REGULAR) {
        return 0;
    }
    return template_end_pipeline(in, state, &result);
}

int template_start_pipeline(stream* in, state* state) {
    unsigned char cp[4];
    size_t cp_len;
    int err = stream_next_utf8_cp(in, cp, &cp_len);
    if (err != 0) {
        return err;
    }
    if (cp[0] != '-') {
        err = stream_seek(in, -cp_len);
        if (err != 0) {
            return err;
        }
        return template_invoke_pipeline(in, state);
    }
    size_t off = cp_len;
    err = stream_next_utf8_cp(in, cp, &cp_len);
    if (err != 0) {
        return err;
    }
    if (cp[0] != ' ') {
        err = stream_seek(in, -cp_len - off);
        if (err != 0) {
            return err;
        }
        return template_invoke_pipeline(in, state);
    }
    state->out.len = state->out_nospace;
    return template_invoke_pipeline(in, state);
}

int template_plain(stream* in, state* state) {
    unsigned char cp[4];
    size_t cp_len;
    int err = 0;
    while (true) {
        err = stream_next_utf8_cp(in, cp, &cp_len);
        if (err != 0) {
            return err;
        }
        if (cp[0] != '{') {
            buf_append(&state->out, (const char*)cp, cp_len);
            if (!isspace(cp[0])) {
                state->out_nospace = state->out.len;
            }
            continue;
        }
        err = stream_next_utf8_cp(in, cp, &cp_len);
        if (err != 0) {
            return err;
        }
        if (cp[0] != '{') {
            char brace_open = '{';
            buf_append(&state->out, &brace_open, 1);
            buf_append(&state->out, (const char*)cp, cp_len);
            if (!isspace(cp[0])) {
                state->out_nospace = state->out.len;
            }
            continue;
        }
        err = template_start_pipeline(in, state);
        if (err != 0) {
            return err;
        }
        if (state->return_reason != RETURN_REASON_REGULAR) {
            if (state->stack.len == 1) {
                return ERR_TEMPLATE_KEYWORD_UNEXPECTED;
            } else {
                state->out_nospace = state->out.len;
            }
            return 0;
        }
        state->out_nospace = state->out.len;
    }
}

void template_init_stack(stack* s, json_value* dot) {
    stack_new(s);
    stack_push_frame(s);
    json_value* dollar = malloc(sizeof(json_value));
    assert(dollar);
    json_value_copy(dollar, dot);
    char* dollar_str = malloc(1);
    assert(dollar_str);
    *dollar_str = 0;
    stack_set_var(s, dollar_str, dollar);
}

int template_eval(const char* tpl, size_t n, json_value* dot, char** out) {
    stream in;
    stream_open_memory(&in, tpl, n);
    state state;
    state.dot = dot;
    state.scratch_val = JSON_NULL;
    state.out_nospace = 0;
    state.return_reason = RETURN_REASON_REGULAR;
    buf_init(&state.out);
    template_init_stack(&state.stack, dot);
    int err = template_plain(&in, &state);
    if (err == EOF && state.stack.len == 1) {
        err = 0;
    }
    buf_append(&state.out, "\0", 1);
    *out = state.out.data;
    stack_free(&state.stack);
    json_value_free(&state.scratch_val);
    int close_err = stream_close(&in);
    if (close_err != 0) {
        free(*out);
        return close_err;
    }
    return err;
}

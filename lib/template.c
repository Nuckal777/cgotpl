#include "template.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "encode.h"
#include "func.h"
#include "json.h"
#include "map.h"
#include "stream.h"

#define STACK_REFS_CAP 4

typedef struct {
    hashmap data;
    size_t refs_len;
    char* refs[STACK_REFS_CAP];
} stack_frame;

typedef struct {
    size_t len;
    size_t cap;
    stack_frame* frames;
} stack;

#define DEFAULT_STACK_CAP 8

void stack_new(stack* s) {
    s->len = 0;
    s->cap = DEFAULT_STACK_CAP;
    s->frames = malloc(sizeof(stack_frame) * s->cap);
    assert(s->frames);
}

void stack_free_entry(entry* entry, void* userdata) {
    stack_frame* frame = (stack_frame*)userdata;
    for (size_t i = 0; i < frame->refs_len; i++) {
        if (strcmp(entry->key, frame->refs[i]) == 0) {
            return;
        }
    }
    free(entry->key);
    json_value_free(entry->value);
    free(entry->value);
}

void stack_push_frame(stack* s) {
    if (s->len == s->cap) {
        s->cap = s->cap * 3 / 2;
        s->frames = realloc(s->frames, sizeof(stack_frame) * s->cap);
        assert(s->frames);
    }
    hashmap_new(&s->frames[s->len].data, hashmap_strcmp, hashmap_strlen, HASH_FUNC_DJB2);
    s->frames[s->len].refs_len = 0;
    s->len++;
}

void stack_pop_frame(stack* s) {
    assert(s->len > 0);
    hashmap_iter(&s->frames[s->len - 1].data, &s->frames[s->len - 1], stack_free_entry);
    hashmap_free(&s->frames[s->len - 1].data);
    s->len--;
}

void stack_free(stack* s) {
    while (s->len > 0) {
        stack_pop_frame(s);
    }
    free(s->frames);
}

void stack_set_var(stack* s, char* var, json_value* value) {
    stack_frame* current = &s->frames[s->len - 1];
    entry previous = hashmap_insert(&current->data, (void*)var, value);
    if (previous.exists) {
        stack_free_entry(&previous, current);
    }
    for (size_t i = 0; i < current->refs_len; i++) {
        if (strcmp(var, current->refs[i]) == 0) {
            for (size_t j = i + 1; j < current->refs_len; j++) {
                current->refs[j - 1] = current->refs[j];
            }
            current->refs_len--;
            return;
        }
    }
}

int stack_set_ref(stack* s, char* var, json_value* value) {
    stack_frame* current = &s->frames[s->len - 1];
    if (current->refs_len == STACK_REFS_CAP) {
        return ERR_BUF_OVERFLOW;
    }
    for (size_t i = 0; i < current->refs_len; i++) {
        if (strcmp(var, current->refs[i]) == 0) {
            // no match is possible, if the key is not already a ref
            hashmap_insert(&current->data, (void*)var, value);
            return 0;
        }
    }
    entry previous = hashmap_insert(&current->data, (void*)var, value);
    if (previous.exists) {
        stack_free_entry(&previous, current);
    }
    current->refs[current->refs_len] = var;
    current->refs_len++;
    return 0;
}

const json_value* stack_find_var(stack* s, const char* var) {
    for (ptrdiff_t i = s->len - 1; i >= 0; i--) {
        const json_value* out;
        if (hashmap_get(&s->frames[i].data, var, (const void**)&out)) {
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
    json_value* dot;
    buf out;
    size_t out_nospace;
    size_t range_depth;
    stack stack;
    hashmap define_locs;
    hashmap funcmap;
    int return_reason;
    char ident[STATE_IDENT_CAP];
    bool eval_block;
} state;

int template_skip_whitespace(stream* in) {
    unsigned char cp[4];
    size_t cp_len;
    while (true) {
        int err = stream_next_utf8_cp(in, cp, &cp_len);
        if (err == EOF) {
            return ERR_TEMPLATE_UNEXPECTED_EOF;
        }
        if (err) {
            return err;
        }
        if (cp_len != 1 || !isspace(cp[0])) {
            return stream_seek(in, -cp_len);
        }
    }
}

// cp needs to hold at least 4 byte
int template_next_nonspace(stream* in, unsigned char* cp, size_t* cp_len) {
    while (true) {
        int err = stream_next_utf8_cp(in, cp, cp_len);
        if (err == EOF) {
            return ERR_TEMPLATE_UNEXPECTED_EOF;
        }
        if (err) {
            return err;
        }
        if (*cp_len != 1 || !isspace(cp[0])) {
            return 0;
        }
    }
}

int template_parse_number(stream* in, double* out) {
    unsigned char cp[4];
    size_t cp_len;
    char buf[128];
    size_t buf_idx = 0;
    while (buf_idx < sizeof(buf)) {
        int err = stream_next_utf8_cp(in, cp, &cp_len);
        if (err) {
            return err;
        }
        if (cp_len != 1) {
            return ERR_TEMPLATE_INVALID_SYNTAX;
        }
        if (!isalnum(cp[0]) && cp[0] != '+' && cp[0] != '-' && cp[0] != '.') {
            buf[buf_idx] = 0;
            char* end;
            *out = strtod(buf, &end);
            if (errno == ERANGE) {
                errno = 0;
                return ERR_TEMPLATE_INVALID_SYNTAX;
            }
            if (buf == end) {
                return ERR_TEMPLATE_INVALID_SYNTAX;
            }
            if (*out == -0) {
                *out = 0;
            }
            ptrdiff_t num_len = end - buf;
            return stream_seek(in, num_len - buf_idx - 1);
        }
        buf[buf_idx] = cp[0];
        buf_idx++;
    }
    return ERR_BUF_OVERFLOW;
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
        if (err) {
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
    if (err) {
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
        if (err) {
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
                if (err) {
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
                            if (err) {
                                goto cleanup;
                            }
                            if (cp_len != 1) {
                                err = ERR_TEMPLATE_INVALID_ESCAPE;
                                goto cleanup;
                            }
                            if (!((cp[0] >= '0' && cp[0] <= '9') || (cp[0] >= 'a' && cp[0] <= 'f') || (cp[0] >= 'A' && cp[0] <= 'F'))) {
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
    if (err) {
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
        if (err) {
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
    return ERR_BUF_OVERFLOW;
}

int template_skip_path_expr(stream* in) {
    unsigned char cp[4];
    size_t cp_len;
    while (true) {
        int err = stream_next_utf8_cp(in, cp, &cp_len);
        if (err) {
            return err;
        }
        if (cp_len != 1) {
            continue;
        }
        if (!isalnum(cp[0]) && cp[0] != '.') {
            break;
        }
    }
    return stream_seek(in, -1);
}

int template_parse_path_expr_recurse(stream* in, state* state, json_value* result) {
    unsigned char cp[4];
    size_t cp_len;
    int err = stream_next_utf8_cp(in, cp, &cp_len);
    if (err) {
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
    if (err) {
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
        err = template_skip_path_expr(in);
        if (err) {
            return err;
        }
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
    if (err) {
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
        err = template_skip_path_expr(in);
        if (err) {
            return err;
        }
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
    if (err) {
        return err;
    }
    if (cp_len > 1 || !isspace(cp[0])) {  // '$' is already consumed => named '$ABC' var
        err = stream_seek(in, -cp_len);
        if (err) {
            return err;
        }
        err = template_parse_ident(in, state);
        if (err) {
            return err;
        }
    } else {  // '$' var
        state->ident[0] = 0;
    }
    const json_value* out = stack_find_var(&state->stack, state->ident);
    if (out == NULL) {
        return ERR_TEMPLATE_VAR_UNKNOWN;
    }
    *result = *out;
    return 0;
}

// seeks back in-front of first when returning ERR_TEMPLATE_NO_VALUE
int template_parse_value(stream* in, state* state, tracked_value* result, unsigned char first) {
    unsigned char cp[4];
    size_t cp_len;
    int err = 0;
    double val;
    size_t seek_back = -1;
    size_t identifier_len = 0;
    tracked_value_free(result);
    result->is_heap = false;
    switch (first) {
        case 't':
            err = template_parse_ident(in, state);
            if (err) {
                return err;
            }
            bool is_true = strcmp("rue", state->ident) == 0;
            if (is_true) {
                result->val.ty = JSON_TY_TRUE;
                return 0;
            }
            identifier_len = strlen(state->ident);
            err = stream_seek(in, -identifier_len - 1);
            if (err) {
                return err;
            }
            return ERR_TEMPLATE_NO_VALUE;
        case 'f':
            err = template_parse_ident(in, state);
            if (err) {
                return err;
            }
            bool is_false = strcmp("alse", state->ident) == 0;
            if (is_false) {
                result->val.ty = JSON_TY_FALSE;
                return 0;
            }
            identifier_len = strlen(state->ident);
            err = stream_seek(in, -identifier_len - 1);
            if (err) {
                return err;
            }
            return ERR_TEMPLATE_NO_VALUE;
        case 'n':
            err = template_parse_ident(in, state);
            if (err) {
                return err;
            }
            bool is_nil = strcmp("il", state->ident) == 0;
            if (is_nil) {
                result->val.ty = JSON_TY_NULL;
                return 0;
            }
            identifier_len = strlen(state->ident);
            err = stream_seek(in, -identifier_len - 1);
            if (err) {
                return err;
            }
            return ERR_TEMPLATE_NO_VALUE;
        case '$':
            return template_parse_var_value(in, state, &result->val);
        case '"':
            err = template_parse_regular_str(in, &result->val.inner.str);
            if (err) {
                return err;
            }
            result->val.ty = JSON_TY_STRING;
            result->is_heap = true;
            return 0;
        case '`':
            err = template_parse_backtick_str(in, &result->val.inner.str);
            if (err) {
                return err;
            }
            result->val.ty = JSON_TY_STRING;
            result->is_heap = true;
            return 0;
        case '-':
            err = stream_next_utf8_cp(in, cp, &cp_len);
            if (err) {
                return err;
            }
            if (cp_len != 1) {
                return ERR_TEMPLATE_INVALID_SYNTAX;
            }
            if (cp[0] == '}') {
                err = stream_seek(in, -2);
                if (err) {
                    return err;
                }
                return ERR_TEMPLATE_NO_VALUE;
            }
            if (!isdigit(cp[0])) {
                return ERR_TEMPLATE_INVALID_SYNTAX;
            }
            seek_back = -2;
            // deliberate fallthrough
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
            if (err) {
                return err;
            }
            result->val.ty = JSON_TY_NUMBER;
            return template_parse_number(in, &result->val.inner.num);
        case '.':
            return template_parse_path_expr(in, state, &result->val);
        default:
            err = stream_seek(in, -1);
            if (err) {
                return err;
            }
            return ERR_TEMPLATE_NO_VALUE;
    }
}

#define TEMPLATE_PARSE_EXPR_NO_VAR_MUT 0x01
#define TEMPLATE_PARSE_EXPR_NO_PIPE 0x02
#define TEMPLATE_PARSE_EXPR_FORCE_SPACE 0x04

int template_parse_expr(stream* in, state* state, tracked_value* result, int flags);

int template_parse_var_mutation(stream* in, state* state, tracked_value* result) {
    unsigned char cp[4];
    size_t cp_len;
    // parse var name
    int err = stream_next_utf8_cp(in, cp, &cp_len);
    if (err) {
        return err;
    }
    if (cp_len > 1 || !isspace(cp[0])) {  // '$' is already consumed => named '$ABC' var
        err = stream_seek(in, -cp_len);
        if (err) {
            return err;
        }
        err = template_parse_ident(in, state);
        if (err) {
            return err;
        }
    } else {  // '$' var
        state->ident[0] = 0;
    }
    err = template_next_nonspace(in, cp, &cp_len);
    if (err) {
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
            if (err) {
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
    if (err) {
        return err;
    }
    char* ident_copy = strdup(state->ident);
    err = template_parse_expr(in, state, result, TEMPLATE_PARSE_EXPR_NO_VAR_MUT);  // result holds right side of assignment
    if (err) {
        free(ident_copy);
        return err;
    }
    // nil cannot be assigned in go
    if (result->val.ty == JSON_TY_NULL) {
        free(ident_copy);
        return ERR_TEMPLATE_KEYWORD_UNEXPECTED;
    }
    json_value* value_copy = malloc(sizeof(json_value));
    assert(value_copy);
    if (result->is_heap) {
        *value_copy = result->val;
        result->is_heap = false;
    } else {
        // in case of $var=$var the second $var would be returned as result, although
        // it is freed in stack_set_var
        json_value_copy(value_copy, &result->val);
        result->val = *value_copy;
    }
    result->is_heap = false;
    stack_set_var(&state->stack, ident_copy, value_copy);
    return 0;
}

int template_parse_value_with_var_mut(stream* in, state* state, tracked_value* result, unsigned char first) {
    if (first == '$') {
        long pre_pos;
        int err = stream_pos(in, &pre_pos);
        if (err) {
            return err;
        }
        err = template_parse_var_mutation(in, state, result);
        if (err != ERR_TEMPLATE_NO_MUTATION) {
            return err;
        }
        err = stream_set_pos(in, pre_pos);
        if (err) {
            return err;
        }
    }
    return template_parse_value(in, state, result, first);
}

int template_parse_parenthesis(stream* in, state* state, tracked_value* result) {
    int err = template_skip_whitespace(in);
    if (err) {
        return err;
    }
    err = template_parse_expr(in, state, result, 0);
    if (err) {
        return err;
    }
    unsigned char cp[4];
    size_t cp_len;
    err = template_next_nonspace(in, cp, &cp_len);
    if (err) {
        return err;
    }
    if (cp_len != 1 || cp[0] != ')') {
        return ERR_TEMPLATE_INVALID_SYNTAX;
    }
    return 0;
}

int template_dispatch_func(stream* in, state* state, tracked_value* piped, tracked_value* result);

int template_parse_pipe(stream* in, state* state, tracked_value* result) {
    unsigned char cp[4];
    size_t cp_len;
    int err = template_next_nonspace(in, cp, &cp_len);
    if (err) {
        return err;
    }
    if (cp_len != 1) {
        return ERR_TEMPLATE_INVALID_SYNTAX;
    }
    if (cp[0] != '|') {
        return stream_seek(in, -cp_len);
    }
    err = template_skip_whitespace(in);
    if (err) {
        return err;
    }
    tracked_value last = *result;
    *result = TRACKED_NULL;
    err = template_dispatch_func(in, state, &last, result);
    if (err) {
        return err;
    }
    return template_parse_pipe(in, state, result);
}

int template_parse_expr(stream* in, state* state, tracked_value* result, int flags) {
    unsigned char cp[4];
    size_t cp_len;
    int err = 0;
    if (flags & TEMPLATE_PARSE_EXPR_FORCE_SPACE) {
        err = stream_next_utf8_cp(in, cp, &cp_len);
        if (err) {
            return err;
        }
        if (cp_len != 1) {
            return ERR_TEMPLATE_INVALID_SYNTAX;
        }
        // at least past certain keywords a parenthesis is allowed as well
        if (cp[0] != '(' && !isspace(cp[0])) {
            return ERR_TEMPLATE_INVALID_SYNTAX;
        }
        if (cp[0] != '(') {
            err = template_next_nonspace(in, cp, &cp_len);
        }
    } else {
        err = template_next_nonspace(in, cp, &cp_len);
    }
    if (err) {
        return err;
    }
    if (cp_len != 1) {
        return ERR_TEMPLATE_INVALID_SYNTAX;
    }
    if (cp[0] == '(') {
        err = template_parse_parenthesis(in, state, result);
        if (err) {
            return err;
        }
        if (flags & TEMPLATE_PARSE_EXPR_NO_PIPE) {
            return 0;
        }
        return template_parse_pipe(in, state, result);
    }
    if (flags & TEMPLATE_PARSE_EXPR_NO_VAR_MUT) {
        err = template_parse_value(in, state, result, cp[0]);
    } else {
        err = template_parse_value_with_var_mut(in, state, result, cp[0]);
    }
    if (err == 0) {
        if (flags & TEMPLATE_PARSE_EXPR_NO_PIPE) {
            return 0;
        }
        return template_parse_pipe(in, state, result);
    }
    if (err != ERR_TEMPLATE_NO_VALUE) {
        return err;
    }
    if (!isalpha(cp[0])) {
        // template_parse_expr() is invoked by template_dispatch_func().
        // The latter stops parsing arguments on ERR_TEMPLATE_NO_VALUE.
        return ERR_TEMPLATE_NO_VALUE;
    }
    err = template_dispatch_func(in, state, NULL, result);
    if (err) {
        return err;
    }
    if (flags & TEMPLATE_PARSE_EXPR_NO_PIPE) {
        return 0;
    }
    return template_parse_pipe(in, state, result);
}

int template_parse_arg(stream* in, state* state, tracked_value* arg) {
    unsigned char cp[4];
    size_t cp_len;
    int err = template_next_nonspace(in, cp, &cp_len);
    if (err) {
        return err;
    }
    if (cp_len != 1) {
        return ERR_TEMPLATE_INVALID_SYNTAX;
    }
    if (cp[0] == '(') {
        return template_parse_parenthesis(in, state, arg);
    }
    return template_parse_value_with_var_mut(in, state, arg, cp[0]);
}

int template_end_pipeline(stream* in, state* state, json_value* result) {
    unsigned char cp[4];
    size_t cp_len;
    int err = template_next_nonspace(in, cp, &cp_len);
    if (err) {
        return err;
    }
    if (cp_len != 1) {
        return ERR_TEMPLATE_INVALID_SYNTAX;
    }
    bool trim = false;
    switch (cp[0]) {
        case '-':
            err = stream_next_utf8_cp(in, cp, &cp_len);
            if (err) {
                return err;
            }
            if (cp_len != 1 || cp[0] != '}') {
                return ERR_TEMPLATE_INVALID_SYNTAX;
            }
            trim = true;
            // deliberate fallthrough
        case '}':
            err = stream_next_utf8_cp(in, cp, &cp_len);
            if (err) {
                return err;
            }
            if (cp_len != 1 || cp[0] != '}') {
                return ERR_TEMPLATE_INVALID_SYNTAX;
            }
            if (result->ty != JSON_TY_NULL) {
                err = sprintval(&state->out, result, NULL_STR_NIL);
                if (err) {
                    return err;
                }
            }
            if (trim) {
                err = template_skip_whitespace(in);
                if (err == ERR_TEMPLATE_UNEXPECTED_EOF) {
                    return 0;
                }
                if (err) {
                    return err;
                }
            }
            return 0;
        default:
            return ERR_TEMPLATE_INVALID_SYNTAX;
    }
}

int template_parse_noop_ident(stream* in, state* state, char leading) {
    unsigned char cp[4];
    size_t cp_len;
    int err = 0;
    state->ident[0] = leading;
    for (size_t i = 1; i < STATE_IDENT_CAP; i++) {
        err = stream_next_utf8_cp(in, cp, &cp_len);
        if (err) {
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
    return ERR_BUF_OVERFLOW;
}

int template_parse_define_name(stream* in, char** name) {
    unsigned char cp[4];
    size_t cp_len;
    int err = stream_next_utf8_cp(in, cp, &cp_len);
    if (err) {
        return err;
    }
    if (cp_len != 1 || !isspace(cp[0])) {
        return ERR_TEMPLATE_INVALID_SYNTAX;
    }
    err = template_next_nonspace(in, cp, &cp_len);
    if (err) {
        return err;
    }
    if (cp_len != 1) {
        return ERR_TEMPLATE_INVALID_SYNTAX;
    }
    switch (cp[0]) {
        case '"':
            err = template_parse_regular_str(in, name);
            break;
        case '`':
            err = template_parse_backtick_str(in, name);
            break;
        default:
            return ERR_TEMPLATE_INVALID_SYNTAX;
    }
    return err;
}

// 2 cases:
// - opening parenthesis => skip everything, consuming closing parenthesis => done
// - no paranthesis (any space, brace, paren, pipe is terminal)
//   - value <space> => trivial
//   - value <terminal> => seek back => the next invocation needs to see the terminal
// both cases are entwined here
// - depth == 0 is unreachable with an opening paren, causing everything to to skipped except except the last closing paren
int template_skip_expr(stream* in, long* start_pos) {
    unsigned char cp[4];
    size_t cp_len;
    int err = template_next_nonspace(in, cp, &cp_len);
    if (err) {
        return err;
    }
    err = stream_pos(in, start_pos);
    if (err) {
        return err;
    }
    (*start_pos) -= cp_len;
    size_t depth = 0;
    bool opening_paren = false;
    if (cp_len == 1) {
        if (cp[0] == ')' || cp[0] == '}' || cp[0] == '|') {
            return ERR_TEMPLATE_NO_VALUE;
        }
        if (cp[0] == '"') {
            char* str;
            err = template_parse_regular_str(in, &str);
            if (err) {
                return err;
            }
            free(str);
            return 0;
        }
        if (cp[0] == '`') {
            char* str;
            err = template_parse_backtick_str(in, &str);
            if (err) {
                return err;
            }
            free(str);
            return 0;
        }
        if (cp[0] == '(') {
            opening_paren = true;
            depth++;
        }
    }
    while (true) {
        int err = stream_next_utf8_cp(in, cp, &cp_len);
        if (err) {
            return err;
        }
        if (cp_len != 1) {
            continue;
        }
        if (depth == 0) {
            if (isspace(cp[0]) || cp[0] == ')' || cp[0] == '|') {
                return stream_seek(in, -cp_len);
            }
            if (cp[0] == '"' || cp[0] == '`') {
                return ERR_TEMPLATE_INVALID_SYNTAX;
            }
        }
        if (cp[0] == ')') {
            if (opening_paren && depth == 1) {
                return 0;
            }
            depth--;
            continue;
        }
        if (cp[0] == '}') {
            return ERR_TEMPLATE_NO_VALUE;
        }
        if (cp[0] == '"') {
            char* str;
            err = template_parse_regular_str(in, &str);
            if (err) {
                return err;
            }
            free(str);
            continue;
        }
        if (cp[0] == '`') {
            char* str;
            err = template_parse_backtick_str(in, &str);
            if (err) {
                return err;
            }
            free(str);
            continue;
        }
        if (cp[0] == '(') {
            depth++;
            continue;
        }
    }
}

int template_pipeline_noop_register_block(stream* in, state* state) {
    char* name;
    int err = template_parse_define_name(in, &name);
    if (err) {
        return err;
    }
    unsigned char cp[4];
    size_t cp_len;
    err = stream_next_utf8_cp(in, cp, &cp_len);
    if (err) {
        goto cleanup;
    }
    if (cp_len != 1 || !isspace(cp[0])) {
        err = ERR_TEMPLATE_INVALID_SYNTAX;
        goto cleanup;
    }
    err = template_skip_whitespace(in);
    if (err) {
        goto cleanup;
    }
    long discard;
    err = template_skip_expr(in, &discard);
    if (err) {
        goto cleanup;
    }
    long pre_end;
    err = stream_pos(in, &pre_end);
    if (err) {
        goto cleanup;
    }
    json_value nothing = JSON_NULL;
    err = template_end_pipeline(in, state, &nothing);
    if (err) {
        goto cleanup;
    }
    long pos;
    err = stream_pos(in, &pos);
    if (err) {
        goto cleanup;
    }
    assert(sizeof(long) == sizeof(void*));
    entry previous = hashmap_insert(&state->define_locs, name, (void*)pos);
    if (previous.exists) {
        free(previous.key);
    }
    err = stream_set_pos(in, pre_end);
    if (err) {
        return err;
    }
cleanup:
    if (err) {
        free(name);
    }
    return err;
}

int template_pipeline_noop(stream* in, state* state, size_t* depth, bool eval_define) {
    unsigned char cp[4];
    size_t cp_len;
    int err = stream_next_utf8_cp(in, cp, &cp_len);
    if (err) {
        return err;
    }
    if (cp_len != 1) {
        return ERR_TEMPLATE_INVALID_SYNTAX;
    }
    if (cp[0] != '-') {
        err = stream_seek(in, -1);
        if (err) {
            return err;
        }
    }
    size_t ident_count = 0;
    while (true) {
        err = template_next_nonspace(in, cp, &cp_len);
        if (err) {
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
                if (err) {
                    return 0;
                }
                free(str);
                break;
            case '`':
                err = template_parse_backtick_str(in, &str);
                if (err) {
                    return 0;
                }
                free(str);
                break;
            case '-':
                err = stream_next_utf8_cp(in, cp, &cp_len);
                if (err) {
                    return err;
                }
                if (cp_len != 1 || cp[0] != '}') {
                    return ERR_TEMPLATE_INVALID_SYNTAX;
                }
                // deliberate fallthrough
            case '}':
                err = stream_next_utf8_cp(in, cp, &cp_len);
                if (err) {
                    return err;
                }
                if (cp_len != 1 || cp[0] != '}') {
                    return ERR_TEMPLATE_INVALID_SYNTAX;
                }
                return 0;
            default:
                err = template_parse_noop_ident(in, state, leading);
                if (err) {
                    return err;
                }
                if (strcmp("if", state->ident) == 0 || strcmp("range", state->ident) == 0 || strcmp("with", state->ident) == 0) {
                    if (ident_count > 0) {
                        return ERR_TEMPLATE_INVALID_SYNTAX;
                    }
                    (*depth)++;
                } else if (strcmp("block", state->ident) == 0) {
                    err = template_pipeline_noop_register_block(in, state);
                    if (err) {
                        return err;
                    }
                    if (ident_count > 0) {
                        return ERR_TEMPLATE_INVALID_SYNTAX;
                    }
                    (*depth)++;
                } else if (strcmp("define", state->ident) == 0) {
                    if (eval_define) {
                        return ERR_TEMPLATE_DEFINE_NESTED;
                    }
                    if (ident_count > 0) {
                        return ERR_TEMPLATE_INVALID_SYNTAX;
                    }
                    (*depth)++;
                } else if (strcmp("end", state->ident) == 0) {
                    if (ident_count > 0) {
                        return ERR_TEMPLATE_INVALID_SYNTAX;
                    }
                    if (*depth == 0) {
                        state->return_reason = RETURN_REASON_END;
                        return 0;
                    }
                    (*depth)--;
                } else if (strcmp("else", state->ident) == 0) {
                    if (ident_count > 0) {
                        return ERR_TEMPLATE_INVALID_SYNTAX;
                    }
                    if (*depth == 0) {
                        state->return_reason = RETURN_REASON_ELSE;
                        return 0;
                    }
                }
                break;
        }
        ident_count++;
    }
}

int template_run_noop(stream* in, state* state, bool eval_define) {
    unsigned char cp[4];
    size_t cp_len;
    size_t depth = 0;
    int err = 0;
    while (true) {
        err = stream_next_utf8_cp(in, cp, &cp_len);
        if (err) {
            return err;
        }
        if (cp[0] != '{') {
            continue;
        }
        err = stream_next_utf8_cp(in, cp, &cp_len);
        if (err) {
            return err;
        }
        if (cp[0] != '{') {
            continue;
        }
        err = template_pipeline_noop(in, state, &depth, eval_define);
        if (err) {
            return err;
        }
        if (state->return_reason == RETURN_REASON_END || state->return_reason == RETURN_REASON_ELSE) {
            return 0;
        }
    }
}

int template_run_plain(stream* in, state* state);

int template_if(stream* in, state* state) {
    json_value nothing = JSON_NULL;
    bool any_branch = false;
    while (true) {
        stack_push_frame(&state->stack);
        tracked_value cond = TRACKED_NULL;
        int err = template_parse_expr(in, state, &cond, TEMPLATE_PARSE_EXPR_FORCE_SPACE);
        if (err) {
            tracked_value_free(&cond);
            stack_pop_frame(&state->stack);
            return err;
        }
        err = template_end_pipeline(in, state, &nothing);
        if (err) {
            tracked_value_free(&cond);
            stack_pop_frame(&state->stack);
            return err;
        }
        bool cond_empty = is_empty(&cond.val);
        if (cond_empty || any_branch) {
            err = template_run_noop(in, state, false);
        } else {
            any_branch = true;
            err = template_run_plain(in, state);
        }
        tracked_value_free(&cond);
        if (err) {
            stack_pop_frame(&state->stack);
            return err;
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
        if (err) {
            return err;
        }
        err = template_parse_ident(in, state);
        if (err) {
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
    if (err) {
        return err;
    }
    if (any_branch) {
        err = template_run_noop(in, state, false);
    } else {
        stack_push_frame(&state->stack);
        err = template_run_plain(in, state);
        stack_pop_frame(&state->stack);
    }
    if (err) {
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
    tracked_value iterable;
    char* key_name;
    char* value_name;
} range_params;

void range_params_free(range_params* params) {
    tracked_value_free(&params->iterable);
    free(params->key_name);
    free(params->value_name);
}

// requires fresh stack frame
int template_parse_range_params(state* state, stream* in, range_params* params) {
    params->iterable = TRACKED_NULL;
    params->key_name = NULL;
    params->value_name = NULL;
    // start post range keyword
    unsigned char cp[4];
    size_t cp_len;
    int err = stream_next_utf8_cp(in, cp, &cp_len);
    if (err) {
        goto cleanup;
    }
    if (cp_len != 1) {
        err = ERR_TEMPLATE_INVALID_SYNTAX;
        goto cleanup;
    }
    if (cp[0] != '$' && cp[0] != '(' && !isspace(cp[0])) {
        err = ERR_TEMPLATE_INVALID_SYNTAX;
        goto cleanup;
    }
    if (cp[0] != '(') {
        err = template_next_nonspace(in, cp, &cp_len);
    }
    if (err) {
        goto cleanup;
    }
    if (cp_len != 1) {
        err = ERR_TEMPLATE_INVALID_SYNTAX;
        goto cleanup;
    }
    // if not starts with $ => parse_expr
    if (cp[0] != '$') {
        err = stream_seek(in, -1);
        if (err) {
            goto cleanup;
        }
        err = template_parse_expr(in, state, &params->iterable, 0);
        goto cleanup;
    }
    err = template_parse_ident(in, state);
    if (err) {
        goto cleanup;
    }
    err = template_next_nonspace(in, cp, &cp_len);
    if (err) {
        goto cleanup;
    }
    if (cp_len != 1) {
        err = ERR_TEMPLATE_INVALID_SYNTAX;
        goto cleanup;
    }
    const json_value* var;
    switch (cp[0]) {
        case ',':
            err = template_next_nonspace(in, cp, &cp_len);
            if (err) {
                goto cleanup;
            }
            if (cp_len != 1 || cp[0] != '$') {
                err = ERR_TEMPLATE_INVALID_SYNTAX;
                goto cleanup;
            }
            params->key_name = strdup(state->ident);
            err = template_parse_ident(in, state);
            if (err) {
                goto cleanup;
            }
            err = template_next_nonspace(in, cp, &cp_len);
            if (err) {
                goto cleanup;
            }
            if (cp_len != 1 || cp[0] != ':') {
                err = ERR_TEMPLATE_INVALID_SYNTAX;
                goto cleanup;
            }
            // deliberate fallthrough
        case ':':
            err = stream_next_utf8_cp(in, cp, &cp_len);
            if (err) {
                goto cleanup;
            }
            if (cp_len != 1 || cp[0] != '=') {
                err = ERR_TEMPLATE_INVALID_SYNTAX;
                goto cleanup;
            }
            params->value_name = strdup(state->ident);
            err = template_parse_expr(in, state, &params->iterable, 0);
            goto cleanup;
        case '-':
        case '}':
            var = stack_find_var(&state->stack, state->ident);
            if (var == NULL) {
                err = ERR_TEMPLATE_VAR_UNKNOWN;
                goto cleanup;
            }
            params->iterable.is_heap = false;
            params->iterable.val = *var;
            err = stream_seek(in, -1);
            goto cleanup;
        default:
            err = ERR_TEMPLATE_INVALID_SYNTAX;
            goto cleanup;
    }
cleanup:
    if (err) {
        range_params_free(params);
        params->key_name = NULL;
        params->value_name = NULL;
        params->iterable = TRACKED_NULL;
    }
    return err;
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

#ifdef FUZZING_BUILD_MODE
#define RANGE_INT_MAX 8
#else
#define RANGE_INT_MAX SIZE_MAX
#endif

int value_iter_new(value_iter* iter, json_value* val) {
    switch (val->ty) {
        case JSON_TY_NUMBER:
            iter->ty = JSON_TY_NUMBER;
            iter->count = 0;
            double num = val->inner.num;
            // not using validate_index from func.c, because
            // it is valid to iterate over negative integers
            if (trunc(num) != num) {
                return ERR_TEMPLATE_NO_ITERABLE;
            }
            if (num > 0.0) {
                if (num > (double)RANGE_INT_MAX) {
                    return ERR_TEMPLATE_NO_ITERABLE;
                }
                iter->len = num;
            } else {
                iter->len = 0;
            }
            iter->keys = NULL;
            return 0;
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
    if (iter->count >= iter->len) {
        return false;
    }
    switch (iter->ty) {
        case JSON_TY_NUMBER:
            out->idx = iter->count;
            out->key.ty = JSON_TY_NUMBER;
            out->key.inner.num = iter->count;
            out->val.ty = JSON_TY_NUMBER;
            out->val.inner.num = iter->count;
            iter->count++;
            return true;
        case JSON_TY_ARRAY:
            out->idx = iter->count;
            out->key.ty = JSON_TY_NUMBER;
            out->key.inner.num = iter->count;
            out->val = iter->inner.arr->data[iter->count];
            iter->count++;
            return true;
        case JSON_TY_OBJECT:
            out->idx = iter->count;
            out->key.ty = JSON_TY_STRING;
            char* key = iter->keys[iter->count];
            out->key.inner.str = key;
            json_value* val;
            assert(hashmap_get(iter->inner.obj, key, (const void**)&val));
            out->val = *val;
            iter->count++;
            return true;
    }
    assert(0);
}

int template_range(stream* in, state* state) {
    json_value nothing = JSON_NULL;
    stack_push_frame(&state->stack);  // holds arg if var def
    range_params params;
    value_iter iter = {.ty = JSON_TY_NULL};  // value_iter_free depends on initalized ty
    int err = template_parse_range_params(state, in, &params);
    if (err) {
        goto clean_pop1;
    }
    switch (params.iterable.val.ty) {
        case JSON_TY_NUMBER:
            if (params.key_name != NULL) {
                err = ERR_TEMPLATE_NO_ITERABLE;
                goto clean_pop1;
            }
            break;
        case JSON_TY_ARRAY:
        case JSON_TY_OBJECT:
            break;
        default:
            err = ERR_TEMPLATE_NO_ITERABLE;
            goto clean_pop1;
    }
    if (is_empty(&params.iterable.val)) {
        err = template_end_pipeline(in, state, &nothing);
        if (err) {
            goto clean_pop1;
        }
        err = template_run_noop(in, state, false);
        if (err) {
            goto clean_pop1;
        }
        bool is_else = state->return_reason == RETURN_REASON_ELSE;
        state->return_reason = RETURN_REASON_REGULAR;
        if (!is_else) {
            goto clean_pop1;
        }
        err = template_end_pipeline(in, state, &nothing);
        if (err) {
            goto clean_pop1;
        }
        err = template_run_plain(in, state);
        if (err) {
            goto clean_pop1;
        }
        state->return_reason = RETURN_REASON_REGULAR;
        goto clean_pop1;
    }

    json_value* current = state->dot;
    long pre_pos;
    err = stream_pos(in, &pre_pos);
    if (err) {
        goto clean_pop1;
    }
    err = value_iter_new(&iter, &params.iterable.val);
    if (err) {
        goto clean_pop1;
    }
    value_iter_out out;
    while (value_iter_next(&iter, &out)) {
        err = template_end_pipeline(in, state, &nothing);
        if (err) {
            goto clean_pop1;
        }
        // post range pipeline
        state->dot = &out.val;
        stack_push_frame(&state->stack);
        if (params.key_name != NULL) {
            err = stack_set_ref(&state->stack, params.key_name, &out.key);
            if (err) {
                goto clean_pop2;
            }
        }
        if (params.value_name != NULL) {
            err = stack_set_ref(&state->stack, params.value_name, &out.val);
            if (err) {
                goto clean_pop2;
            }
        }
        err = template_run_plain(in, state);
        stack_pop_frame(&state->stack);
        if (err) {
            goto clean_pop1;
        }
        err = stream_set_pos(in, pre_pos);
        if (err) {
            goto clean_pop1;
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
    if (err) {
        goto cleanup;
    }
    err = template_run_noop(in, state, false);
    if (err) {
        goto cleanup;
    }
    bool is_else = state->return_reason == RETURN_REASON_ELSE;
    state->return_reason = RETURN_REASON_REGULAR;
    state->dot = current;
    if (!is_else) {
        goto cleanup;
    }
    err = template_end_pipeline(in, state, &nothing);
    if (err) {
        goto cleanup;
    }
    err = template_run_noop(in, state, false);
    if (err) {
        goto cleanup;
    }
    if (state->return_reason != RETURN_REASON_END) {
        err = ERR_TEMPLATE_INVALID_SYNTAX;
        goto cleanup;
    }
    state->return_reason = RETURN_REASON_REGULAR;
    goto cleanup;

clean_pop2:
    stack_pop_frame(&state->stack);
clean_pop1:
    stack_pop_frame(&state->stack);
cleanup:
    value_iter_free(&iter);
    range_params_free(&params);
    return err;
}

int template_with(stream* in, state* state) {
    json_value nothing = JSON_NULL;
    bool any_branch = false;
    while (true) {
        stack_push_frame(&state->stack);
        tracked_value arg = TRACKED_NULL;
        int err = template_parse_expr(in, state, &arg, TEMPLATE_PARSE_EXPR_FORCE_SPACE);
        if (err == ERR_TEMPLATE_KEY_UNKNOWN) {
            arg = TRACKED_NULL;
            err = 0;
        }
        if (err) {
            tracked_value_free(&arg);
            stack_pop_frame(&state->stack);
            return err;
        }
        err = template_end_pipeline(in, state, &nothing);
        if (err) {
            tracked_value_free(&arg);
            stack_pop_frame(&state->stack);
            return err;
        }
        bool arg_empty = is_empty(&arg.val);
        if (arg_empty) {
            err = template_run_noop(in, state, false);
        } else {
            // Add another stack frame in case arg originates from the stack
            // but is reassigned in the body causing a double-free, e.g.
            // "{{ with $ = . }}{{ $ = "a" }}{{ . }}".
            // The second assignment would free arg once
            stack_push_frame(&state->stack);
            any_branch = true;
            json_value* previous = state->dot;
            state->dot = &arg.val;
            err = template_run_plain(in, state);
            state->dot = previous;
            stack_pop_frame(&state->stack);
        }
        tracked_value_free(&arg);
        if (err) {
            stack_pop_frame(&state->stack);
            return err;
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
        if (err) {
            return err;
        }
        err = template_parse_ident(in, state);
        if (err) {
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
    if (err) {
        return err;
    }
    if (any_branch) {
        err = template_run_noop(in, state, false);
    } else {
        stack_push_frame(&state->stack);
        err = template_run_plain(in, state);
        stack_pop_frame(&state->stack);
    }
    if (err) {
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

int template_define(stream* in, state* state) {
    if (state->eval_block) {
        return ERR_TEMPLATE_DEFINE_NESTED;
    }
    char* name;
    int err = template_parse_define_name(in, &name);
    if (err) {
        return err;
    }
    json_value nothing = JSON_NULL;
    err = template_end_pipeline(in, state, &nothing);
    if (err) {
        goto cleanup;
    }
    long pos;
    err = stream_pos(in, &pos);
    if (err) {
        goto cleanup;
    }
    err = template_run_noop(in, state, true);
    if (err) {
        goto cleanup;
    }
    if (state->return_reason != RETURN_REASON_END) {
        err = ERR_TEMPLATE_INVALID_SYNTAX;
        goto cleanup;
    }
    state->return_reason = RETURN_REASON_REGULAR;
    assert(sizeof(long) == sizeof(void*));
    entry previous = hashmap_insert(&state->define_locs, name, (void*)pos);
    if (previous.exists) {
        free(previous.key);
    }
cleanup:
    if (err) {
        free(name);
    }
    return err;
}

int template_run_nested(stream* in, state* state, json_value* new_dot) {
    json_value* current_dot = state->dot;
    state->dot = new_dot;
    stack current_stack = state->stack;
    stack_new(&state->stack);
    stack_push_frame(&state->stack);
    stack_push_frame(&state->stack);
    int err = template_run_plain(in, state);
    stack_pop_frame(&state->stack);
    stack_pop_frame(&state->stack);
    stack_free(&state->stack);
    state->stack = current_stack;
    state->dot = current_dot;
    return err;
}

int template_template(stream* in, state* state) {
    char* name;
    int err = template_parse_define_name(in, &name);
    if (err) {
        return err;
    }
    long define_pos;
    int found = hashmap_get(&state->define_locs, name, (const void**)&define_pos);
    if (!found) {
        err = ERR_TEMPLATE_DEFINE_UNKNOWN;
        goto cleanup;
    }
    unsigned char cp[4];
    size_t cp_len;
    err = stream_next_utf8_cp(in, cp, &cp_len);
    if (err) {
        goto cleanup;
    }
    if (cp_len != 1 || !isspace(cp[0])) {
        err = ERR_TEMPLATE_INVALID_SYNTAX;
        goto cleanup;
    }
    tracked_value arg = TRACKED_NULL;
    err = template_parse_expr(in, state, &arg, 0);
    switch (err) {
        case 0:
            break;
        case ERR_TEMPLATE_NO_VALUE:
            tracked_value_free(&arg);
            arg = TRACKED_NULL;
            break;
        default:
            tracked_value_free(&arg);
            goto cleanup;
    }
    long current_pos;
    err = stream_pos(in, &current_pos);
    if (err) {
        tracked_value_free(&arg);
        goto cleanup;
    }
    err = stream_set_pos(in, define_pos);
    if (err) {
        tracked_value_free(&arg);
        goto cleanup;
    }
    err = template_run_nested(in, state, &arg.val);
    if (err) {
        tracked_value_free(&arg);
        goto cleanup;
    }
    if (state->return_reason != RETURN_REASON_END) {
        err = ERR_TEMPLATE_INVALID_SYNTAX;
        tracked_value_free(&arg);
        goto cleanup;
    }
    state->return_reason = RETURN_REASON_REGULAR;
    err = stream_set_pos(in, current_pos);
    if (err) {
        tracked_value_free(&arg);
        goto cleanup;
    }
    tracked_value_free(&arg);
cleanup:
    free(name);
    return err;
}

int template_block(stream* in, state* state) {
    char* name;
    int err = template_parse_define_name(in, &name);
    if (err) {
        return err;
    }
    unsigned char cp[4];
    size_t cp_len;
    err = stream_next_utf8_cp(in, cp, &cp_len);
    if (err) {
        goto cleanup;
    }
    if (cp_len != 1 || !isspace(cp[0])) {
        err = ERR_TEMPLATE_INVALID_SYNTAX;
        goto cleanup;
    }
    tracked_value arg = TRACKED_NULL;
    err = template_parse_expr(in, state, &arg, 0);
    if (err) {
        tracked_value_free(&arg);
        goto cleanup;
    }
    json_value nothing = JSON_NULL;
    err = template_end_pipeline(in, state, &nothing);
    if (err) {
        tracked_value_free(&arg);
        goto cleanup;
    }
    long pos;
    err = stream_pos(in, &pos);
    if (err) {
        tracked_value_free(&arg);
        goto cleanup;
    }
    state->eval_block = true;
    err = template_run_nested(in, state, &arg.val);
    state->eval_block = false;
    if (err) {
        tracked_value_free(&arg);
        goto cleanup;
    }
    if (state->return_reason != RETURN_REASON_END) {
        err = ERR_TEMPLATE_INVALID_SYNTAX;
        tracked_value_free(&arg);
        goto cleanup;
    }
    state->return_reason = RETURN_REASON_REGULAR;
    assert(sizeof(long) == sizeof(void*));
    entry previous = hashmap_insert(&state->define_locs, name, (void*)pos);
    if (previous.exists) {
        free(previous.key);
    }
    tracked_value_free(&arg);
cleanup:
    if (err) {
        free(name);
    }
    return err;
}

#ifdef FUZZING_BUILD_MODE
#define RANGE_DEPTH_MAX 3
#else
#define RANGE_DEPTH_MAX 24
#endif

int template_dispatch_keyword(stream* in, state* state) {
    int err = template_parse_ident(in, state);
    if (err) {
        return err;
    }
    if (strcmp("if", state->ident) == 0) {
        err = template_if(in, state);
        if (err == EOF) {
            return ERR_TEMPLATE_UNEXPECTED_EOF;
        }
        return err;
    }
    if (strcmp("range", state->ident) == 0) {
        if (state->range_depth > RANGE_DEPTH_MAX) {
            return ERR_BUF_OVERFLOW;
        }
        state->range_depth++;
        err = template_range(in, state);
        state->range_depth--;
        if (err == EOF) {
            return ERR_TEMPLATE_UNEXPECTED_EOF;
        }
        return err;
    }
    if (strcmp("with", state->ident) == 0) {
        err = template_with(in, state);
        if (err == EOF) {
            return ERR_TEMPLATE_UNEXPECTED_EOF;
        }
        return err;
    }
    if (strcmp("define", state->ident) == 0) {
        err = template_define(in, state);
        if (err == EOF) {
            return ERR_TEMPLATE_UNEXPECTED_EOF;
        }
        return err;
    }
    if (strcmp("template", state->ident) == 0) {
        err = template_template(in, state);
        if (err == EOF) {
            return ERR_TEMPLATE_UNEXPECTED_EOF;
        }
        return err;
    }
    if (strcmp("block", state->ident) == 0) {
        err = template_block(in, state);
        if (err == EOF) {
            return ERR_TEMPLATE_UNEXPECTED_EOF;
        }
        return err;
    }
    if (strcmp("end", state->ident) == 0) {
        // only used in the non-noop case
        // in the noop case template_run_noop
        // takes care of the matching "end"
        state->return_reason = RETURN_REASON_END;
        return 0;
    }
    if (strcmp("else", state->ident) == 0) {
        // only used in the non-noop case
        // in the noop case template_run_noop
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

int template_eval_arg(stream* in, state* state, long where, tracked_value* result) {
    int err = stream_set_pos(in, where);
    if (err) {
        return err;
    }
    err = template_parse_expr(in, state, result, TEMPLATE_PARSE_EXPR_NO_PIPE | TEMPLATE_PARSE_EXPR_NO_VAR_MUT);
    if (err) {
        return err;
    }
    // check that the next char is something template_skip_expr() would split at
    // to ensure that the arg was fully consumed, without such a check stuff like
    // '$#' would return '$' due to the '#' never being read and hence not producing
    // an ERR_TEMPLATE_INVALID_SYNTAX.
    unsigned char cp[4];
    size_t cp_len;
    // no need to seek back as template_dispatch_func does that
    err = stream_next_utf8_cp(in, cp, &cp_len);
    if (err) {
        return err;
    }
    if (cp_len != 1) {
        return ERR_TEMPLATE_INVALID_SYNTAX;
    }
    if (isspace(cp[0]) || cp[0] == ')' || cp[0] == '}' || cp[0] == '|') {
        return 0;
    }
    return ERR_TEMPLATE_INVALID_SYNTAX;
}

int template_arg_iter_next(template_arg_iter* iter, tracked_value* result) {
    if (iter->idx < iter->args_len) {
        int err = template_eval_arg(iter->in, (state*)iter->state, iter->args[iter->idx], result);
        iter->idx++;
        if (err) {
            tracked_value_free(result);
            return err;
        }
        return 0;
    }
    if (iter->idx == iter->args_len && iter->piped != NULL) {
        *result = *iter->piped;
        iter->idx++;
        return 0;
    }
    return 0;
}

int template_arg_iter_len(template_arg_iter* iter) {
    return iter->args_len + (iter->piped ? 1 : 0);
}

#define TEMPLATE_FUNC_ARGS_MAX 16

int template_dispatch_func(stream* in, state* state, tracked_value* piped, tracked_value* result) {
    long args[TEMPLATE_FUNC_ARGS_MAX];
    template_arg_iter iter = {
        .in = in,
        .args = args,
        .idx = 0,
        .args_len = 0,
        .piped = piped,
        .state = state,
    };
    int err = template_parse_ident(in, state);
    if (err) {
        goto cleanup;
    }
    char func_name[STATE_IDENT_CAP];
    strcpy(func_name, state->ident);
    long pre_end;
    unsigned char cp[4];
    size_t cp_len;
    while (true) {
        err = stream_next_utf8_cp(in, cp, &cp_len);
        if (err) {
            goto cleanup;
        }
        if (cp_len != 1) {
            err = ERR_TEMPLATE_INVALID_SYNTAX;
            goto cleanup;
        }
        if (!(isspace(cp[0]) || cp[0] == ')' || cp[0] == '}' || cp[0] == '|')) {
            err = ERR_TEMPLATE_INVALID_SYNTAX;
            goto cleanup;
        }
        err = stream_seek(in, -cp_len);
        if (err) {
            goto cleanup;
        }
        if (iter.args_len == TEMPLATE_FUNC_ARGS_MAX) {
            err = ERR_BUF_OVERFLOW;
            goto cleanup;
        }
        err = template_skip_expr(in, args + iter.args_len);
        if (err == ERR_TEMPLATE_NO_VALUE) {
            pre_end = args[iter.args_len];
            break;
        }
        if (err) {
            goto cleanup;
        }
        iter.args_len++;
    }

    funcptr f;
    int found = hashmap_get(&state->funcmap, func_name, (const void**)&f);
    if (!found) {
        err = ERR_TEMPLATE_FUNC_UNKNOWN;
        goto cleanup;
    }
    err = f(&iter, result);
    if (err) {
        goto cleanup;
    }
    err = stream_set_pos(in, pre_end);
cleanup:
    // func impls shall free every tracked_value requested
    // from the iter, hence check where the iter is at.
    if (piped != NULL && iter.idx < iter.args_len + 1) {
        tracked_value_free(piped);
    }
    return err;
}

int template_skip_comment(stream* in) {
    unsigned char cp[4];
    size_t cp_len;
    while (true) {
        int err = stream_next_utf8_cp(in, cp, &cp_len);
        if (err) {
            return err;
        }
        if (cp_len != 1 || cp[0] != '*') {
            continue;
        }
        err = stream_next_utf8_cp(in, cp, &cp_len);
        if (err) {
            return err;
        }
        if (cp_len != 1 || cp[0] != '/') {
            continue;
        }
        err = stream_next_utf8_cp(in, cp, &cp_len);
        if (err) {
            return err;
        }
        if (cp_len != 1) {
            return ERR_TEMPLATE_INVALID_SYNTAX;
        }
        switch (cp[0]) {
            case '}':
                return stream_seek(in, -cp_len);
            case ' ':
                err = stream_next_utf8_cp(in, cp, &cp_len);
                if (err) {
                    return err;
                }
                if (cp_len != 1 || cp[0] != '-') {
                    return ERR_TEMPLATE_INVALID_SYNTAX;
                }
                return stream_seek(in, -cp_len);
            default:
                return ERR_TEMPLATE_INVALID_SYNTAX;
        }
    }
}

int template_dispatch_pipeline(stream* in, state* state, tracked_value* result) {
    unsigned char cp[4];
    size_t cp_len;
    int err = stream_next_utf8_cp(in, cp, &cp_len);
    if (err) {
        return err;
    }
    if (cp_len != 1) {
        return ERR_TEMPLATE_INVALID_SYNTAX;
    }
    if (cp[0] == '/') {
        err = stream_next_utf8_cp(in, cp, &cp_len);
        if (err) {
            return err;
        }
        if (cp_len != 1 || cp[0] != '*') {
            return ERR_TEMPLATE_INVALID_SYNTAX;
        }
        return template_skip_comment(in);
    }
    if (isspace(cp[0])) {
        err = template_next_nonspace(in, cp, &cp_len);
        if (err) {
            return err;
        }
        if (cp_len != 1) {
            return ERR_TEMPLATE_INVALID_SYNTAX;
        }
    }
    if (cp[0] == '$') {
        long pre_pos;
        err = stream_pos(in, &pre_pos);
        if (err) {
            return err;
        }
        err = template_parse_var_mutation(in, state, result);
        if (err != ERR_TEMPLATE_NO_MUTATION) {
            tracked_value_free(result);
            // top level var assignments/definitions have their result discarded
            *result = TRACKED_NULL;
            return err;
        }
        err = stream_set_pos(in, pre_pos);
        if (err) {
            return err;
        }
    }
    if (cp[0] == '(') {
        err = template_parse_parenthesis(in, state, result);
        if (err) {
            return err;
        }
        return template_parse_pipe(in, state, result);
    }
    err = template_parse_value(in, state, result, cp[0]);
    const char no_value[] = "<no value>";
    switch (err) {
        case 0:
            // for some reason top-level nil is invalid in go templates
            if (result->val.ty == JSON_TY_NULL) {
                return ERR_TEMPLATE_KEYWORD_UNEXPECTED;
            }
            return template_parse_pipe(in, state, result);
        case ERR_TEMPLATE_KEY_UNKNOWN:
            buf_append(&state->out, no_value, sizeof(no_value) - 1);
            return 0;
        case ERR_TEMPLATE_NO_VALUE:
            break;
        default:
            return err;
    }
    if (isalpha(cp[0])) {
        err = template_dispatch_keyword(in, state);
        if (err != ERR_TEMPLATE_KEYWORD_UNKNOWN) {
            return err;
        }
        err = stream_seek(in, -strlen(state->ident));
        if (err) {
            return err;
        }
        err = template_dispatch_func(in, state, NULL, result);
        if (err) {
            return err;
        }
        return template_parse_pipe(in, state, result);
    }
    return ERR_TEMPLATE_INVALID_SYNTAX;
}

int template_invoke_pipeline(stream* in, state* state) {
    tracked_value result = TRACKED_NULL;
    int err = template_dispatch_pipeline(in, state, &result);
    if (err) {
        tracked_value_free(&result);
        return err;
    }
    if (state->return_reason != RETURN_REASON_REGULAR) {
        return 0;
    }
    err = template_end_pipeline(in, state, &result.val);
    tracked_value_free(&result);
    return err;
}

int template_start_pipeline(stream* in, state* state) {
    unsigned char cp[4];
    size_t cp_len;
    int err = stream_next_utf8_cp(in, cp, &cp_len);
    if (err) {
        return err;
    }
    if (cp_len != 1) {
        return ERR_TEMPLATE_INVALID_SYNTAX;
    }
    if (cp[0] != '-') {
        err = stream_seek(in, -cp_len);
        if (err) {
            return err;
        }
        return template_invoke_pipeline(in, state);  // just past "{{"
    }
    size_t off = cp_len;
    err = stream_next_utf8_cp(in, cp, &cp_len);
    if (err) {
        return err;
    }
    if (cp_len != 1) {
        return ERR_TEMPLATE_INVALID_SYNTAX;
    }
    if (cp[0] != ' ') {
        err = stream_seek(in, -cp_len - off);
        if (err) {
            return err;
        }
        return template_invoke_pipeline(in, state);  // just past "{{", "-" is guaranteed after
    }
    state->out.len = state->out_nospace;
    return template_invoke_pipeline(in, state);  // past "{{- "
}

int template_run_plain(stream* in, state* state) {
    unsigned char cp[4];
    size_t cp_len;
    int err = 0;
    while (true) {
        err = stream_next_utf8_cp(in, cp, &cp_len);
        if (err) {
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
        if (err) {
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
        if (err) {
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

void define_loc_free(entry* e, void* userdata) {
    free(e->key);
}

int template_eval_stream(stream* in, json_value* dot, char** out) {
    state state;
    state.dot = dot;
    state.out_nospace = 0;
    state.range_depth = 0;
    state.return_reason = RETURN_REASON_REGULAR;
    state.eval_block = false;
    hashmap_new(&state.define_locs, hashmap_strcmp, hashmap_strlen, HASH_FUNC_DJB2);
    funcmap_new(&state.funcmap);
    stack_new(&state.stack);
    stack_push_frame(&state.stack);
    int err = stack_set_ref(&state.stack, "", dot);
    if (err) {
        stack_pop_frame(&state.stack);
        goto cleanup;
    }
    buf_init(&state.out);
    err = template_run_plain(in, &state);
    stack_pop_frame(&state.stack);
    assert(state.stack.len == 0);
    if (err == EOF && state.stack.len == 0) {
        err = 0;
    }
    buf_append(&state.out, "", 1);
    *out = state.out.data;
cleanup:
    stack_free(&state.stack);
    funcmap_free(&state.funcmap);
    hashmap_iter(&state.define_locs, NULL, define_loc_free);
    hashmap_free(&state.define_locs);
    return err;
}

int template_eval_mem(const char* tpl, size_t n, json_value* dot, char** out) {
    stream in;
    stream_open_memory(&in, tpl, n);
    int err = template_eval_stream(&in, dot, out);
    int close_err = stream_close(&in);
    if (close_err) {
        free(*out);
        return close_err;
    }
    return err;
}

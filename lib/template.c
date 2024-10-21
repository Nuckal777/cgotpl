#include "template.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
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

int sprintval(buf* b, json_value* val) {
    size_t expected;
    char print_buf[128];
    const char* str_true = "true";
    const char* str_false = "false";
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
    }
    assert(0);
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

#define STATE_IDENT_CAP 128

typedef struct {
    json_value scratch_val;
    json_value* dot;
    buf out;
    size_t out_nospace;
    size_t depth;
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

int template_parse_path_expr(stream* in, state* state, json_value* result, size_t depth) {
    *result = JSON_NULL;
    int err = template_parse_ident(in, state);
    if (err != 0) {
        return err;
    }
    unsigned char cp[4];
    size_t cp_len;
    err = stream_next_utf8_cp(in, cp, &cp_len);
    if (err != 0) {
        return err;
    }
    if (cp_len != 1) {
        return ERR_TEMPLATE_INVALID_SYNTAX;
    }
    if (strlen(state->ident) == 0) {
        if (depth == 0) {
            *result = *state->dot;
            return 0;
        } else {
            return ERR_TEMPLATE_INVALID_SYNTAX;
        }
    }
    if (state->dot->ty != JSON_TY_OBJECT) {
        return ERR_TEMPLATE_NO_OBJECT;
    }
    json_value* next;
    int found = hashmap_get(&state->dot->inner.obj, state->ident, (const void**)&next);
    if (!found) {
        return ERR_TEMPLATE_KEY_UNKNOWN;
    }
    if (cp[0] == '.') {
        json_value* current = state->dot;
        state->dot = next;
        err = template_parse_path_expr(in, state, result, depth + 1);
        state->dot = current;
        return err;
    }
    if (isspace(cp[0])) {
        *result = *next;
        return 0;
    }
    return ERR_TEMPLATE_INVALID_SYNTAX;
}

int template_parse_literal(stream* in, state* state, json_value* result, unsigned char first) {
    unsigned char cp[4];
    size_t cp_len;
    int err = 0;
    double val;
    size_t seek_back = -1;
    size_t identifier_len = 0;
    json_value out = JSON_NULL;
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
            return stream_seek(in, -identifier_len - 1);
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
            return stream_seek(in, -identifier_len - 1);
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
            identifier_len = strlen(state->ident);
            return stream_seek(in, -identifier_len - 1);
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
            return template_parse_path_expr(in, state, result, 0);
        default:
            return ERR_TEMPLATE_NO_LITERAL;
    }
    return 0;
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
    return template_parse_literal(in, state, arg, cp[0]);
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
                if (strcmp("if", state->ident) == 0) {
                    if (ident_count > 0) {
                        return ERR_TEMPLATE_INVALID_SYNTAX;
                    }
                    state->depth++;
                } else if (strcmp("end", state->ident) == 0) {
                    if (ident_count > 0) {
                        return ERR_TEMPLATE_INVALID_SYNTAX;
                    }
                    state->depth--;
                    if (state->depth == start_depth - 1) {
                        return ERR_TEMPLATE_KEYWORD_END;
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
    size_t start_depth = state->depth;
    while (true) {
        int err = stream_next_utf8_cp(in, cp, &cp_len);
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
        if (err == ERR_TEMPLATE_KEYWORD_END) {
            return 0;
        }
        if (err != 0) {
            return err;
        }
    }
}

int template_plain(stream* in, state* state);

int template_dispatch_keyword(stream* in, state* state) {
    int err = template_parse_ident(in, state);
    if (err != 0) {
        return err;
    }
    json_value nothing = JSON_NULL;
    if (strcmp("if", state->ident) == 0) {
        json_value cond;
        err = template_parse_arg(in, state, &cond);
        if (err != 0) {
            return err;
        }
        state->depth++;
        if (is_empty(&cond)) {
            err = template_end_pipeline(in, state, &nothing);
            if (err != 0) {
                return err;
            }
            err = template_noop(in, state);
            if (err != 0) {
                return err;
            }
        } else {
            err = template_end_pipeline(in, state, &nothing);
            if (err != 0) {
                return err;
            }
            err = template_plain(in, state);
            if (err != 0) {
                return err;
            }
        }
        return 0;
    }
    if (strcmp("range", state->ident) == 0) {
        json_value arg;
        err = template_parse_arg(in, state, &arg);
        if (err != 0) {
            return err;
        }
        if (arg.ty != JSON_TY_ARRAY) {
            return ERR_TEMPLATE_NO_LIST;
        }
        state->depth++;
        if (arg.inner.arr.len == 0) {
            err = template_end_pipeline(in, state, &nothing);
            if (err != 0) {
                return err;
            }
            err = template_noop(in, state);
            if (err != 0) {
                return err;
            }
            return 0;
        }
        json_value* current = state->dot;
        long pre_pos;
        err = stream_pos(in, &pre_pos);
        if (err != 0) {
            return err;
        }
        for (size_t i = 0; i < arg.inner.arr.len; i++) {
            err = template_end_pipeline(in, state, &nothing);
            if (err != 0) {
                return err;
            }
            state->dot = &arg.inner.arr.data[i];
            err = template_plain(in, state);
            if (err != 0) {
                return err;
            }
            if (i != arg.inner.arr.len - 1) {
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
        }
        state->dot = current;
        return 0;
    }
    if (strcmp("end", state->ident) == 0) {
        // only used in the non-noop case
        // in the noop case template_noop
        // takes care of the matching "end"
        return ERR_TEMPLATE_KEYWORD_END;
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
    err = template_parse_literal(in, state, result, cp[0]);
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
    if (err == 0) {
        return template_end_pipeline(in, state, &result);
    }
    // intentionally skips reading }} in
    // case of ERR_TEMPLATE_KEYWORD_END
    return err;
}

int template_start_pipeline(stream* in, state* state) {
    unsigned char cp[4];
    size_t cp_len;
    int err = stream_next_utf8_cp(in, cp, &cp_len);
    if (err != 0) {
        return err;
    }
    if (cp[0] != '-') {
        stream_seek(in, -cp_len);
        return template_invoke_pipeline(in, state);
    }
    size_t off = cp_len;
    err = stream_next_utf8_cp(in, cp, &cp_len);
    if (err != 0) {
        return err;
    }
    if (cp[0] != ' ') {
        stream_seek(in, -cp_len - off);
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
            goto cleanup;
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
            goto cleanup;
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
        if (err == ERR_TEMPLATE_KEYWORD_END && state->depth > 0) {
            state->depth--;
            return 0;
        }
        if (err != 0) {
            return err;
        }
    }
cleanup:
    if (err == EOF) {
        err = 0;
    }
    return err;
}

int template_eval(const char* tpl, size_t n, json_value* dot, char** out) {
    stream in;
    stream_open_memory(&in, tpl, n);
    state state;
    state.dot = dot;
    state.depth = 0;
    state.scratch_val = JSON_NULL;
    buf_init(&state.out);
    int err = template_plain(&in, &state);
    buf_append(&state.out, "\0", 1);
    *out = state.out.data;
    json_value_free(&state.scratch_val);
    stream_close(&in);
    return err;
}

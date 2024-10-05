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

typedef struct {
    json_value* dot;
    buf out;
    size_t out_nospace;
} state;

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
                printf("pre strtod %s\n", buf);
                *out = strtod(buf, NULL);
                if (errno == ERANGE) {
                    errno = 0;
                    return ERR_TEMPLATE_INVALID_SYNTAX;
                }
                printf("post strtod\n");
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

int template_parse_identifier(stream* in, char** out) {
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
            err = ERR_TEMPLATE_INVALID_SYNTAX;
            goto cleanup;
        }
        if (!isalnum(cp[0])) {
            *out = b.data;
            buf_append(&b, "\0", 1);
            err = stream_seek(in, -1);
            goto cleanup;
        }
        buf_append(&b, (const char*)cp, 1);
    }
cleanup:
    if (err != 0) {
        buf_free(&b);
    }
    return err;
}

int template_dispatch_func(stream* in, state* state, json_value* result) {
    char* func_name;
    int err = template_parse_identifier(in, &func_name);
    if (err != 0) {
        goto cleanup;
    }

    err = ERR_TEMPLATE_FUNC_UNKNOWN;
cleanup:
    if (func_name) {
        free(func_name);
    }
    return err;
}

int template_parse_literal(stream* in, json_value* result, unsigned char first) {
    unsigned char cp[4];
    size_t cp_len;
    int err = 0;
    double val;
    size_t seek_back = -1;
    char* identifier;
    size_t identifier_len = 0;
    switch (first) {
        case 't':
            err = template_parse_identifier(in, &identifier);
            if (err != 0) {
                return err;
            }
            bool is_true = strcmp("rue", identifier) == 0;
            if (is_true) {
                free(identifier);
                result->ty = JSON_TY_TRUE;
                return 0;
            }
            identifier_len = strlen(identifier);
            free(identifier);
            return stream_seek(in, -identifier_len - 1);
        case 'f':
            err = template_parse_identifier(in, &identifier);
            if (err != 0) {
                return err;
            }
            bool is_false = strcmp("alse", identifier) == 0;
            if (is_false) {
                free(identifier);
                result->ty = JSON_TY_FALSE;
                return 0;
            }
            identifier_len = strlen(identifier);
            free(identifier);
            return stream_seek(in, -identifier_len - 1);
        case 'n':
            err = template_parse_identifier(in, &identifier);
            if (err != 0) {
                return err;
            }
            bool is_nil = strcmp("il", identifier) == 0;
            if (is_nil) {
                free(identifier);
                result->ty = JSON_TY_NULL;
                return 0;
            }
            identifier_len = strlen(identifier);
            free(identifier);
            return stream_seek(in, -identifier_len - 1);
        case '"':
            err = template_parse_regular_str(in, &result->inner.str);
            if (err != 0) {
                return err;
            }
            result->ty = JSON_TY_STRING;
            return 0;
        case '`':
            err = template_parse_backtick_str(in, &result->inner.str);
            if (err != 0) {
                return err;
            }
            result->ty = JSON_TY_STRING;
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
            printf("trying to parse a number\n");
            err = template_parse_number(in, &val);
            if (err != 0) {
                return err;
            }
            result->ty = JSON_TY_NUMBER;
            result->inner.num = val;
            return 0;
        default:
            return ERR_TEMPLATE_NO_LITERAL;
    }
    return 0;
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
    err = template_parse_literal(in, result, cp[0]);
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

int template_end_pipeline(stream* in, state* state) {
    json_value result = JSON_NULL;
    int err = template_dispatch_pipeline(in, state, &result);
    if (err != 0) {
        goto cleanup;
    }
    unsigned char cp[4];
    size_t cp_len;
    bool has_minus = false;
    while (true) {
        int err = stream_next_utf8_cp(in, cp, &cp_len);
        if (err != 0) {
            goto cleanup;
        }
        if (cp_len != 1) {
            err = ERR_TEMPLATE_INVALID_SYNTAX;
            goto cleanup;
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
                if (result.ty == JSON_TY_NULL) {
                    return 0;
                }
                printf("sprintval\n");
                err = sprintval(&state->out, &result);
                goto cleanup;
            default:
                err = ERR_TEMPLATE_INVALID_SYNTAX;
                goto cleanup;
        }
    }
cleanup:
    json_value_free(&result);
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
        return template_end_pipeline(in, state);
    }
    size_t off = cp_len;
    err = stream_next_utf8_cp(in, cp, &cp_len);
    if (err != 0) {
        return err;
    }
    if (cp[0] != ' ') {
        stream_seek(in, -cp_len - off);
        return template_end_pipeline(in, state);
    }
    state->out.len = state->out_nospace;
    return template_end_pipeline(in, state);
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
    buf_init(&state.out);
    int err = template_plain(&in, &state);
    buf_append(&state.out, "\0", 1);
    *out = state.out.data;
    stream_close(&in);
    return err;
}

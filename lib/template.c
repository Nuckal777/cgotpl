#include "template.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "json.h"
#include "stream.h"

typedef struct {
    char* data;
    size_t len;
    size_t cap;
} buf;

#define BUF_DEFAULT_CAP 128

#define ERR_TEMPLATE_INVALID_SYNTAX -901
#define ERR_TEMPLATE_BUFFER_OVERFLOW -902

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

int sprintval(buf* b, json_value* val) {
    size_t expected;
    char print_buf[128];
    switch (val->ty) {
        case JSON_TY_NUMBER:
            expected = snprintf(print_buf, sizeof(print_buf), "%g", val->inner.num);
            if (expected > sizeof(print_buf) - 1) {
                return ERR_TEMPLATE_BUFFER_OVERFLOW;
            }
            buf_append(b, print_buf, expected);
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

int template_dispatch_pipeline(stream* in, state* state, json_value* result) {
    unsigned char cp[4];
    size_t cp_len;
    bool has_minus = false;
    size_t seek_back = 0;
    while (true) {
        int err = stream_next_utf8_cp(in, cp, &cp_len);
        if (err != 0) {
            return err;
        }
        if (cp_len != 1) {
            return ERR_TEMPLATE_INVALID_SYNTAX;
        }
        if (isspace(cp[0])) {
            continue;
        }
        switch (cp[0]) {
            case '}':
                seek_back = 1;
                if (has_minus) {
                    seek_back += 1;
                }
                return stream_seek(in, -seek_back);
            case '-':
                has_minus = true;
                continue;
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
                seek_back = 1;
                if (has_minus) {
                    seek_back += 1;
                }
                err = stream_seek(in, -seek_back);
                if (err != 0) {
                    return err;
                }
                double val;
                printf("trying to parse a number\n");
                err = template_parse_number(in, &val);
                if (err != 0) {
                    return err;
                }
                result->ty = JSON_TY_NUMBER;
                result->inner.num = val;
                return 0;
            default:
                return ERR_TEMPLATE_INVALID_SYNTAX;
        }
        has_minus = false;
    }
    return 0;
}

int template_end_pipeline(stream* in, state* state) {
    json_value result = JSON_NULL;
    int err = template_dispatch_pipeline(in, state, &result);
    if (err != 0) {
        return err;
    }
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
                if (result.ty == JSON_TY_NULL) {
                    return 0;
                }
                printf("sprintval\n");
                return sprintval(&state->out, &result);
            default:
                return ERR_TEMPLATE_INVALID_SYNTAX;
        }
    }
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
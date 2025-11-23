#include "encode.h"

#include <assert.h>

void utf8_encode(int32_t cp, char* out, size_t* out_len) {
    *out_len = 0;
    if (cp < 128) {
        out[0] = (char)cp;
        *out_len = 1;
        return;
    }
    if (cp < 2048) {
        out[0] = (int8_t)(0xc0 | (int8_t)((cp >> 6) & 0x1f));
        out[1] = (int8_t)(0x80 | (int8_t)(cp & 0x3f));
        *out_len = 2;
        return;
    }
    if (cp < 0xffff) {
        out[0] = (int8_t)(0xe0 | (int8_t)((cp >> 12) & 0x0f));
        out[1] = (int8_t)(0x80 | (int8_t)((cp >> 6) & 0x3f));
        out[2] = (int8_t)(0x80 | (int8_t)(cp & 0x3f));
        *out_len = 3;
        return;
    }
    out[0] = (int8_t)(0xf0 | (int8_t)((cp >> 18) & 0x07));
    out[1] = (int8_t)(0x80 | (int8_t)((cp >> 12) & 0x3f));
    out[2] = (int8_t)(0x80 | (int8_t)((cp >> 6) & 0x3f));
    out[3] = (int8_t)(0x80 | (int8_t)(cp & 0x3f));
    *out_len = 4;
}

int32_t utf8_decode(const unsigned char* in, size_t in_len) {
    switch (in_len) {
        case 1:
            return in[0];
        case 2:
            return ((in[0] & 0x1f) << 6) | (in[1] & 0x3f);
        case 3:
            return ((in[0] & 0x0f) << 12) | ((in[1] & 0x3f) << 6) | (in[2] & 0x3f);
        case 4:
            return ((in[0] & 0x07) << 18) | ((in[1] & 0x3f) << 12) | ((in[2] & 0x3f) << 6) | (in[3] & 0x3f);
    }
    assert(0);
    return 0;
}

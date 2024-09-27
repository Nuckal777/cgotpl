#include "encode.h"

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

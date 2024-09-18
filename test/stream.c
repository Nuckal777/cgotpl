#include "stream.h"
#include <stdio.h>
#include <string.h>

#include "test.h"

nutest_result stream_memory() {
    const char data[4] = {7, 8, 9, 10};
    stream st;
    stream_open_memory(&st, data, sizeof(data));
    unsigned char out;
    NUTEST_ASSERT(stream_read(&st, &out) == 0);
    NUTEST_ASSERT(out == 7);
    NUTEST_ASSERT(stream_read(&st, &out) == 0);
    NUTEST_ASSERT(out == 8);
    NUTEST_ASSERT(stream_read(&st, &out) == 0);
    NUTEST_ASSERT(out == 9);
    NUTEST_ASSERT(stream_read(&st, &out) == 0);
    NUTEST_ASSERT(out == 10);
    NUTEST_ASSERT(stream_read(&st, &out) == EOF);
    stream_close(&st);
    return NUTEST_PASS;
}

nutest_result stream_file() {
    const char* path = "stream.txt";
    remove(path);
    FILE* file = fopen(path, "wb");
    NUTEST_ASSERT(file);
    size_t written = fwrite("ab", sizeof(char), 3, file);
    NUTEST_ASSERT(written == 3);
    NUTEST_ASSERT(fclose(file) == 0);

    stream st;
    NUTEST_ASSERT(stream_open_file(&st, path) == 0);
    unsigned char out;
    NUTEST_ASSERT(stream_read(&st, &out) == 0);
    NUTEST_ASSERT(out == 'a');
    NUTEST_ASSERT(stream_read(&st, &out) == 0);
    NUTEST_ASSERT(out == 'b');
    NUTEST_ASSERT(stream_read(&st, &out) == 0);
    NUTEST_ASSERT(out == 0);
    NUTEST_ASSERT(stream_read(&st, &out) == EOF);
    stream_close(&st);
    remove(path);
    return NUTEST_PASS;
}

nutest_result stream_utf8_single(void) {
    const char* data = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    stream st;
    stream_open_memory(&st, data, strlen(data) + 1);
    unsigned char out[4];
    size_t len;
    int err;
    for (char i = 'a'; i <= 'z'; i++) {
        err = stream_next_utf8_cp(&st, out, &len);
        NUTEST_ASSERT(err == 0);
        NUTEST_ASSERT(len == 1);
        NUTEST_ASSERT(*out == i);
    }
    for (char i = 'A'; i <= 'Z'; i++) {
        err = stream_next_utf8_cp(&st, out, &len);
        NUTEST_ASSERT(err == 0);
        NUTEST_ASSERT(len == 1);
        NUTEST_ASSERT(*out == i);
    }
    err = stream_next_utf8_cp(&st, out, &len);
    NUTEST_ASSERT(err == 0);
    NUTEST_ASSERT(len == 1);
    NUTEST_ASSERT(*out == 0);
    err = stream_next_utf8_cp(&st, out, &len);
    NUTEST_ASSERT(err == EOF);
    NUTEST_ASSERT(len == 0);
    stream_close(&st);
    return NUTEST_PASS;
}

nutest_result stream_utf8_multi(void) {
    const char* data = u8"a£€𐍈";
    stream st;
    stream_open_memory(&st, data, strlen(data));
    unsigned char out[4];
    size_t len;
    int err;
    err = stream_next_utf8_cp(&st, out, &len);
    NUTEST_ASSERT(err == 0);
    NUTEST_ASSERT(len == 1);
    NUTEST_ASSERT(*out == 'a');
    err = stream_next_utf8_cp(&st, out, &len);
    NUTEST_ASSERT(err == 0);
    NUTEST_ASSERT(len == 2);
    NUTEST_ASSERT(out[0] == 0xc2);
    NUTEST_ASSERT(out[1] == 0xa3);
    err = stream_next_utf8_cp(&st, out, &len);
    NUTEST_ASSERT(err == 0);
    NUTEST_ASSERT(len == 3);
    NUTEST_ASSERT(out[0] == 0xe2);
    NUTEST_ASSERT(out[1] == 0x82);
    NUTEST_ASSERT(out[2] == 0xac);
    err = stream_next_utf8_cp(&st, out, &len);
    NUTEST_ASSERT(err == 0);
    NUTEST_ASSERT(len == 4);
    NUTEST_ASSERT(out[0] == 0xf0);
    NUTEST_ASSERT(out[1] == 0x90);
    NUTEST_ASSERT(out[2] == 0x8D);
    NUTEST_ASSERT(out[3] == 0x88);
    stream_close(&st);
    return NUTEST_PASS;
}

nutest_result stream_utf8_invalid(void) {
    const unsigned char data[3] = {0xc2, 0xa3, 0xa3};
    stream st;
    stream_open_memory(&st, data, sizeof(data));
    unsigned char out[4];
    size_t len;
    int err;
    err = stream_next_utf8_cp(&st, out, &len);
    NUTEST_ASSERT(err == 0);
    err = stream_next_utf8_cp(&st, out, &len);
    NUTEST_ASSERT(err == ERR_INVALID_UTF8);
    stream_close(&st);
    return NUTEST_PASS;
}

nutest_result stream_utf8_overlong_4(void) {
    const unsigned char data[4] = {0xf0, 0x82, 0x82, 0xac};
    stream st;
    stream_open_memory(&st, data, sizeof(data));
    unsigned char out[4];
    size_t len;
    int err;
    err = stream_next_utf8_cp(&st, out, &len);
    NUTEST_ASSERT(err == ERR_INVALID_UTF8);
    stream_close(&st);
    return NUTEST_PASS;
}

nutest_result stream_utf8_overlong_2(void) {
    const unsigned char data[2] = {0xc0, 0xa3};
    stream st;
    stream_open_memory(&st, data, sizeof(data));
    unsigned char out[4];
    size_t len;
    int err;
    err = stream_next_utf8_cp(&st, out, &len);
    NUTEST_ASSERT(err == ERR_INVALID_UTF8);
    stream_close(&st);
    return NUTEST_PASS;
}

int main() {
    nutest_register(stream_memory);
    nutest_register(stream_file);
    nutest_register(stream_utf8_single);
    nutest_register(stream_utf8_multi);
    nutest_register(stream_utf8_invalid);
    nutest_register(stream_utf8_overlong_4);
    nutest_register(stream_utf8_overlong_2);
    return nutest_run();
}
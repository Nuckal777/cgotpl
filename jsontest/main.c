#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "json.h"
#include "stream.h"

#if defined(_WIN32) || defined(_WIN64)
#define DIR_SEP '\'
#else
#define DIR_SEP '/'
#endif

int test_file(const char* path) {
    const char* filename = strrchr(path, DIR_SEP) + 1;
    if (strlen(filename) == 0) {
        fprintf(stderr, "no file specified: %s\n", path);
        return 1;
    }
    char expected = filename[0];
    stream st;
    int err = stream_open_file(&st, path);
    if (err) {
        fprintf(stderr, "error opening: %s\n", path);
        return 1;
    }
    int result = 0;
    json_value val;
    err = json_parse(&st, &val);
    if (!err) {
        json_value_free(&val);
    }
    unsigned char data;
    int st_err = stream_read(&st, &data);
    if (st_err && st_err != EOF) {
        fprintf(stderr, "failed to read byte past JSON");
        result = 1;
        goto cleanup;
    }
    switch (expected) {
        case 'y':
            if (err) {
                result = 1;
            }
            break;
        case 'n':
            // cgotpl's parser doesn't care about garbage after a valid value
            if (!err && st_err == EOF) {
                result = 1;
            }
            break;
        case 'i':
            break;
        default:
            result = 1;
            fprintf(stderr, "unexpected outcome flag '%c'\n", expected);
            break;
    }
cleanup:
    err = stream_close(&st);
    if (err) {
        fprintf(stderr, "failed to close file stream\n");
    }
    return result;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "invalid number of arguments\n");
        return EXIT_FAILURE;
    }
    int sum = 0;
    for (size_t i = 1; i < argc; i++) {
        int result = test_file(argv[i]);
        sum += result;
        if (result) {
            printf("failed: %s\n", argv[i]);
        }
    }
    printf("failed %d cases\n", sum);
    return sum ? EXIT_FAILURE : EXIT_SUCCESS;
}

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "template.h"

typedef struct {
    char* filename;
    char* tpl;
    char* data;
    char is_help;
} args;

#define ERR_PARSE_EXPECT_ARG -700
#define ERR_PARSE_UNEXPECTED_COUNT -701

int parse_args(int argc, char* argv[], args* out) {
    *out = (args){.filename = NULL, .data = NULL, .tpl = NULL, .is_help = 0};
    int err = 0;
    size_t freestanding_len = 0;
    char** freestanding = malloc(argc * sizeof(char*));
    assert(freestanding);

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-f") == 0) {
            i++;
            if (i >= argc) {
                err = ERR_PARSE_EXPECT_ARG;
                goto cleanup;
            }
            out->filename = argv[i];
            continue;
        }
        if (strcmp(argv[i], "--help") == 0) {
            out->is_help = 1;
            continue;
        }
        freestanding[freestanding_len] = argv[i];
        freestanding_len++;
    }

    if (out->is_help) {
        if (freestanding_len != 0) {
            err = ERR_PARSE_UNEXPECTED_COUNT;
        }
        goto cleanup;
    }
    if (out->filename != NULL) {
        if (freestanding_len != 1) {
            err = ERR_PARSE_UNEXPECTED_COUNT;
            goto cleanup;
        }
        out->data = freestanding[0];
        goto cleanup;
    }
    if (freestanding_len != 2) {
        err = ERR_PARSE_UNEXPECTED_COUNT;
        goto cleanup;
    }
    out->tpl = freestanding[0];
    out->data = freestanding[1];
cleanup:
    free(freestanding);
    return err;
}

int main(int argc, char* argv[]) {
    args args;
    int err = parse_args(argc, argv, &args);
    if (err) {
        fprintf(stderr, "invalid arguments, try cgotpl --help\n");
        return EXIT_FAILURE;
    }
    if (args.is_help) {
        printf("usage: cgotpl ([TEMPLATE] | -f [FILENAME]) [DATA]\n");
        return EXIT_SUCCESS;
    }

    int result = EXIT_SUCCESS;
    stream data;
    json_value dot = JSON_NULL;
    char* out = NULL;

    stream_open_memory(&data, args.data, strlen(args.data));
    err = json_parse(&data, &dot);
    if (err) {
        long pos = 0;
        int st_err = stream_pos(&data, &pos);
        if (st_err) {
            fprintf(stderr, "failed to get data stream position: %d\n", st_err);
        }
        char* desc = json_describe_err(err);
        if (desc == NULL) {
            desc = "unknown error";
        }
        fprintf(stderr, "failed to parse data at offset %ld: %d (%s)\n", pos, err, desc);
        result = EXIT_FAILURE;
        goto cleanup;
    }

    stream tpl;
    if (args.filename) {
        err = stream_open_file(&tpl, args.filename);
        if (err) {
            fprintf(stderr, "failed to open file %s: %d\n", args.filename, err);
            result = EXIT_FAILURE;
            goto cleanup_json;
        }
    } else {
        stream_open_memory(&tpl, args.tpl, strlen(args.tpl));
    }

    err = template_eval_stream(&tpl, &dot, &out);
    if (err) {
        long pos = 0;
        int st_err = stream_pos(&tpl, &pos);
        if (st_err) {
            fprintf(stderr, "failed to get template stream position: %d\n", st_err);
        }
        char* desc = template_describe_err(err);
        if (desc == NULL) {
            desc = "unknown error";
        }
        fprintf(stderr, "failed to evaluate template at offset %ld: %d (%s)\n", pos, err, desc);
        result = EXIT_FAILURE;
        goto cleanup_tpl;
    }
    printf("%s", out);

cleanup_tpl:
    err = stream_close(&tpl);
    if (err) {
        fprintf(stderr, "failed to close template stream: %d\n", err);
    }
cleanup_json:
    json_value_free(&dot);
cleanup:
    free(out);
    err = stream_close(&data);
    if (err) {
        fprintf(stderr, "failed to close data stream: %d\n", err);
    }
    return result;
}

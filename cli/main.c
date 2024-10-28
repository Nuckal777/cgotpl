#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "json.h"
#include "stream.h"
#include "template.h"

int main (int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "usage: cgotpl [TEMPLATE] [DATA]\n");
        return EXIT_FAILURE;
    }

    int result = EXIT_SUCCESS;
    stream data;
    json_value dot;
    char* out = NULL;
    
    const char* tpl = argv[1];
    stream_open_memory(&data, argv[2], strlen(argv[2]));
    int err = json_parse(&data, &dot);
    if (err) {
        dot = JSON_NULL;
        fprintf(stderr, "failed to parse data: %d\n", err);
        result = EXIT_FAILURE;
        goto cleanup;
    }
    err = template_eval(tpl, strlen(tpl), &dot, &out);
    if (err) {
        fprintf(stderr, "failed to evaluate template: %d\n", err);
        result = EXIT_FAILURE;
        goto cleanup;
    }
    printf("%s", out);
cleanup:
    if (out) {
        free(out);
    }
    json_value_free(&dot);
    err = stream_close(&data);
    if (err) {
        fprintf(stderr, "failed to close data stream: %d\n", err);
    }
    return result;
}

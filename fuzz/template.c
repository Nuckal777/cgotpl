#include "template.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "stream.h"

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    json_value val;
    stream st;
    const char* json = "{\"a\": 28, \"b\": [true, false, null, [\"c\"]], \"d\": {\"e\": 43.2, \"f\": [{}, 7, {\"g\": []}]}}";
    stream_open_memory(&st, json, strlen(json));
    int err = json_parse(&st, &val);
    if (err) {
        return -1;
    }
    err = stream_close(&st);
    if (err) {
        return -1;
    }
    char* out;
    err = template_eval_mem((const char*)data, size, &val, &out);
    free(out);
    json_value_free(&val);
    return 0;
}

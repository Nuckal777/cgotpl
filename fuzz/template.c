#include "template.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "stream.h"

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    json_value val;
    stream st;
    const char* json = "{\"a\": 28, \"b\": [true, false, null, [\"c\"]], \"d\": {\"e\": 43.2}}";
    stream_open_memory(&st, json, strlen(json));
    int err = json_parse(&st, &val);
    if (err != 0) {
        return -1;
    }
    stream_close(&st);

    char* out;
    err = template_eval((const char*)data, size, &val, &out);
    free(out);
    json_value_free(&val);
    return 0;
}

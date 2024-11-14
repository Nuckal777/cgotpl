#include <stdint.h>

#include "json.h"
#include "stream.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    stream st;
    stream_open_memory(&st, data, size);
    json_value val;
    int err = json_parse(&st, &val);
    if (err != 0) {
        return -1;
    }
    json_value_free(&val);
    stream_close(&st);
    return 0;
}

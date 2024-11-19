#ifndef CGOTPL_STREAM
#define CGOTPL_STREAM

#include <stdio.h>

#define STREAM_MEMORY 1
#define STREAM_FILE 2

#define ERR_INVALID_UTF8 -700

typedef struct {
    const unsigned char* data;
    size_t len;
    size_t pos;
} buffer;

typedef struct {
    int ty;
    union {
        buffer data;
        FILE* file;
    } inner;
} stream;

// Opens stream backed by data up to len bytes.
void stream_open_memory(stream* stream, const void* data, size_t len);
// Opens stream on the file referenced by filename. Returns 0 on success.
int stream_open_file(stream* stream, const char* filename);
// Closes stream. Returns 0 on success.
int stream_close(stream* stream);
int stream_pos(stream* stream, long* pos);
int stream_read(stream* stream, unsigned char* out);
int stream_next_utf8_cp(stream* st, unsigned char* out, size_t* len);
int stream_seek(stream* stream, size_t relative);

#endif
